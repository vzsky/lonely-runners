#include <iostream>
#include <vector>
#include <set>
#include <thread>
#include <atomic>
#include <mutex>
#include <cmath>
#include <iomanip>
#include <chrono>

using namespace std;

// Globals for read-only fast access
uint16_t p_val, k_val;
vector<uint16_t> R_flat;
size_t num_R;
vector<uint16_t> mod_table;

// Concurrency controls
atomic<bool> global_failed(false);
atomic<uint64_t> global_checked_count(0);
mutex queue_mtx;
vector<vector<uint16_t>> tasks;

// Ultra-fast inner loop check
inline bool is_happy(const vector<uint16_t>& b) {
    // R(0) Special Case
    for (int s = 0; s < k_val; ++s) {
        bool s_works = true;
        for (int i = 0; i < k_val; ++i) {
            if (mod_table[s * b[i]] == 0) {
                s_works = false; break;
            }
        }
        if (s_works) return true;
    }

    // R(r) Standard Cases (flattened contiguous memory traversal)
    for (size_t r_idx = 0; r_idx < num_R; ++r_idx) {
        const uint16_t* R_ptr = &R_flat[r_idx * k_val];
        for (int s = 0; s < k_val; ++s) {
            bool s_works = true;
            for (int i = 0; i < k_val; ++i) {
                uint16_t val = mod_table[s * b[i] + R_ptr[i]];
                if (val == 0 || val == k_val) {
                    s_works = false; break;
                }
            }
            if (s_works) return true;
        }
    }
    return false;
}

// Recursive generator run by worker threads
bool generate_and_check(int index, vector<uint16_t>& b, int zero_count, uint64_t& local_checks) {
    if (global_failed.load(memory_order_relaxed)) return false;

    if (index == k_val) {
        if (zero_count == 0) return true; 
        
        if (!is_happy(b)) {
            bool expected = false;
            // Ensure only the first failing thread prints the output
            if (global_failed.compare_exchange_strong(expected, true)) {
                cout << "\n\n❌ Problem Failed! Found an unhappy tuple: (";
                for (int i = 0; i < k_val; ++i) {
                    cout << b[i] << (i == k_val - 1 ? "" : ", ");
                }
                cout << ")\n" << endl;
            }
            return false;
        }
        
        local_checks++;
        // Batch atomic updates to avoid thread contention overhead
        if (local_checks >= 5000) {
            global_checked_count.fetch_add(local_checks, memory_order_relaxed);
            local_checks = 0;
        }
        return true;
    }
    
    for (int val = 0; val <= k_val; ++val) {
        b[index] = val;
        int new_zeros = zero_count + (val == 0 ? 1 : 0);
        
        // Exact integer math: ignores >= (k+1)/2 zeros safely
        if (new_zeros * 2 >= k_val + 1) continue; 
        
        if (!generate_and_check(index + 1, b, new_zeros, local_checks)) return false;
    }
    return true;
}

// Thread lifecycle
void worker_thread() {
    vector<uint16_t> b(k_val);
    uint64_t local_checks = 0;

    while (true) {
        if (global_failed.load(memory_order_relaxed)) return;

        vector<uint16_t> prefix;
        {
            lock_guard<mutex> lock(queue_mtx);
            if (tasks.empty()) break;
            prefix = tasks.back();
            tasks.pop_back();
        }

        int initial_zeros = 0;
        for (size_t i = 0; i < prefix.size(); ++i) {
            b[i] = prefix[i];
            if (prefix[i] == 0) initial_zeros++;
        }

        generate_and_check(prefix.size(), b, initial_zeros, local_checks);
    }
    // Flush remaining
    global_checked_count.fetch_add(local_checks, memory_order_relaxed);
}

