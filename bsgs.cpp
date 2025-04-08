#include <iostream>
#include <atomic>
#include <memory>
#include <fstream>
#include <string>
#include <cstdlib>
#include <vector>
#include <unordered_map>
#include <cmath>
#include <gmpxx.h>
#include <chrono>
#include <cassert>
#include <cstdio>
#include <sys/stat.h>
#include <xxhash.h>
#include <omp.h>
#include <unistd.h>
#include <iomanip>

using namespace std;

// Constants and typedefs
typedef array<mpz_class, 2> Point;

const mpz_class modulo("FFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFEFFFFFC2F", 16);
const mpz_class order("FFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFEBAAEDCE6AF48A03BBFD25E8CD0364141", 16);
const mpz_class Gx("79BE667EF9DCBBAC55A06295CE870B07029BFCDB2DCE28D959F2815B16F81798", 16);
const mpz_class Gy("483ada7726a3c4655da4fbfc0e1108a8fd17b448a68554199c47d08ffb10d4b8", 16);
const Point PG = {Gx, Gy};
const Point Z = {0, 0};

// Global variables
int puzzle = 30;
string puzzle_pubkey = "030d282cf2ff536d2c42f105d0b8588821a915dc3f9a05bd98bb23af67a2e92a5b";
int threads = 0;
bool verbose = false;
const size_t MAX_TABLE_SIZE = 200 * 1024 * 1024; // 200MB per part

// Function declarations
void print_help();
bool validate_pubkey(const string& pubkey);
Point parse_pubkey(const string& pubkey);
inline Point add(const Point& P, const Point& Q, const mpz_class& p = modulo);
inline Point mul(const mpz_class& k, const Point& P = PG, const mpz_class& p = modulo);
inline Point point_subtraction(const Point& P, const Point& Q);
inline mpz_class X2Y(const mpz_class& X, int y_parity, const mpz_class& p = modulo);
inline string point_to_cpub(const Point& point);
inline string hash_cpub(const string& cpub);
void save_baby_table_part(const unordered_map<string, int>& baby_table, int part_num);
void save_baby_table(const unordered_map<string, int>& baby_table);
unordered_map<string, int> load_baby_table_part(const string& filename);
unordered_map<string, int> load_baby_table();
void delete_existing_table();
unordered_map<string, int> generate_baby_table_parallel(const mpz_class& m);

void print_help() {
    cout << "BSGS (Baby-Step Giant-Step) Elliptic Curve Cryptography Tool\n\n";
    cout << "Usage: ./bsgs [options]\n\n";
    cout << "Options:\n";
    cout << "  -p <number>     Puzzle number (default: 30)\n";
    cout << "  -k <pubkey>     Compressed public key in hex format\n";
    cout << "  -t <threads>    Number of CPU cores to use (default: all available)\n";
    cout << "  -v              Verbose output\n";
    cout << "  -h              Show this help message\n\n";
    cout << "Example:\n";
    cout << "  ./bsgs -p 30 -k 030d282cf2ff536d2c42f105d0b8588821a915dc3f9a05bd98bb23af67a2e92a5b -t 8\n";
}

bool validate_pubkey(const string& pubkey) {
    if (pubkey.length() != 66) {
        cerr << "[error] Public key must be 66 characters long (including 02/03 prefix)\n";
        return false;
    }
    
    if (pubkey[0] != '0' || (pubkey[1] != '2' && pubkey[1] != '3')) {
        cerr << "[error] Public key must start with 02 or 03\n";
        return false;
    }
    
    for (size_t i = 2; i < pubkey.length(); i++) {
        if (!isxdigit(pubkey[i])) {
            cerr << "[error] Public key contains invalid hex characters\n";
            return false;
        }
    }
    
    return true;
}

Point parse_pubkey(const string& pubkey) {
    string prefix = pubkey.substr(0, 2);
    mpz_class X(pubkey.substr(2), 16);
    int y_parity = stoi(prefix) - 2;
    mpz_class Y = X2Y(X, y_parity);
    
    if (Y == 0) {
        cerr << "[error] Invalid compressed public key!\n";
        exit(1);
    }
    
    return {X, Y};
}

