#pragma once

#include <windows.h>

#include <cstdint>
#include <vector>

void copyHeadersAndSections(const std::vector<uint8_t> &fileBytes,
                            IMAGE_NT_HEADERS64 *ntHeader,
                            std::vector<uint8_t> &localImage);

void applyRelocations(std::vector<uint8_t> &localImage,
                      IMAGE_NT_HEADERS64 *ntHeader, uintptr_t imageBaseDelta);