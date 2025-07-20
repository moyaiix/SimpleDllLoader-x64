#include <windows.h>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <vector>

#include "PeMapper.h"
#include "RemoteLoader.h"

std::vector<uint8_t> readFile(const std::filesystem::path &path) {
  std::ifstream file(path, std::ios::binary);
  if (!file) {
    std::printf("File could not be opened");
    return {};
  }
  uintmax_t length = std::filesystem::file_size(path);

  std::vector<uint8_t> fileBytes(length);

  file.read(reinterpret_cast<char *>(fileBytes.data()), length);
  return fileBytes;
}

// argv[1] = DLL path
// argv[2] = PID
int main(int argc, char *argv[]) {
  if (argc != 3) {
    return 0;
  }
  DWORD pid = static_cast<DWORD>(std::stoul(argv[2]));
  std::vector<uint8_t> fileBytes = readFile(argv[1]);

  auto header = reinterpret_cast<IMAGE_DOS_HEADER *>(fileBytes.data());
  if (header->e_magic != IMAGE_DOS_SIGNATURE) {
    std::printf("Not MZ, can not proceed");
    return 1;
  }

  IMAGE_NT_HEADERS64 *ntHeader = reinterpret_cast<IMAGE_NT_HEADERS64 *>(
      fileBytes.data() + header->e_lfanew);
  DWORD imageSizeToAllocate = ntHeader->OptionalHeader.SizeOfImage;

  std::vector<uint8_t> localImage(imageSizeToAllocate);

  copyHeadersAndSections(fileBytes, ntHeader, localImage);

  HANDLE procHandle = OpenProcess(PROCESS_ALL_ACCESS, false, pid);
  if (procHandle == nullptr) {
    std::printf("Open Process failed, exiting");
    return 1;
  }

  LPVOID allocatedMem =
      VirtualAllocEx(procHandle, nullptr, imageSizeToAllocate,
                     MEM_RESERVE | MEM_COMMIT, PAGE_EXECUTE_READWRITE);

  if (allocatedMem == nullptr) {
    std::printf("Failed to allocate memory in the target process, exiting");
    return 1;
  }

  uintptr_t imageBaseDelta = reinterpret_cast<uintptr_t>(allocatedMem) -
                             ntHeader->OptionalHeader.ImageBase;
  applyRelocations(localImage, ntHeader, imageBaseDelta);

  if (!WriteProcessMemory(procHandle, allocatedMem, localImage.data(),
                          localImage.size(), nullptr)) {
    std::printf("Failed to write local image into target proc, exiting");
    return 1;
  }

  if (!runRemoteLoader(procHandle, allocatedMem)) {
    std::printf("Failed to run remote loader");
    return 1;
  }

  return 0;
}