inline Point add(const Point& P, const Point& Q, const mpz_class& p) {
    if (P == Z) return Q;
    if (Q == Z) return P;
    const mpz_class& P0 = P[0];
    const mpz_class& P1 = P[1];
    const mpz_class& Q0 = Q[0];
    const mpz_class& Q1 = Q[1];
    mpz_class lmbda, num, denom, inv;
    if (P != Q) {
        num = Q1 - P1;
        denom = Q0 - P0;
    } else {
        if (P1 == 0) return Z;
        num = 3 * P0 * P0;
        denom = 2 * P1;
    }
    mpz_invert(inv.get_mpz_t(), denom.get_mpz_t(), p.get_mpz_t());
    lmbda = (num * inv) % p;
    mpz_class x = (lmbda * lmbda - P0 - Q0) % p;
    if (x < 0) x += p;
    mpz_class y = (lmbda * (P0 - x) - P1) % p;
    if (y < 0) y += p;
    return {x, y};
}

inline Point mul(const mpz_class& k, const Point& P, const mpz_class& p) {
    Point R0 = Z, R1 = P;
    unsigned long bit_length = mpz_sizeinbase(k.get_mpz_t(), 2);
    for (unsigned long i = bit_length - 1; i < bit_length; --i) {
        if (mpz_tstbit(k.get_mpz_t(), i)) {
            R0 = add(R0, R1, p);
            R1 = add(R1, R1, p);
        } else {
            R1 = add(R0, R1, p);
            R0 = add(R0, R0, p);
        }
    }
    return R0;
}

inline Point point_subtraction(const Point& P, const Point& Q) {
    Point Q_neg = {Q[0], (-Q[1]) % modulo};
    return add(P, Q_neg);
}

inline mpz_class X2Y(const mpz_class& X, int y_parity, const mpz_class& p) {
    mpz_class X_cubed = (X * X * X) % p;
    mpz_class tmp = (X_cubed + mpz_class(7)) % p;
    mpz_class Y;
    mpz_class exp = (p + mpz_class(1)) / mpz_class(4);
    mpz_powm(Y.get_mpz_t(), tmp.get_mpz_t(), exp.get_mpz_t(), p.get_mpz_t());
    if ((Y % 2) != y_parity) {
        Y = p - Y;
    }
    return Y;
}

inline string point_to_cpub(const Point& point) {
    mpz_class x = point[0], y = point[1];
    int y_parity = y.get_ui() & 1;
    string prefix = y_parity == 0 ? "02" : "03";
    char buffer[65];
    mpz_get_str(buffer, 16, x.get_mpz_t());
    return prefix + string(buffer);
}

inline string hash_cpub(const string& cpub) {
    XXH64_hash_t hash = XXH64(cpub.c_str(), cpub.size(), 0);
    char buffer[17];
    snprintf(buffer, sizeof(buffer), "%016lx", hash);
    return string(buffer, 8);
}

void save_baby_table_part(const unordered_map<string, int>& baby_table, int part_num) {
    string filename = "baby_table_part_" + to_string(part_num);
    ofstream outfile(filename, ios::binary);
    if (!outfile) {
        cerr << "[error] Failed to open file for writing: " << filename << endl;
        return;
    }
    
    for (const auto& entry : baby_table) {
        outfile.write(entry.first.c_str(), 8);
        int index = entry.second;
        outfile.write(reinterpret_cast<const char*>(&index), sizeof(index));
    }
    
    if (verbose) {
        cout << "[+] Saved baby table part " << part_num << " with " << baby_table.size() << " entries" << endl;
    }
    
    string command = "pigz -9 -b 128 -f " + filename;
    FILE* pipe = popen(command.c_str(), "r");
    if (!pipe) {
        cerr << "[error] Failed to compress file: " << filename << endl;
        return;
    }
    pclose(pipe);
}

void save_baby_table(const unordered_map<string, int>& baby_table) {
    size_t current_size = 0;
    int part_num = 1;
    unordered_map<string, int> current_part;
    
    for (const auto& entry : baby_table) {
        size_t entry_size = entry.first.size() + sizeof(int);
        if (current_size + entry_size > MAX_TABLE_SIZE && !current_part.empty()) {
            save_baby_table_part(current_part, part_num++);
            current_part.clear();
            current_size = 0;
        }
        current_part.insert(entry);
        current_size += entry_size;
    }
    
    if (!current_part.empty()) {
        save_baby_table_part(current_part, part_num);
    }
}

unordered_map<string, int> load_baby_table_part(const string& filename) {
    string command = "pigz -d -c " + filename;
    FILE* pipe = popen(command.c_str(), "r");
    if (!pipe) {
        cerr << "[error] Failed to decompress file: " << filename << endl;
        return {};
    }
    
    unordered_map<string, int> baby_table_part;
    char key_buffer[9];
    int index;
    
    while (fread(key_buffer, 8, 1, pipe) == 1) {
        key_buffer[8] = '\0';
        fread(&index, sizeof(index), 1, pipe);
        baby_table_part[key_buffer] = index;
    }
    
    pclose(pipe);
    return baby_table_part;
}

