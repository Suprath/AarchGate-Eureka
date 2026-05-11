# AarchGate-Eureka ⚡

**AarchGate-Eureka** is an ultra-high-throughput, zero-copy, JIT-compiled log search and evaluation engine designed for modern Apple Silicon and ARM64 platforms. By transitioning from traditional ASCII string parsing to direct columnar **bit-sliced logical execution**, Eureka achieves physical memory-bus saturation, scanning logs at **36+ GB/s** and processing over **1.64 Billion Records Per Second (RPS)** on a standard MacBook.

---

## 🚀 The Core Bottlenecks & Eureka's Architecture

Standard log search engines (e.g., Elasticsearch, Grep, standard JSON parsers) fail to scale because they pay two critical CPU "taxes" on every search:

### 1. The Parsing Tax (String-to-Int Bottleneck)
Converting human-readable NDJSON log lines into parsed, in-memory structures (like integers or float values) requires continuous string parsing, tokenization, and validation. Standard parsers max out around **1–2 GB/s** per core.
* **Eureka's Solution**: Uses `simdjson` for initial, vectorized structural indexing and token delimiter identification at **4+ GB/s**, which is then converted into native **AarchGate Binary (`.agb`)** bit-planes.

### 2. The Transcoding Tax (Row-to-Column Bottleneck)
Standard databases store records horizontally (row-major). Performing scalar conditions (e.g., `status == 500`) requires loading entire rows into cache lines, creating massive cache waste.
* **Eureka's Solution**: Converts data into **Bit-Sliced columnar slices** (columns represented as 64 separate bit-planes). JIT-compiled ARM64 assembly kernels then evaluate logic across all 64 elements simultaneously using fast, vectorized bitwise operations (`AND`, `BIC`, `EOR`).

---

## 📁 Repository Directory Structure

* **`bin/`**: Contains execution CLI entrypoints (e.g., `ag-lex` binary).
* **`benchmarks/`**: Memory bus saturation benchmarking suites (`bench_native_scan.cpp`).
* **`include/`**: C++ header declarations (`include/lex/exporter.hpp`, `include/lex/ingester.hpp`).
* **`src/`**: Primary implementation files for streaming ingesters and exporters.
* **`external/`**: Integrated 3rd-party submodules (`AarchGate-ML`, `simdjson`, `asmjit`).

---

## 🛠️ How to Build and Compile

### Prerequisites
* **macOS** (Apple Silicon M1/M2/M3)
* **CMake** (v3.20+)
* **Clang/GCC** supporting C++20 and ARM64 target compilation

### Compilation Steps

1. Configure the build directory under the optimized **Release** profile:
   ```bash
   cmake -DCMAKE_BUILD_TYPE=Release -B build
   ```

2. Compile all static libraries, command-line utilities, and benchmarks:
   ```bash
   cd build && make -j$(sysctl -n hw.logicalcpu)
   ```

The output binaries will be placed in your `build/` directory, including:
* `./build/ag-lex` (The primary query and exporter CLI)
* `./build/bench_native_scan` (The performance saturation test executable)

---

## 💾 The AarchGate Binary (`.agb`) Format Specification

The `.agb` format is a streamlined, zero-copy binary layout designed to point CPU evaluation registers directly to alignment boundaries. 

* **64-Record Blocks**: Data is serialized in blocks of exactly 64 records.
* **Bit-Sliced Columnar Planes**: Inside each block, every schema field occupies exactly 64 sequential `uint64_t` bit-planes (representing bits $0 \dots 63$ of the 64 scalar values).
* **Layout Structure**: For a schema of $K$ fields, a block is serialized as:
  $$\text{Block Size} = K \times 64 \times 8 \text{ bytes} = K \times 512 \text{ bytes}$$
* **Memory Alignment**: Every block is aligned to a 64-byte boundary. Memory mapping (`mmap`) allows the engine to pass block offset pointers directly to the JIT evaluation kernels, paying **zero** transcoding or buffering costs.

---

## 🛠️ Translating JSON Logs to `.agb`

To convert a standard NDJSON log file to the high-performance `.agb` bit-sliced format:

```bash
./build/ag-lex --convert input_logs.json output_logs.agb
```

---

## ⚡ Running the Saturation Benchmark

The benchmark generates an optimal **1.86 GB AGB file**, memory-maps it, pre-faults page tables to load it into active RAM, compiles a 3-field expression to raw machine code, and runs parallel evaluations:

```bash
./build/bench_native_scan
```

### 🏆 Local Silicon Scoreboard

Evaluating the complex expression `status == 500 AND latency > 100 AND severity == 3` on 4 performance cores:

```ansi
========================================================
               NATIVE MEMORY-WALL BENCHMARK             
========================================================
  TOTAL MATCHES FOUND     : 41,600,000
  TOTAL DATA INGESTED     : 1.860 GB
  TOTAL RECORDS SCANNED   : 83,200,000
  TOTAL SCANNED TIME      : 50.68 ms
  PROCESSING BANDWIDTH    : 36.70 GB/sec
  RECORDS PER SECOND (RPS): 1641.79 Million RPS
========================================================

[*] Executing pure hardware memory-bus saturation sweep (multi-channel parallel)...

========================================================
                 HARDWARE MEMORY BUS STATS              
========================================================
  TOTAL DATA READ         : 1.860 GB
  SWEEP TIME ELAPSED      : 25.07 ms
  PHYSICAL BUS BANDWIDTH   : 74.19 GB/sec
  VERIFIED RESULT CHECKSUM: 0xaaaaaaaaa9fec040
========================================================
```

---

## 🔍 Memory Diagnostics Tip

> [!TIP]
> **Virtual Memory Swap / Page Thrashing**
>
> If your system has low free physical RAM (under ~2 GB), running benchmarks with large datasets (e.g., 10 GB) will force the macOS pager to write memory blocks to SSD swap, capping execution speeds to SSD limits (**~0.36 GB/s**). 
> 
> Keep the benchmark dataset sized to **1.86 GB** to bypass paging and verify the true **74+ GB/s physical memory-bus speed** of your Mac!
