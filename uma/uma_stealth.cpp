
#include "uma_stealth.h"
#include <dirent.h>
#include <fstream>
#include <sstream>

namespace Uma {

void UmaStealth::Attach(std::string_view name) {
  DIR *procDir = opendir("/proc");
  if (!procDir)
    throw std::runtime_error("Cannot open /proc");

  struct dirent *entry;
  while ((entry = readdir(procDir)) != nullptr) {
    if (entry->d_type != DT_DIR)
      continue;

    char *endPtr;
    pid_t pid = strtol(entry->d_name, &endPtr, 10);
    if (*endPtr != '\0')
      continue;

    std::ifstream commFile("/proc/" + std::string(entry->d_name) + "/comm");
    std::string processName;
    std::getline(commFile, processName);

    if (processName == name) {
      _processId = pid;
      break;
    }
  }
  closedir(procDir);

  if (_processId == 0)
    throw std::runtime_error("Process not found");

  kmd_attach_request req{};
  req.pid = _processId;

  if (ioctl(_fd, HIDDEN_ATTACH, &req) < 0)
    throw std::runtime_error("Attach failed - is driver loaded?");
}

std::uintptr_t UmaStealth::ReadModule(std::string_view name) const {
  std::ifstream maps("/proc/" + std::to_string(_processId) + "/maps");
  std::string line;

  while (std::getline(maps, line)) {
    if (line.find(name) != std::string::npos) {
      size_t dash = line.find('-');
      if (dash != std::string::npos) {
        return std::stoull(line.substr(0, dash), nullptr, 16);
      }
    }
  }
  return 0;
}

} // namespace Uma