unordered_map<string, int> load_baby_table() {
    unordered_map<string, int> baby_table;
    int part_num = 1;
    
    while (true) {
        string filename = "baby_table_part_" + to_string(part_num) + ".gz";
        ifstream test(filename);
        if (!test.good()) {
            if (part_num == 1) {
                cerr << "[error] No baby table parts found!" << endl;
                return {};
            }
            break;
        }
        test.close();
        
        string command = "pigz -d -c " + filename;
        FILE* pipe = popen(command.c_str(), "r");
        if (!pipe) {
            cerr << "[error] Failed to decompress file: " << filename << endl;
            return {};
        }
        
        char key[9] = {0};
        int index;
        size_t entries = 0;
        
        while (fread(key, 8, 1, pipe) == 1) {
            if (fread(&index, sizeof(int), 1, pipe) != 1) break;
            key[8] = '\0';
            baby_table[key] = index;
            entries++;
        }
        
        pclose(pipe);
        if (verbose) {
            cout << "[+] Loaded part " << part_num << " with " << entries << " entries" << endl;
        }
        part_num++;
    }
    
    cout << "[+] Loaded baby table with " << baby_table.size() << " total entries" << endl;
    return baby_table;
}

void delete_existing_table() {
    int part_num = 1;
    while (true) {
        string filename = "baby_table_part_" + to_string(part_num);
        string compressed_filename = filename + ".gz";
        
        struct stat buffer;
        bool found = false;
        
        if (stat(filename.c_str(), &buffer) == 0) {
            if (remove(filename.c_str()) != 0) {
                cerr << "[error] Failed to delete file: " << filename << endl;
            } else {
                found = true;
            }
        }
        
        if (stat(compressed_filename.c_str(), &buffer) == 0) {
            if (remove(compressed_filename.c_str()) != 0) {
                cerr << "[error] Failed to delete file: " << compressed_filename << endl;
            } else {
                found = true;
            }
        }
        
        if (!found) {
            if (part_num == 1 && verbose) {
                cout << "[+] No existing table parts found to delete" << endl;
            }
            break;
        }
        
        part_num++;
    }
}

unordered_map<string, int> generate_baby_table_parallel(const mpz_class& m) {
    const int num_threads = threads > 0 ? threads : omp_get_max_threads();
    const unsigned long total_entries = m.get_ui();
    const size_t max_part_size = MAX_TABLE_SIZE * 0.99; // 99% threshold
    std::atomic<int> parts_created(0);
    std::atomic<size_t> current_part_size(0);
    std::atomic<size_t> total_entries_written(0);
    
    system("rm -f baby_table_part_* baby_table_part_*.gz 2>/dev/null");
  
    cout << "[+] Generating " << total_entries << " baby steps" << endl;

    #pragma omp parallel num_threads(num_threads)
    {
        vector<pair<string, int>> local_buffer;
        local_buffer.reserve(100000);

        #pragma omp for schedule(dynamic, 100000)
        for (unsigned long i = 0; i < total_entries; ++i) {
            Point P = mul(i);
            string cpub_hash = hash_cpub(point_to_cpub(P));
            local_buffer.emplace_back(cpub_hash, i);

            if (local_buffer.size() >= 100000) {
                #pragma omp critical
                {
                    int current_part = parts_created + 1;
                    string filename = "baby_table_part_" + to_string(current_part);
                    ofstream outfile(filename, ios::binary | ios::app);
                    if (outfile) {
                        for (const auto& entry : local_buffer) {
                            outfile.write(entry.first.c_str(), 8);
                            outfile.write(reinterpret_cast<const char*>(&entry.second), sizeof(int));
                            total_entries_written++;
                            current_part_size += 12;
                            
                            if (current_part_size >= max_part_size) {
                                outfile.close();
                                string cmd = "pigz -9 -b 128 -f " + filename;
                                system(cmd.c_str());
                                parts_created++;
                                current_part_size = 0;
                            }
                        }
                    }
                    local_buffer.clear();
                }
            }
        }

        #pragma omp critical
        {
            if (!local_buffer.empty()) {
                int current_part = parts_created + 1;
                string filename = "baby_table_part_" + to_string(current_part);
                ofstream outfile(filename, ios::binary | ios::app);
                if (outfile) {
                    for (const auto& entry : local_buffer) {
                        outfile.write(entry.first.c_str(), 8);
                        outfile.write(reinterpret_cast<const char*>(&entry.second), sizeof(int));
                        total_entries_written++;
                    }
                    outfile.close();
                    string cmd = "pigz -9 -b 128 -f " + filename;
                    system(cmd.c_str());
                    parts_created++;
                }
            }
        }
    }

    cout << "[+] Generated " << parts_created << " compressed parts ("
         << total_entries_written << " total entries) " << endl;

    return {};
}