// Computes exact permutations to feed the progress tracker
uint64_t calculate_total_tuples() {
    auto nCr = [](int n, int r) -> uint64_t {
        uint64_t res = 1;
        for (int i = 0; i < r; ++i) res = res * (n - i) / (i + 1);
        return res;
    };
    
    uint64_t total = 0;
    for (int z = 1; z * 2 < k_val + 1; ++z) {
        uint64_t powers = 1;
        for (int i = 0; i < k_val - z; ++i) powers *= k_val;
        total += nCr(k_val, z) * powers;
    }
    return total;
}

int main() {
    cout << "--- Program Initialized ---" << endl;
    cout << "Enter prime p and integer k: " << flush;
    if (!(cin >> p_val >> k_val)) return 0;
    
    auto start_time = chrono::high_resolution_clock::now();

    // 1. Build Precomputed Modulo Table
    int max_val = (k_val - 1) * k_val + k_val; 
    mod_table.resize(max_val + 1);
    for (int i = 0; i <= max_val; ++i) {
        mod_table[i] = i % (k_val + 1);
    }

    // 2. Generate and Flatten Unique R(r)
    cout << "Generating R(r) vectors..." << flush;
    set<vector<uint16_t>> unique_R;
    for (long long r = 1; r < p_val; ++r) {
        vector<uint16_t> current_R(k_val);
        for (long long i = 1; i <= k_val; ++i) {
            long long rem = (r * i) % p_val;
            current_R[i - 1] = ((k_val + 1) * rem) / p_val; 
        }
        unique_R.insert(current_R);
    }
    num_R = unique_R.size();
    R_flat.resize(num_R * k_val);
    size_t idx = 0;
    for (const auto& vec : unique_R) {
        for (int i = 0; i < k_val; ++i) R_flat[idx++] = vec[i];
    }
    cout << " Done.\n";
    
    // 3. Setup Task Queue 
    for (int v0 = 0; v0 <= k_val; ++v0) {
        if (k_val == 1) {
            if ((v0 == 0) * 2 < k_val + 1) tasks.push_back({(uint16_t)v0});
        } else {
            for (int v1 = 0; v1 <= k_val; ++v1) {
                int z_count = (v0 == 0 ? 1 : 0) + (v1 == 0 ? 1 : 0);
                if (z_count * 2 < k_val + 1) tasks.push_back({(uint16_t)v0, (uint16_t)v1});
            }
        }
    }

    uint64_t total_expected = calculate_total_tuples();
    cout << "Flattened " << num_R << " unique R(r) vectors.\n";
    cout << "Total valid tuples to check: " << total_expected << "\n\n";

    // 4. Launch CPU Threads
    unsigned int num_cores = thread::hardware_concurrency();
    vector<thread> threads;
    for (unsigned int i = 0; i < num_cores; ++i) {
        threads.emplace_back(worker_thread);
    }

    // 5. Monitor Progress on Main Thread (Every 10%)
    int next_threshold = 10;
    while (!global_failed.load(memory_order_relaxed) && global_checked_count.load(memory_order_relaxed) < total_expected) {
        this_thread::sleep_for(chrono::milliseconds(100)); // Sleep slightly longer to save CPU cycles
        
        uint64_t current = global_checked_count.load(memory_order_relaxed);
        double pct = 100.0 * current / total_expected;
        
        if (pct >= next_threshold) {
            cout << "Progress: " << next_threshold << "% (" << current << " / " << total_expected << ") tuples checked." << endl;
            next_threshold += 10;
        }
    }

    // Wait for all worker threads to finish
    for (auto& t : threads) t.join();

    auto end_time = chrono::high_resolution_clock::now();
    chrono::duration<double> diff = end_time - start_time;

    // Output Results
    if (!global_failed.load()) {
        cout << "Progress: 100% (" << total_expected << " / " << total_expected << ") tuples checked.\n";
        cout << "\n✅ Problem Solved: All required tuples are happy!" << endl;
    }
    
    cout << "Time elapsed: " << fixed << setprecision(3) << diff.count() << " seconds on " << num_cores << " CPU threads.\n";

    return 0;
}
