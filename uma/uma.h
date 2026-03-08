#ifndef UMA_H
#define UMA_H

#include <cstdint>
#include <fcntl.h>
#include <stdexcept>
#include <string_view>
#include <sys/ioctl.h>
#include <unistd.h>

#include "../kmd/kmd.h"

namespace Uma {

class Uma {
private:
  int _kmdFd;
  pid_t _processId;

public:
  Uma();
  ~Uma();

  Uma(const Uma &) = delete;
  Uma &operator=(const Uma &) = delete;

  void Attach(std::string_view name);

  std::uintptr_t ReadModule(std::string_view name) const;

  template <typename T> T ReadMemory(std::uintptr_t address);

  template <typename T>
  void WriteMemory(std::uintptr_t address, const T &value);
};

template <typename T> T Uma::ReadMemory(std::uintptr_t address) {
  T result{};

  kmd_copy_request req{};
  req.from = address;
  req.to = reinterpret_cast<unsigned long>(&result);
  req.size = sizeof(T);

  if (ioctl(_kmdFd, KMD_IOCTL_READ, &req) < 0)
    return T{};

  return result;
}

template <typename T>
void Uma::WriteMemory(std::uintptr_t address, const T &value) {
  kmd_copy_request req{};
  req.from = address;
  req.to = reinterpret_cast<unsigned long>(&value);
  req.size = sizeof(T);

  ioctl(_kmdFd, KMD_IOCTL_WRITE, &req);
}

} // namespace Uma

#endif /* !UMA_H */