int main(int argc, char* argv[]) {
    // Parse command line arguments
    int opt;
    while ((opt = getopt(argc, argv, "p:k:t:vh")) != -1) {
        switch (opt) {
            case 'p':
                puzzle = atoi(optarg);
                if (puzzle < 1 || puzzle > 256) {
                    cerr << "[error] Invalid puzzle number (must be between 1-256)\n";
                    print_help();
                    return 1;
                }
                break;
            case 'k':
                puzzle_pubkey = optarg;
                if (!validate_pubkey(puzzle_pubkey)) {
                    print_help();
                    return 1;
                }
                break;
            case 't':
                threads = atoi(optarg);
                if (threads < 1) {
                    cerr << "[error] Thread count must be at least 1\n";
                    print_help();
                    return 1;
                }
                break;
            case 'v':
                verbose = true;
                break;
            case 'h':
            default:
                print_help();
                return 0;
        }
    }

    // Initialize OpenMP threads
    if (threads > 0) {
        omp_set_num_threads(threads);
    }
    int actual_threads = omp_get_max_threads();

    // Print configuration
    time_t currentTime = time(nullptr);
    cout << "\n\033[01;33m[+]\033[32m BSGS Started: \033[01;33m" << ctime(&currentTime);
    cout << "\033[0m[+] Puzzle: " << puzzle << endl;
    cout << "[+] Public Key: " << puzzle_pubkey << endl;
    cout << "[+] Using " << actual_threads << " CPU cores" << endl;

    // Initialize range and point
    mpz_class start_range = mpz_class(1) << (puzzle - 1);
    mpz_class end_range = (mpz_class(1) << puzzle) - 1;
    Point P = parse_pubkey(puzzle_pubkey);

    // Calculate m and generate baby table
    mpz_class m = sqrt(end_range - start_range);
    m = m * 4;
    Point m_P = mul(m);

    if (verbose) {
        cout << "[+] Range: 2^" << (puzzle-1) << " to 2^" << puzzle << "-1" << endl;
        cout << "[+] Baby-step count (m): " << m << endl;
    }

    delete_existing_table();

    cout << "[+] Generating baby table..." << endl;
    auto baby_table = generate_baby_table_parallel(m);

    cout << "[+] Loading baby table..." << endl;
    baby_table = load_baby_table();
    if (baby_table.empty()) {
        cerr << "[error] Failed to load baby table\n";
        return 1;
    }

    cout << "[+] Starting BSGS search..." << endl;
    Point S = point_subtraction(P, mul(start_range));
    mpz_class step = 0;
    bool found = false;
    mpz_class found_key;
    
    auto st = chrono::high_resolution_clock::now();
    
    #pragma omp parallel
    {
        Point local_S = S;
        mpz_class local_step = step;
        
        #pragma omp for schedule(dynamic)
        for (int i = 0; i < actual_threads; ++i) {
            while (local_step < (end_range - start_range)) {
                #pragma omp flush(found)
                if (found) break;
                
                string cpub = point_to_cpub(local_S);
                string cpub_hash = hash_cpub(cpub);
                auto it = baby_table.find(cpub_hash);
                if (it != baby_table.end()) {
                    int b = it->second;
                    mpz_class k = start_range + local_step + b;
                    if (point_to_cpub(mul(k)) == puzzle_pubkey) {
                        #pragma omp critical
                        {
                            if (!found) {
                                found = true;
                                found_key = k;
                                auto et = chrono::high_resolution_clock::now();
                                chrono::duration<double> elapsed = et - st;
                                
                                cout << "\n\033[01;32m[+] Solution found!\033[0m" << endl;
                                cout << "[+] Private key: " << k << endl;
                                cout << "[+] Hex: 0x" << hex << k << dec << endl;
                                cout << "[+] Time elapsed: " << elapsed.count() << " seconds\n";
                            }
                        }
                        #pragma omp flush(found)
                        break;
                    }
                }
                local_S = point_subtraction(local_S, m_P);
                local_step += m;
            }
        }
    }

    if (!found) {
        auto et = chrono::high_resolution_clock::now();
        chrono::duration<double> elapsed = et - st;
        cout << "\n\033[01;31m[!] Key not found in the specified range\033[0m" << endl;
        cout << "[+] Time elapsed: " << elapsed.count() << " seconds\n";
    }

    return 0;
}