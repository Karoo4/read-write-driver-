/*
    Interactive memory scanner
*/

#include "uma.h"
#include <chrono>
#include <dirent.h>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

struct MemRegion {
  uintptr_t start, end;
  std::string perms, name;
};

std::vector<MemRegion> getRegions(pid_t pid) {
  std::vector<MemRegion> regions;
  std::ifstream maps("/proc/" + std::to_string(pid) + "/maps");
  std::string line;

  while (std::getline(maps, line)) {
    std::istringstream iss(line);
    std::string addr, perms, offset, dev, inode, name;
    iss >> addr >> perms >> offset >> dev >> inode;
    std::getline(iss, name);

    // Trim name
    size_t start = name.find_first_not_of(" \t");
    if (start != std::string::npos)
      name = name.substr(start);

    size_t dash = addr.find('-');
    MemRegion r;
    r.start = std::stoull(addr.substr(0, dash), nullptr, 16);
    r.end = std::stoull(addr.substr(dash + 1), nullptr, 16);
    r.perms = perms;
    r.name = name;
    regions.push_back(r);
  }
  return regions;
}

pid_t findPid(const std::string &name) {
  DIR *dir = opendir("/proc");
  dirent *entry;
  while ((entry = readdir(dir))) {
    char *end;
    pid_t pid = strtol(entry->d_name, &end, 10);
    if (*end)
      continue;

    std::ifstream f("/proc/" + std::string(entry->d_name) + "/comm");
    std::string comm;
    std::getline(f, comm);
    if (comm == name) {
      closedir(dir);
      return pid;
    }
  }
  closedir(dir);
  return 0;
}

template <typename T>
std::vector<uintptr_t> scanAll(Uma::Uma &uma, pid_t pid, T value) {
  std::vector<uintptr_t> results;
  auto regions = getRegions(pid);

  for (const auto &r : regions) {
    if (r.perms[0] != 'r' || r.perms[1] != 'w')
      continue;
    if (r.name.find("[vvar]") != std::string::npos)
      continue;
    if (r.name.find("[vsyscall]") != std::string::npos)
      continue;
    if (r.end - r.start > 50 * 1024 * 1024)
      continue;

    for (uintptr_t addr = r.start; addr + sizeof(T) <= r.end; addr += 4) {
      T val = uma.ReadMemory<T>(addr);
      if (val == value) {
        results.push_back(addr);
      }
    }
  }
  return results;
}

template <typename T>
std::vector<uintptr_t>
filterResults(Uma::Uma &uma, const std::vector<uintptr_t> &addrs, T value) {
  std::vector<uintptr_t> results;
  for (uintptr_t addr : addrs) {
    T val = uma.ReadMemory<T>(addr);
    if (val == value) {
      results.push_back(addr);
    }
  }
  return results;
}

