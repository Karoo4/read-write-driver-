/*
    Example trainer - demonstrates finding and modifying values
    Usage: sudo ./trainer <process_name> <value_to_find>
*/

#include <iostream>
#include <iomanip>
#include <dirent.h>
#include "uma.h"
#include "scanner.h"

int main(int argc, char* argv[]) {
    if (argc < 3) {
        std::cerr << "Usage: " << argv[0] << " <process> <int_value>" << std::endl;
        std::cerr << "Example: " << argv[0] << " target 1337" << std::endl;
        return 1;
    }

    const char* processName = argv[1];
    int searchValue = std::stoi(argv[2]);

    try {
        Uma::Uma uma;
        uma.Attach(processName);

        std::cout << "Attached to: " << processName << std::endl;
        std::cout << "Scanning for value: " << searchValue << std::endl;

        // Get PID for scanning
        std::ifstream comm("/proc/self/comm"); // We need target PID
        pid_t pid = 0;

        // Find PID from /proc
        DIR* procDir = opendir("/proc");
        struct dirent* entry;
        while ((entry = readdir(procDir)) != nullptr) {
            char* endPtr;
            pid_t p = strtol(entry->d_name, &endPtr, 10);
            if (*endPtr != '\0') continue;

            std::ifstream cf("/proc/" + std::string(entry->d_name) + "/comm");
            std::string name;
            std::getline(cf, name);
            if (name == processName) {
                pid = p;
                break;
            }
        }
        closedir(procDir);

        if (!pid) {
            std::cerr << "Could not find PID" << std::endl;
            return 1;
        }

        std::cout << "Target PID: " << pid << std::endl;

        auto results = Uma::ScanValue<int>(uma, pid, searchValue);

        std::cout << "Found " << results.size() << " results:" << std::endl;
        for (size_t i = 0; i < results.size() && i < 20; i++) {
            std::cout << "  [" << i << "] 0x" << std::hex << results[i] << std::dec << std::endl;
        }

        if (!results.empty()) {
            std::cout << "\nEnter index to modify (or -1 to exit): ";
            int idx;
            std::cin >> idx;

            if (idx >= 0 && idx < (int)results.size()) {
                std::cout << "Enter new value: ";
                int newVal;
                std::cin >> newVal;

                uma.WriteMemory<int>(results[idx], newVal);
                std::cout << "Written " << newVal << " to 0x" << std::hex << results[idx] << std::endl;

                // Verify
                int verify = uma.ReadMemory<int>(results[idx]);
                std::cout << "Verified: " << std::dec << verify << std::endl;
            }
        }

    } catch (const std::exception& e) {
        std::cerr << e.what() << std::endl;
        return 1;
    }

    return 0;
}
