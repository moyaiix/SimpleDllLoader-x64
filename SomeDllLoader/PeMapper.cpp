#include <cstring>

#include "PeMapper.h"

void copyHeadersAndSections(const std::vector<uint8_t> &fileBytes,
                            IMAGE_NT_HEADERS64 *ntHeader,
                            std::vector<uint8_t> &localImage) {
  std::memcpy(localImage.data(), fileBytes.data(),
              ntHeader->OptionalHeader.SizeOfHeaders);

  IMAGE_SECTION_HEADER *sectionTable = IMAGE_FIRST_SECTION(ntHeader);

  for (WORD i{0}; i < ntHeader->FileHeader.NumberOfSections; i++) {
    IMAGE_SECTION_HEADER *imageSection = &sectionTable[i];
    const BYTE *sectionData = fileBytes.data() + imageSection->PointerToRawData;
    size_t sectionDataSize = imageSection->SizeOfRawData;

    BYTE *localTargetAddress = localImage.data() + imageSection->VirtualAddress;

    std::memcpy(localTargetAddress, sectionData, sectionDataSize);
  }
}

void applyRelocations(std::vector<uint8_t> &localImage,
                      IMAGE_NT_HEADERS64 *ntHeader, uintptr_t imageBaseDelta) {
  IMAGE_DATA_DIRECTORY relocDataDir =
      ntHeader->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_BASERELOC];

  BYTE *relocTable = localImage.data() + relocDataDir.VirtualAddress;
  DWORD offset = 0;

  while (offset < relocDataDir.Size) {
    IMAGE_BASE_RELOCATION *relocBlock =
        reinterpret_cast<IMAGE_BASE_RELOCATION *>(relocTable + offset);
    WORD *entries = reinterpret_cast<WORD *>(
        reinterpret_cast<BYTE *>(relocBlock) + sizeof(IMAGE_BASE_RELOCATION));

    size_t wordCount =
        (relocBlock->SizeOfBlock - sizeof(IMAGE_BASE_RELOCATION)) /
        sizeof(WORD);
    for (int i{0}; i < wordCount; i++) {
      WORD entry = entries[i];
      WORD type = entry >> 12;
      WORD entryOffset = entry & 0x0FFF;

      if (type != IMAGE_REL_BASED_DIR64) {
        continue;
      }
      DWORD patchRva = relocBlock->VirtualAddress + entryOffset;
      DWORD64 *valueToPatch =
          reinterpret_cast<DWORD64 *>(localImage.data() + patchRva);

      *valueToPatch += imageBaseDelta;
    }
    offset += relocBlock->SizeOfBlock;
  }
}