int main(int argc, char *argv[]) {
  if (argc < 2) {
    std::cerr << "Usage: " << argv[0] << " <process_name>" << std::endl;
    return 1;
  }

  pid_t pid = findPid(argv[1]);
  if (!pid) {
    std::cerr << "Process not found: " << argv[1] << std::endl;
    return 1;
  }

  Uma::Uma uma;
  uma.Attach(argv[1]);

  uintptr_t base = uma.ReadModule(argv[1]);

  std::cout << "=== Memory Scanner ===" << std::endl;
  std::cout << "Process: " << argv[1] << " (PID: " << pid << ")" << std::endl;
  std::cout << "Base: 0x" << std::hex << base << std::dec << std::endl;
  std::cout << "\nCommands:" << std::endl;
  std::cout << "  scan <int>      - Initial scan for integer value"
            << std::endl;
  std::cout << "  scanf <float>   - Initial scan for float value" << std::endl;
  std::cout << "  filter <value>  - Filter existing results" << std::endl;
  std::cout << "  list            - Show current results" << std::endl;
  std::cout << "  write <idx> <val> - Write value to result index" << std::endl;
  std::cout << "  read <addr>     - Read int at address (hex)" << std::endl;
  std::cout << "  offset <idx>    - Show offset from base" << std::endl;
  std::cout << "  quit            - Exit" << std::endl;

  std::vector<uintptr_t> results;
  bool isFloat = false;

  std::string line;
  while (true) {
    std::cout << "\n[" << results.size() << " results] > ";
    if (!std::getline(std::cin, line))
      break;

    std::istringstream iss(line);
    std::string cmd;
    iss >> cmd;

    if (cmd == "quit" || cmd == "q") {
      break;
    } else if (cmd == "scan") {
      int value;
      iss >> value;
      std::cout << "Scanning for " << value << "..." << std::endl;
      auto start = std::chrono::steady_clock::now();
      results = scanAll<int>(uma, pid, value);
      auto end = std::chrono::steady_clock::now();
      auto ms =
          std::chrono::duration_cast<std::chrono::milliseconds>(end - start)
              .count();
      std::cout << "Found " << results.size() << " results (" << ms << "ms)"
                << std::endl;
      isFloat = false;
    } else if (cmd == "scanf") {
      float value;
      iss >> value;
      std::cout << "Scanning for " << value << "f..." << std::endl;
      results = scanAll<float>(uma, pid, value);
      std::cout << "Found " << results.size() << " results" << std::endl;
      isFloat = true;
    } else if (cmd == "filter" || cmd == "f") {
      if (results.empty()) {
        std::cout << "No results to filter. Run scan first." << std::endl;
        continue;
      }
      if (isFloat) {
        float value;
        iss >> value;
        results = filterResults<float>(uma, results, value);
      } else {
        int value;
        iss >> value;
        results = filterResults<int>(uma, results, value);
      }
      std::cout << "Filtered to " << results.size() << " results" << std::endl;
    } else if (cmd == "list" || cmd == "l") {
      size_t count = std::min(results.size(), (size_t)20);
      for (size_t i = 0; i < count; i++) {
        uintptr_t addr = results[i];
        std::cout << "[" << std::setw(3) << i << "] 0x" << std::hex << addr;
        if (addr >= base) {
          std::cout << "  (base + 0x" << (addr - base) << ")";
        }
        std::cout << " = ";
        if (isFloat) {
          std::cout << std::dec << uma.ReadMemory<float>(addr) << "f";
        } else {
          std::cout << std::dec << uma.ReadMemory<int>(addr);
        }
        std::cout << std::endl;
      }
      if (results.size() > 20) {
        std::cout << "... and " << (results.size() - 20) << " more"
                  << std::endl;
      }
    } else if (cmd == "write" || cmd == "w") {
      int idx;
      iss >> idx;
      if (idx < 0 || idx >= (int)results.size()) {
        std::cout << "Invalid index" << std::endl;
        continue;
      }
      if (isFloat) {
        float val;
        iss >> val;
        uma.WriteMemory<float>(results[idx], val);
        std::cout << "Wrote " << val << "f to 0x" << std::hex << results[idx]
                  << std::dec << std::endl;
      } else {
        int val;
        iss >> val;
        uma.WriteMemory<int>(results[idx], val);
        std::cout << "Wrote " << val << " to 0x" << std::hex << results[idx]
                  << std::dec << std::endl;
      }
    } else if (cmd == "read" || cmd == "r") {
      std::string addrStr;
      iss >> addrStr;
      uintptr_t addr = std::stoull(addrStr, nullptr, 16);
      std::cout << "int:   " << uma.ReadMemory<int>(addr) << std::endl;
      std::cout << "float: " << uma.ReadMemory<float>(addr) << std::endl;
      std::cout << "ptr:   0x" << std::hex << uma.ReadMemory<uintptr_t>(addr)
                << std::dec << std::endl;
    } else if (cmd == "offset" || cmd == "o") {
      int idx;
      iss >> idx;
      if (idx >= 0 && idx < (int)results.size()) {
        std::cout << "Offset from base: 0x" << std::hex << (results[idx] - base)
                  << std::dec << std::endl;
      }
    } else if (!cmd.empty()) {
      std::cout << "Unknown command: " << cmd << std::endl;
    }
  }

  return 0;
}
