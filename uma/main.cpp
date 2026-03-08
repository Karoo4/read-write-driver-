#include "uma.h"
#include <cstring>
#include <iomanip>
#include <iostream>

int main(int argc, char *argv[]) {
  if (argc < 2) {
    std::cerr << "Usage: " << argv[0] << " <process_name> [module_name]"
              << std::endl;
    std::cerr << "Example: " << argv[0] << " firefox libxul.so" << std::endl;
    return 1;
  }

  const char *processName = argv[1];
  const char *moduleName = argc > 2 ? argv[2] : nullptr;

  Uma::Uma *uma = nullptr;

  try {
    uma = new Uma::Uma();
    uma->Attach(processName);

    std::cout << "Attached to process: " << processName << std::endl;

    if (moduleName) {
      std::uintptr_t moduleBase = uma->ReadModule(moduleName);
      if (moduleBase) {
        std::cout << "Module " << moduleName << " base: 0x" << std::hex
                  << moduleBase << std::endl;

        // Read first 64 bytes (ELF header)
        std::cout << "\nFirst 64 bytes at module base:" << std::endl;
        for (int i = 0; i < 64; i += 16) {
          std::cout << std::hex << std::setw(16) << std::setfill('0')
                    << (moduleBase + i) << ": ";

          for (int j = 0; j < 16; j++) {
            uint8_t byte = uma->ReadMemory<uint8_t>(moduleBase + i + j);
            std::cout << std::setw(2) << std::setfill('0')
                      << static_cast<int>(byte) << " ";
          }

          std::cout << " | ";
          for (int j = 0; j < 16; j++) {
            uint8_t byte = uma->ReadMemory<uint8_t>(moduleBase + i + j);
            char c = (byte >= 32 && byte < 127) ? static_cast<char>(byte) : '.';
            std::cout << c;
          }
          std::cout << std::endl;
        }
      } else {
        std::cerr << "Module not found: " << moduleName << std::endl;
      }
    }
  } catch (const std::exception &e) {
    std::cerr << e.what() << std::endl;
    delete uma;
    return 1;
  }

  delete uma;
  return 0;
}
