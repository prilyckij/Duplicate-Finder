// find_duplicates.cpp
#include <iostream>
#include <string>
#include <vector>
#include <map>
#include <filesystem>
#include <fstream>
#include <openssl/md5.h>
#include <openssl/sha.h>
#include <iomanip>
#include <sstream>
#include <algorithm>
#include <fnmatch.h>

using namespace std;
namespace fs = std::filesystem;

const string RESET = "\033[0m";
const string GREEN = "\033[92m";
const string RED = "\033[91m";
const string YELLOW = "\033[93m";
const string BLUE = "\033[94m";

string colorize(const string& text, const string& color) {
    return color + text + RESET;
}

string getFileHash(const string& path, const string& algo, size_t partial) {
    ifstream f(path, ios::binary);
    if (!f) return "";
    unsigned char hash[MD5_DIGEST_LENGTH];
    if (algo == "md5") {
        MD5_CTX ctx;
        MD5_Init(&ctx);
        char buffer[8192];
        size_t total = 0;
        while (f.good() && (partial == 0 || total < partial)) {
            size_t toRead = (partial > 0) ? min<size_t>(8192, partial - total) : 8192;
            f.read(buffer, toRead);
            size_t read = f.gcount();
            if (read == 0) break;
            MD5_Update(&ctx, buffer, read);
            total += read;
        }
        MD5_Final(hash, &ctx);
    } else if (algo == "sha1") {
        SHA_CTX ctx;
        SHA1_Init(&ctx);
        char buffer[8192];
        size_t total = 0;
        while (f.good() && (partial == 0 || total < partial)) {
            size_t toRead = (partial > 0) ? min<size_t>(8192, partial - total) : 8192;
            f.read(buffer, toRead);
            size_t read = f.gcount();
            if (read == 0) break;
            SHA1_Update(&ctx, buffer, read);
            total += read;
        }
        SHA1_Final(hash, &ctx);
    } else {
        return "";
    }
    stringstream ss;
    for (int i = 0; i < (algo == "md5" ? MD5_DIGEST_LENGTH : SHA_DIGEST_LENGTH); ++i)
        ss << hex << setw(2) << setfill('0') << (int)hash[i];
    return ss.str();
}

void findDuplicates(const fs::path& root, bool recursive, const string& algo, size_t partial,
                    const vector<string>& excludePatterns,
                    map<string, vector<vector<fs::path>>>& duplicates) {
    map<uintmax_t, vector<fs::path>> sizeMap;
    function<void(const fs::path&)> walk = [&](const fs::path& dir) {
        for (const auto& entry : fs::directory_iterator(dir)) {
            if (entry.is_directory()) {
                if (!recursive && entry.path() != root) continue;
                // Исключения
                bool skip = false;
                for (auto& pat : excludePatterns) {
                    if (fnmatch(pat.c_str(), entry.path().filename().c_str(), 0) == 0) {
                        skip = true;
                        break;
                    }
                }
                if (skip) continue;
                walk(entry.path());
            } else if (entry.is_regular_file()) {
                // Исключение файлов
                bool skip = false;
                for (auto& pat : excludePatterns) {
                    if (fnmatch(pat.c_str(), entry.path().filename().c_str(), 0) == 0) {
                        skip = true;
                        break;
                    }
                }
                if (skip) continue;
                auto size = entry.file_size();
                if (size > 0) {
                    sizeMap[size].push_back(entry.path());
                }
            }
        }
    };
    walk(root);

    for (auto& [size, files] : sizeMap) {
        if (files.size() < 2) continue;
        map<string, vector<fs::path>> hashMap;
        for (auto& f : files) {
            string h = getFileHash(f.string(), algo, partial);
            if (!h.empty()) hashMap[h].push_back(f);
        }
        for (auto& [h, group] : hashMap) {
            if (group.size() > 1) {
                duplicates[h].push_back(group);
            }
        }
    }
}

string formatSize(uintmax_t size) {
    const char* units[] = {"B", "KB", "MB", "GB", "TB"};
    double s = size;
    int i = 0;
    while (s >= 1024 && i < 4) { s /= 1024; ++i; }
    stringstream ss;
    ss << fixed << setprecision(1) << s << " " << units[i];
    return ss.str();
}

