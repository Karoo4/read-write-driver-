/*
    File scanner.h
*/

#ifndef SCANNER_H
#define SCANNER_H

#include "uma.h"
#include <cstdint>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

namespace Uma {

struct MemoryRegion {
  uintptr_t start;
  uintptr_t end;
  std::string perms;
  std::string name;
};

inline std::vector<MemoryRegion> GetMemoryRegions(pid_t pid) {
  std::vector<MemoryRegion> regions;
  std::ifstream maps("/proc/" + std::to_string(pid) + "/maps");
  std::string line;

  while (std::getline(maps, line)) {
    MemoryRegion region;
    std::istringstream iss(line);
    std::string addrRange;
    iss >> addrRange >> region.perms;

    size_t dash = addrRange.find('-');
    region.start = std::stoull(addrRange.substr(0, dash), nullptr, 16);
    region.end = std::stoull(addrRange.substr(dash + 1), nullptr, 16);

    // Get name (last field)
    size_t lastSpace = line.rfind(' ');
    if (lastSpace != std::string::npos && lastSpace + 1 < line.size()) {
      region.name = line.substr(lastSpace + 1);
    }

    regions.push_back(region);
  }

  return regions;
}

template <typename T>
std::vector<uintptr_t> ScanValue(Uma &uma, pid_t pid, T value) {
  std::vector<uintptr_t> results;
  auto regions = GetMemoryRegions(pid);

  for (const auto &region : regions) {
    // Only scan readable, writable regions (heap, stack, data)
    if (region.perms.find('r') == std::string::npos)
      continue;
    if (region.perms.find('w') == std::string::npos)
      continue;

    // Skip non-writable or special regions
    if (region.name.find("[vvar]") != std::string::npos)
      continue;
    if (region.name.find("[vsyscall]") != std::string::npos)
      continue;

    size_t regionSize = region.end - region.start;
    if (regionSize > 100 * 1024 * 1024) // Skip regions > 100MB
      continue;

    for (uintptr_t addr = region.start; addr < region.end - sizeof(T);
         addr += sizeof(T)) {
      T read = uma.ReadMemory<T>(addr);
      if (read == value) {
        results.push_back(addr);
      }
    }
  }

  return results;
}

} // namespace Uma

#endif /* !SCANNER_H */
