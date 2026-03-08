
#include "uma.h"

#include <algorithm>
#include <cstring>
#include <dirent.h>
#include <fstream>
#include <sstream>

namespace Uma {

Uma::Uma() : _kmdFd(-1), _processId(0) {
  _kmdFd = open(KMD_DEVICE_PATH, O_RDWR);
  if (_kmdFd < 0)
    throw std::runtime_error(
        "Fatal, failed to connect to driver. Make sure it is loaded.");
}

Uma::~Uma() {
  if (_kmdFd >= 0)
    close(_kmdFd);
}

void Uma::Attach(std::string_view name) {
  DIR *procDir = opendir("/proc");
  if (!procDir)
    throw std::runtime_error("Fatal, failed to open /proc");

  struct dirent *entry;
  while ((entry = readdir(procDir)) != nullptr) {
    if (entry->d_type != DT_DIR)
      continue;

    char *endPtr;
    pid_t pid = strtol(entry->d_name, &endPtr, 10);
    if (*endPtr != '\0')
      continue;

    std::string commPath = "/proc/" + std::string(entry->d_name) + "/comm";
    std::ifstream commFile(commPath);
    if (!commFile.is_open())
      continue;

    std::string processName;
    std::getline(commFile, processName);

    if (processName == name) {
      _processId = pid;
      break;
    }
  }

  closedir(procDir);

  if (_processId == 0)
    throw std::runtime_error(
        "Fatal, process not found. Check if process is available.");

  kmd_attach_request req{};
  req.pid = _processId;

  if (ioctl(_kmdFd, KMD_IOCTL_ATTACH, &req) < 0)
    throw std::runtime_error("Fatal, driver attach request failed.");
}

std::uintptr_t Uma::ReadModule(std::string_view name) const {
  std::string mapsPath = "/proc/" + std::to_string(_processId) + "/maps";
  std::ifstream mapsFile(mapsPath);

  if (!mapsFile.is_open())
    return 0;

  std::string line;
  while (std::getline(mapsFile, line)) {
    if (line.find(name) != std::string::npos) {
      std::uintptr_t baseAddr;
      std::istringstream iss(line);
      std::string addrRange;
      iss >> addrRange;

      size_t dashPos = addrRange.find('-');
      if (dashPos != std::string::npos) {
        std::string startAddr = addrRange.substr(0, dashPos);
        baseAddr = std::stoull(startAddr, nullptr, 16);
        return baseAddr;
      }
    }
  }

  return 0;
}

} // namespace Uma
