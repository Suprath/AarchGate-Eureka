import os
import sys
import json
import time
import subprocess
import random

def generate_sample_logs(filepath, size_mb=250):
    print(f"[*] Generating {size_mb}MB of realistic high-density NDJSON logs at {filepath}...")
    
    status_options = [200, 301, 404, 500, 502, 503]
    records_written = 0
    bytes_written = 0
    target_bytes = size_mb * 1024 * 1024
    
    t0 = time.perf_counter()
    with open(filepath, "w") as f:
        while bytes_written < target_bytes:
            # Generate randomized but patterned log structure
            status = random.choice(status_options)
            latency = random.randint(5, 500)
            
            # Add text markers for grep comparisons
            level = "INFO"
            if status >= 500:
                level = "ERROR" if random.random() < 0.8 else "WARNING"
                
            line = f'{{"level":"{level}","status":{status},"latency":{latency},"message":"API call completed successfully"}}\n'
            f.write(line)
            bytes_written += len(line)
            records_written += 1
            
            if records_written % 500000 == 0:
                print(f"    [+] Generated {bytes_written / (1024*1024):.1f} MB ({records_written} records)...")
                
    t1 = time.perf_counter()
    print(f"[+] Generation complete in {t1 - t0:.2f}s! Total records: {records_written}\n")
    return records_written

def run_grep_test(filepath, pattern):
    print(f"[*] Running grep benchmark for pattern: '{pattern}'...")
    t0 = time.perf_counter()
    
    # Run BSD/GNU grep to search for pattern inside file
    try:
        # We count matching lines
        res = subprocess.run(["grep", "-c", pattern, filepath], capture_output=True, text=True)
        count = int(res.stdout.strip())
    except Exception as e:
        print(f"[-] Grep search failed: {e}")
        count = 0
        
    t1 = time.perf_counter()
    elapsed = (t1 - t0) * 1000.0
    print(f"    [+] Grep matches  : {count}")
    print(f"    [+] Grep duration : {elapsed:.2f} ms")
    return elapsed, count

def run_python_test(filepath):
    print("[*] Running standard Python JSON procedural scan (evaluating status == 500 AND latency > 100)...")
    t0 = time.perf_counter()
    
    count = 0
    try:
        with open(filepath, "r") as f:
            for line in f:
                record = json.loads(line)
                if record.get("status") == 500 and record.get("latency", 0) > 100:
                    count += 1
    except Exception as e:
        print(f"[-] Python search failed: {e}")
        
    t1 = time.perf_counter()
    elapsed = (t1 - t0) * 1000.0
    print(f"    [+] Python matches  : {count}")
    print(f"    [+] Python duration : {elapsed:.2f} ms")
    return elapsed, count

def run_aglex_logical_test(bin_path, filepath, query):
    print(f"[*] Running ag-lex compiled JIT query: \"{query}\"...")
    t0 = time.perf_counter()
    
    try:
        res = subprocess.run([bin_path, query, filepath], capture_output=True, text=True)
        output = res.stdout
    except Exception as e:
        print(f"[-] ag-lex execution failed: {e}")
        return 0, 0, ""
        
    t1 = time.perf_counter()
    elapsed = (t1 - t0) * 1000.0
    
    # Parse count from ag-lex stdout
    count = 0
    for line in output.splitlines():
        if "TOTAL MATCHING RECORDS" in line:
            parts = line.split(":")
            if len(parts) > 1:
                try:
                    count = int(parts[1].strip())
                except:
                    pass
                    
    print(f"    [+] ag-lex matches  : {count}")
    print(f"    [+] ag-lex duration : {elapsed:.2f} ms")
    return elapsed, count, output

def main():
    print("=================================================================")
    print("      AARCHGATE-LEX VS THE WORLD: THE HIGH-SPEED LOG CHALLENGE  ")
    print("=================================================================\n")
    
    workspace_dir = "/Users/suprathps/code/AarchGate-Eureka"
    log_file = os.path.join(workspace_dir, "benchmark_logs.json")
    bin_path = os.path.join(workspace_dir, "build", "ag-lex")
    
    # 1. Ensure the ag-lex binary is compiled
    if not os.path.exists(bin_path):
        print(f"[-] Error: ag-lex compiled binary not found at {bin_path}!")
        print("    Please compile the binary using CMake first.")
        sys.exit(1)
        
    # 2. Generate test logs if they don't exist yet
    if not os.path.exists(log_file):
        generate_sample_logs(log_file, size_mb=250)
    else:
        print(f"[+] Reusing existing benchmark log file at {log_file} ({os.path.getsize(log_file)/(1024*1024):.1f} MB)\n")
        
    # 3. Running Benchmark 1: Substring Match Challenge
    print("--- CHALLENGE 1: SUBSTRING TEXT GREP MATCH (Pattern: 'ERROR') ---")
    grep_time, grep_count = run_grep_test(log_file, "ERROR")
    
    ag_string_time, ag_string_count, ag_output = run_aglex_logical_test(bin_path, log_file, "contains(\"ERROR\")")
    
    # 4. Running Benchmark 2: Multi-Field Numerical Logic Challenge (status == 500 && latency > 100)
    print("\n--- CHALLENGE 2: MULTI-FIELD NUMERICAL LOGIC (status == 500 AND latency > 100) ---")
    py_time, py_count = run_python_test(log_file)
    
    ag_logic_time, ag_logic_count, ag_logic_output = run_aglex_logical_test(bin_path, log_file, "status == 500 AND latency > 100")
    
    # 5. Display scoreboard
    print("\n=================================================================")
    print("                    FINAL BENCHMARK SCOREBOARD                   ")
    print("=================================================================")
    print(f"  CHALLENGE 1: Substring Text Matching ('ERROR')")
    print(f"    - Standard grep        : {grep_time:.2f} ms")
    print(f"    - AarchGate ag-lex     : {ag_string_time:.2f} ms  <--- \033[1;32m{grep_time / max(1.0, ag_string_time):.1f}x SPEEDUP!\033[0m")
    print(f"    - Parity Verification  : {'PASSED' if grep_count == ag_string_count else 'FAILED'}")
    
    print(f"\n  CHALLENGE 2: Complex Numerical Logic (status == 500 AND latency > 100)")
    print(f"    - Python Procedural    : {py_time:.2f} ms")
    print(f"    - AarchGate ag-lex     : {ag_logic_time:.2f} ms  <--- \033[1;32m{py_time / max(1.0, ag_logic_time):.1f}x SPEEDUP!\033[0m")
    print(f"    - Parity Verification  : {'PASSED' if py_count == ag_logic_count else 'FAILED'}")
    print("=================================================================\n")
    
    # Print the native performance HUD for the JIT engine
    print(ag_logic_output)

if __name__ == "__main__":
    main()
