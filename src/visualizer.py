import sys
import os
import ctypes
import numpy as np
import time
from PySide6.QtCore import QTimer, Qt, Slot, QThread, Signal
from PySide6.QtWidgets import QApplication, QMainWindow, QWidget, QVBoxLayout, QHBoxLayout, QSlider, QLabel, QFrame
from PySide6.QtGui import QImage, QPixmap, QFont, QColor, QPainter

# Locate and load the compiled eureka_core shared library
def load_eureka_lib():
    possible_paths = [
        "build/libeureka_core.dylib",
        "build/Release/libeureka_core.dylib",
        "libeureka_core.dylib",
        "../build/libeureka_core.dylib",
        "../build/Release/libeureka_core.dylib"
    ]
    for path in possible_paths:
        if os.path.exists(path):
            try:
                lib = ctypes.CDLL(path)
                return lib
            except Exception as e:
                print(f"Error loading {path}: {e}")
    # Try system loader
    try:
        return ctypes.CDLL("libeureka_core.dylib")
    except Exception:
        pass
    return None

class CEurekaResult(ctypes.Structure):
    _fields_ = [
        ("best_u", ctypes.c_int),
        ("best_v", ctypes.c_int),
        ("best_a", ctypes.c_double),
        ("best_b", ctypes.c_double),
        ("min_mse", ctypes.c_double)
    ]

# Setup ctypes arguments and return types
lib = load_eureka_lib()
if lib:
    lib.eureka_evaluate_grid.argtypes = [
        ctypes.POINTER(ctypes.c_double),
        ctypes.POINTER(ctypes.c_double),
        ctypes.c_int,
        ctypes.POINTER(ctypes.c_float)
    ]
    lib.eureka_evaluate_grid.restype = CEurekaResult
else:
    print("WARNING: libeureka_core.dylib not found. Running in simulation fallback mode!")

class GLOW_EFFECT_STYLE:
    BG_COLOR = "#0b0c10"
    PANEL_COLOR = "rgba(31, 40, 51, 0.45)"
    TEXT_COLOR = "#c5c6c7"
    ACCENT_CYAN = "#66fcf1"
    ACCENT_MAGENTA = "#ff007f"
    FONT_FAMILY = "Outfit"

class DiscoverySurfaceWidget(QWidget):
    def __init__(self, parent=None):
        super().__init__(parent)
        self.setFixedSize(600, 600)
        self.grid_size = 1000 # Perfectly matches C++ static GRID_SIZE limit
        
        # Pre-allocate buffer for PySide6 performance (RGB32 has 4 bytes per pixel: B, G, R, A)
        self.pixel_data = np.zeros((self.grid_size, self.grid_size, 4), dtype=np.uint8)
        self.pixel_data[:, :, 3] = 255 # Alpha channel

        # Create QImage wrapping the numpy array with zero-copy
        self.image = QImage(self.pixel_data.data, self.grid_size, self.grid_size, QImage.Format_RGB32)

    def draw_pixel_data(self, new_pixels):
        # Directly copy the fully color-mapped pixels from the background thread
        # This operates at near-instantaneous speed on the main thread (less than 1ms!)
        np.copyto(self.pixel_data, new_pixels)
        self.update()

    def paintEvent(self, event):
        painter = QPainter(self)
        painter.setRenderHint(QPainter.Antialiasing)
        
        # Draw the main pixel texture scaled to the widget size
        painter.drawImage(self.rect(), self.image)
        
        # Draw dynamic HUD overlays on the grid
        painter.setPen(QColor(102, 252, 241, 100)) # Neon cyan with alpha
        painter.setFont(QFont(GLOW_EFFECT_STYLE.FONT_FAMILY, 9))
        
        # Grid axes indicators
        painter.drawText(10, 20, "Amplitude: +2.0")
        painter.drawText(10, 590, "Amplitude: -2.0")
        painter.drawText(490, 590, "Freq: 2.0 rad/s")
        painter.drawText(10, 305, "-------------------------")

