
#ifndef UMA_STEALTH_H
#define UMA_STEALTH_H

#include <cstdint>
#include <fcntl.h>
#include <stdexcept>
#include <string_view>
#include <sys/ioctl.h>
#include <unistd.h>

// Hidden IOCTL codes matching kmd_hijack.c
#define HIDDEN_MAGIC 0xDE

struct kmd_attach_request {
  pid_t pid;
};

struct kmd_copy_request {
  unsigned long from;
  unsigned long to;
  size_t size;
};

#define HIDDEN_ATTACH _IOW(HIDDEN_MAGIC, 0x01, struct kmd_attach_request)
#define HIDDEN_READ _IOWR(HIDDEN_MAGIC, 0x02, struct kmd_copy_request)
#define HIDDEN_WRITE _IOW(HIDDEN_MAGIC, 0x03, struct kmd_copy_request)

namespace Uma {

class UmaStealth {
private:
  int _fd;
  pid_t _processId;

public:
  UmaStealth() : _fd(-1), _processId(0) {
    // Open /dev/null - looks completely innocent
    _fd = open("/dev/null", O_RDWR);
    if (_fd < 0)
      throw std::runtime_error("Cannot open /dev/null");
  }

  ~UmaStealth() {
    if (_fd >= 0)
      close(_fd);
  }

  bool isDriverLoaded() {
    // Test if our hidden handler is installed
    kmd_attach_request req{};
    req.pid = getpid();
    return ioctl(_fd, HIDDEN_ATTACH, &req) == 0;
  }

  void Attach(std::string_view name);
  std::uintptr_t ReadModule(std::string_view name) const;

  template <typename T> T ReadMemory(std::uintptr_t address) {
    T result{};
    kmd_copy_request req{};
    req.from = address;
    req.to = reinterpret_cast<unsigned long>(&result);
    req.size = sizeof(T);

    if (ioctl(_fd, HIDDEN_READ, &req) < 0)
      return T{};
    return result;
  }

  template <typename T>
  void WriteMemory(std::uintptr_t address, const T &value) {
    kmd_copy_request req{};
    req.from = address;
    req.to = reinterpret_cast<unsigned long>(&value);
    req.size = sizeof(T);
    ioctl(_fd, HIDDEN_WRITE, &req);
  }

  pid_t getPid() const { return _processId; }
};

} // namespace Uma

#endif