void printDuplicates(const map<string, vector<vector<fs::path>>>& duplicates, bool verbose) {
    if (duplicates.empty()) {
        cout << colorize("Дубликатов не найдено.", GREEN) << endl;
        return;
    }
    int totalGroups = 0, totalFiles = 0;
    uintmax_t totalSize = 0;
    for (auto& [h, groups] : duplicates) {
        for (auto& group : groups) {
            ++totalGroups;
            totalFiles += group.size();
            auto size = fs::file_size(group[0]);
            totalSize += size * (group.size() - 1);
            cout << colorize("\nГруппа дубликатов (размер: " + formatSize(size) + "):", BLUE) << endl;
            cout << "  Хеш: " << h << endl;
            for (size_t i = 0; i < group.size(); ++i) {
                if (i == 0)
                    cout << colorize("  [оригинал] " + group[i].string(), GREEN) << endl;
                else
                    cout << colorize("  [дубликат] " + group[i].string(), YELLOW) << endl;
            }
            if (verbose) cout << "  Количество файлов: " << group.size() << endl;
        }
    }
    cout << colorize("\nВсего групп: " + to_string(totalGroups) + ", файлов: " + to_string(totalFiles) +
                     ", экономия: " + formatSize(totalSize), GREEN) << endl;
}

int main(int argc, char* argv[]) {
    string root = ".";
    bool recursive = true;
    string algo = "md5";
    size_t partial = 0;
    vector<string> excludePatterns;
    bool deleteDups = false, hardlink = false, dryRun = false, verbose = false, yes = false;
    string moveTo, logFile;

    for (int i = 1; i < argc; ++i) {
        string arg = argv[i];
        if (arg == "-p" && i+1 < argc) root = argv[++i];
        else if (arg == "-r") recursive = true;
        else if (arg == "-a" && i+1 < argc) algo = argv[++i];
        else if (arg == "--partial" && i+1 < argc) partial = stoull(argv[++i]);
        else if (arg == "-e" && i+1 < argc) {
            string pat = argv[++i];
            stringstream ss(pat);
            string item;
            while (getline(ss, item, ',')) excludePatterns.push_back(item);
        }
        else if (arg == "--delete") deleteDups = true;
        else if (arg == "--move" && i+1 < argc) moveTo = argv[++i];
        else if (arg == "--hardlink") hardlink = true;
        else if (arg == "-n") dryRun = true;
        else if (arg == "-l" && i+1 < argc) logFile = argv[++i];
        else if (arg == "-v") verbose = true;
        else if (arg == "-y") yes = true;
        else if (arg == "-h") {
            cout << "Usage: find_duplicates [options]\n";
            return 0;
        }
    }

    fs::path rootPath = fs::absolute(root);
    if (!fs::exists(rootPath) || !fs::is_directory(rootPath)) {
        cerr << colorize("Ошибка: '" + root + "' не является папкой", RED) << endl;
        return 1;
    }

    map<string, vector<vector<fs::path>>> duplicates;
    findDuplicates(rootPath, recursive, algo, partial, excludePatterns, duplicates);
    printDuplicates(duplicates, verbose);

    // Действия
    if (deleteDups || !moveTo.empty() || hardlink) {
        if (!yes) {
            cout << colorize("Выполнить действия над дубликатами? [y/N] ", YELLOW);
            string ans;
            getline(cin, ans);
            if (ans != "y" && ans != "Y") {
                cout << colorize("Операция отменена.", RED) << endl;
                return 0;
            }
        }
        for (auto& [h, groups] : duplicates) {
            for (auto& group : groups) {
                auto original = group[0];
                for (size_t i = 1; i < group.size(); ++i) {
                    auto dup = group[i];
                    if (dryRun) {
                        cout << colorize("[DRY RUN] Будет обработан дубликат: " + dup.string(), YELLOW) << endl;
                        continue;
                    }
                    if (deleteDups) {
                        fs::remove(dup);
                        cout << colorize("Удалён: " + dup.string(), RED) << endl;
                    } else if (!moveTo.empty()) {
                        fs::create_directories(moveTo);
                        auto dest = fs::path(moveTo) / dup.filename();
                        fs::rename(dup, dest);
                        cout << colorize("Перемещён: " + dup.string() + " -> " + dest.string(), YELLOW) << endl;
                    } else if (hardlink) {
                        fs::remove(dup);
                        fs::create_hard_link(original, dup);
                        cout << colorize("Создана жёсткая ссылка: " + dup.string() + " -> " + original.string(), BLUE) << endl;
                    }
                }
            }
        }
    }
    return 0;
}