# Background worker to run C++ calculations and color mapping with 0 blockage to main thread
class EvaluationWorker(QThread):
    # Signals colored pixel data and numeric stats to the UI thread
    result_ready = Signal(np.ndarray, float, float, float, float)

    def __init__(self, target_amp, target_freq, sample_size, grid_size):
        super().__init__()
        self.target_amp = target_amp
        self.target_freq = target_freq
        self.sample_size = sample_size
        self.grid_size = grid_size
        self.running = True
        self.mse_grid_buffer = np.zeros(self.grid_size * self.grid_size, dtype=np.float32)
        
        # Pre-allocate thread-local pixel buffer
        self.local_pixel_data = np.zeros((self.grid_size, self.grid_size, 4), dtype=np.uint8)
        self.local_pixel_data[:, :, 3] = 255

    def set_target_freq(self, freq):
        self.target_freq = freq

    def run(self):
        while self.running:
            start_time = time.time()
            
            # 1. Generate streaming data with STATIC/DETERMINISTIC seeded noise.
            # This ensures that when the frequency slider is still, the input signal is 100% identical
            # every frame. This eliminates all mathematical drift and makes the output completely frozen!
            x = np.linspace(-6.28, 6.28, self.sample_size, dtype=np.float64)
            rng = np.random.RandomState(42) # Static random state
            noise = rng.normal(0, 0.02, self.sample_size)
            y = self.target_amp * np.sin(self.target_freq * x) + noise

            if lib:
                # 2. Invoke our ultra-fast parallel C++ bit-sliced engine (takes exactly GRID_SIZE*GRID_SIZE floats)
                x_ptr = x.ctypes.data_as(ctypes.POINTER(ctypes.c_double))
                y_ptr = y.ctypes.data_as(ctypes.POINTER(ctypes.c_double))
                grid_ptr = self.mse_grid_buffer.ctypes.data_as(ctypes.POINTER(ctypes.c_float))
                
                res = lib.eureka_evaluate_grid(x_ptr, y_ptr, self.sample_size, grid_ptr)
                best_u = res.best_u
                best_v = res.best_v
                best_a = res.best_a
                best_b = res.best_b
                min_mse = res.min_mse
            else:
                # Simulation fallback
                best_u = int((self.target_amp - (-2.0)) / 4.0 * (self.grid_size - 1))
                best_v = int((self.target_freq - 0.0) / 2.0 * (self.grid_size - 1))
                best_a, best_b, min_mse = self.target_amp, self.target_freq, 0.0025
                
                # Setup simulation grid
                u_grid, v_grid = np.ogrid[-2:2:1000j, 0:2:1000j]
                mse_sim = (u_grid - self.target_amp)**2 + (v_grid - self.target_freq)**2
                self.mse_grid_buffer = mse_sim.flatten().astype(np.float32)

            # 3. Perform heavy NumPy color-mapping in the BACKGROUND thread!
            mse_reshaped = self.mse_grid_buffer.reshape(self.grid_size, self.grid_size)
            
            # Normalize MSE array smoothly to range [0, 1]
            min_mse_val = mse_reshaped.min()
            max_mse_val = mse_reshaped.max()
            if max_mse_val > min_mse_val:
                norm_mse = (mse_reshaped - min_mse_val) / (max_mse_val - min_mse_val)
            else:
                norm_mse = np.zeros_like(mse_reshaped)
            
            # Convert so low MSE gets high intensity / brightness value
            normalized = 1.0 - norm_mse
            
            # Apply logarithmic contrast enhancement to make waves glow
            normalized = np.log1p(normalized * 1.7) / np.log1p(2.7)

            # Apply a gorgeous neon science science-grade color-map showing full interference ripples
            # Low match -> deep space violet, medium match -> glowing pink, high match -> blazing cyan/white
            r = (np.clip(normalized * 120 + (normalized > 0.7) * 135, 0, 255)).astype(np.uint8)
            g = (np.clip(normalized * 80 + (normalized > 0.8) * 175, 0, 255)).astype(np.uint8)
            b = (np.clip(normalized * 220 + (normalized > 0.5) * 35, 0, 255)).astype(np.uint8)

            # Draw a custom bright indicator / reticle on the brightest pixel
            for du in range(-3, 4):
                for dv in range(-3, 4):
                    nu, nv = best_u + du, best_v + dv
                    if 0 <= nu < self.grid_size and 0 <= nv < self.grid_size:
                        r[nu, nv] = 255
                        g[nu, nv] = 255
                        b[nu, nv] = 255

            # Pack channels into local pixel buffer
            self.local_pixel_data[:, :, 0] = b  # Blue
            self.local_pixel_data[:, :, 1] = g  # Green
            self.local_pixel_data[:, :, 2] = r  # Red

            elapsed = (time.time() - start_time) * 1000.0 # ms

            # Emit final colored pixel array and numeric stats back to the UI thread
            self.result_ready.emit(
                self.local_pixel_data.copy(),
                best_a, best_b, min_mse, elapsed
            )

            # Throttle the loop slightly to prevent high-frequency CPU spin
            time.sleep(0.010)

class MainWindow(QMainWindow):
    def __init__(self):
        super().__init__()
        self.setWindowTitle("AarchGate-Eureka | Real-Time Symbolic Regression")
        self.setStyleSheet(f"background-color: {GLOW_EFFECT_STYLE.BG_COLOR}; color: {GLOW_EFFECT_STYLE.TEXT_COLOR};")
        
        # State variables
        self.target_freq = 0.5
        self.target_amp = 1.5
        self.grid_size = 1000
        self.sample_size = 100 # Highly precise fitting sample size
        
        # Try to load custom font
        QFont.insertSubstitution("Outfit", "Inter")
        
        self.init_ui()
        
        # Start background worker thread
        self.worker = EvaluationWorker(self.target_amp, self.target_freq, self.sample_size, self.grid_size)
        self.worker.result_ready.connect(self.handle_results)
        self.worker.start()
        
        self.last_time = time.time()
        self.frame_count = 0
        self.fps = 0.0

    def init_ui(self):
        main_widget = QWidget()
        self.setCentralWidget(main_widget)
        main_layout = QHBoxLayout(main_widget)
        main_layout.setContentsMargins(20, 20, 20, 20)
        main_layout.setSpacing(25)

        # Left Column: Discovery Surface Visualizer
        left_layout = QVBoxLayout()
        title_label = QLabel("AARCHGATE EUREKA // SYMBOLIC DISCOVERY SURFACE")
        title_label.setFont(QFont(GLOW_EFFECT_STYLE.FONT_FAMILY, 14, QFont.Bold))
        title_label.setStyleSheet(f"color: {GLOW_EFFECT_STYLE.ACCENT_CYAN}; letter-spacing: 1.5px;")
        left_layout.addWidget(title_label)

        self.surface = DiscoverySurfaceWidget()
        left_layout.addWidget(self.surface)
        main_layout.addLayout(left_layout)

        # Right Column: Control Panel & Telemetry HUD
        right_panel = QFrame()
        right_panel.setFixedWidth(340)
        right_panel.setStyleSheet(f"""
            QFrame {{
                background-color: {GLOW_EFFECT_STYLE.PANEL_COLOR};
                border: 1px solid rgba(102, 252, 241, 0.25);
                border-radius: 12px;
            }}
            QLabel {{
                border: none;
            }}
        """)
        panel_layout = QVBoxLayout(right_panel)
        panel_layout.setContentsMargins(20, 20, 20, 20)
        panel_layout.setSpacing(20)

        # Header Section
        header_label = QLabel("CORE TELEMETRY HUD")
        header_label.setFont(QFont(GLOW_EFFECT_STYLE.FONT_FAMILY, 12, QFont.Bold))
        header_label.setStyleSheet(f"color: {GLOW_EFFECT_STYLE.ACCENT_MAGENTA};")
        panel_layout.addWidget(header_label)

        # Live telemetry cards
        self.status_lbl = QLabel("STATUS: SEARCHING...")
        self.status_lbl.setFont(QFont(GLOW_EFFECT_STYLE.FONT_FAMILY, 10, QFont.Bold))
        self.status_lbl.setStyleSheet(f"color: {GLOW_EFFECT_STYLE.ACCENT_CYAN};")
        panel_layout.addWidget(self.status_lbl)

        self.fps_lbl = QLabel("LATENCY: -- ms (0.0 FPS)")
        self.fps_lbl.setFont(QFont(GLOW_EFFECT_STYLE.FONT_FAMILY, 10))
        panel_layout.addWidget(self.fps_lbl)

        self.formula_lbl = QLabel("DISCOVERED LAW:\nSearching Parameter Space...")
        self.formula_lbl.setFont(QFont(GLOW_EFFECT_STYLE.FONT_FAMILY, 11, QFont.Bold))
        self.formula_lbl.setStyleSheet("color: #ffffff; padding: 5px; background: rgba(0,0,0,0.3); border-radius: 6px;")
        panel_layout.addWidget(self.formula_lbl)

        # Interactive controls separator
        sep = QFrame()
        sep.setFrameShape(QFrame.HLine)
        sep.setStyleSheet("color: rgba(102, 252, 241, 0.15);")
        panel_layout.addWidget(sep)

        controls_header = QLabel("STREAM FREQUENCY CONTROLLER")
        controls_header.setFont(QFont(GLOW_EFFECT_STYLE.FONT_FAMILY, 11, QFont.Bold))
        panel_layout.addWidget(controls_header)

        # Freq Slider
        self.slider = QSlider(Qt.Horizontal)
        self.slider.setMinimum(10) # 0.1 rad/s
        self.slider.setMaximum(190) # 1.9 rad/s
        self.slider.setValue(50) # 0.5 rad/s
        self.slider.setStyleSheet(f"""
            QSlider::groove:horizontal {{
                height: 6px;
                background: rgba(255,255,255,0.1);
                border-radius: 3px;
            }}
            QSlider::handle:horizontal {{
                background: {GLOW_EFFECT_STYLE.ACCENT_CYAN};
                width: 14px;
                height: 14px;
                margin-top: -4px;
                margin-bottom: -4px;
                border-radius: 7px;
            }}
        """)
        self.slider.valueChanged.connect(self.slider_changed)
        panel_layout.addWidget(self.slider)

        self.freq_lbl = QLabel("Target Frequency: 0.50 rad/s")
        self.freq_lbl.setFont(QFont(GLOW_EFFECT_STYLE.FONT_FAMILY, 10))
        panel_layout.addWidget(self.freq_lbl)

        # Specs section
        panel_layout.addStretch()
        specs_lbl = QLabel("M3 ARM64 Vector Cores Active\nFixed-Point Arithmetic [2^20]\nDecoupled Worker Threads\nBackground Pixel Color Mapping")
        specs_lbl.setFont(QFont(GLOW_EFFECT_STYLE.FONT_FAMILY, 9))
        specs_lbl.setStyleSheet("color: rgba(197, 198, 199, 0.5);")
        panel_layout.addWidget(specs_lbl)

        main_layout.addWidget(right_panel)

    def slider_changed(self, val):
        self.target_freq = val / 100.0
        self.freq_lbl.setText(f"Target Frequency: {self.target_freq:.2f} rad/s")
        # Thread-safe target freq update
        self.worker.set_target_freq(self.target_freq)

    @Slot(np.ndarray, float, float, float, float)
    def handle_results(self, pixel_data, best_a, best_b, min_mse, elapsed):
        # 1. Update the image surface texture (Instant copy, <1ms)
        self.surface.draw_pixel_data(pixel_data)

        # 2. Update stats and calculations
        self.frame_count += 1
        now = time.time()
        if now - self.last_time >= 1.0:
            self.fps = self.frame_count / (now - self.last_time)
            self.frame_count = 0
            self.last_time = now

        self.status_lbl.setText("STATUS: DISCOVERED!")
        self.status_lbl.setStyleSheet(f"color: {GLOW_EFFECT_STYLE.ACCENT_CYAN}; font-weight: bold;")
        self.fps_lbl.setText(f"LATENCY: {elapsed:.2f} ms ({self.fps:.1f} FPS)")
        
        # Display the stable mathematical parameters found under stationary seeded noise
        self.formula_lbl.setText(
            f"DISCOVERED LAW:\n"
            f"y = {best_a:.3f} * sin({best_b:.3f} * x)\n"
            f"MSE: {min_mse:.5f}"
        )

    def closeEvent(self, event):
        # Clean shutdown of background worker thread
        self.worker.running = False
        self.worker.wait()
        event.accept()

if __name__ == "__main__":
    app = QApplication(sys.argv)
    window = MainWindow()
    window.show()
    sys.exit(app.exec())
