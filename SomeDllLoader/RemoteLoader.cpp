#include "RemoteLoader.h"

using LoadLibraryAFn = HMODULE(WINAPI *)(LPCSTR);
using GetProcAddressFn = FARPROC(WINAPI *)(HMODULE, LPCSTR);
using DllMainFn = BOOL(WINAPI *)(HINSTANCE, DWORD, LPVOID);

struct RemoteLoaderData {
  BYTE *imageBase;
  LoadLibraryAFn loadLibraryA;
  GetProcAddressFn getProcAddress;
  BOOL success;
};

#pragma optimize("", off)
DWORD WINAPI remoteLoaderStub(RemoteLoaderData *loaderData) {
  BYTE *imageBase = loaderData->imageBase;
  IMAGE_DOS_HEADER *dosHeader = reinterpret_cast<IMAGE_DOS_HEADER *>(imageBase);
  IMAGE_NT_HEADERS64 *ntHeader =
      reinterpret_cast<IMAGE_NT_HEADERS64 *>(imageBase + dosHeader->e_lfanew);

  IMAGE_DATA_DIRECTORY importDataDir =
      ntHeader->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];

  if (importDataDir.VirtualAddress != 0 && importDataDir.Size != 0) {
    IMAGE_IMPORT_DESCRIPTOR *importDescriptor =
        reinterpret_cast<IMAGE_IMPORT_DESCRIPTOR *>(
            imageBase + importDataDir.VirtualAddress);

    while (importDescriptor->Name != 0) {
      char *dllName =
          reinterpret_cast<char *>(imageBase + importDescriptor->Name);
      HMODULE loadedDll = loaderData->loadLibraryA(dllName);

      if (loadedDll == nullptr) {
        loaderData->success = FALSE;
        return 1;
      }

      DWORD lookupThunkRva = importDescriptor->OriginalFirstThunk;
      if (lookupThunkRva == 0) {
        lookupThunkRva = importDescriptor->FirstThunk;
      }

      IMAGE_THUNK_DATA64 *lookupThunk =
          reinterpret_cast<IMAGE_THUNK_DATA64 *>(imageBase + lookupThunkRva);
      IMAGE_THUNK_DATA64 *addressThunk = reinterpret_cast<IMAGE_THUNK_DATA64 *>(
          imageBase + importDescriptor->FirstThunk);

      while (lookupThunk->u1.AddressOfData != 0) {
        FARPROC functionAddress = nullptr;

        if (IMAGE_SNAP_BY_ORDINAL64(lookupThunk->u1.Ordinal)) {
          WORD ordinal =
              static_cast<WORD>(IMAGE_ORDINAL64(lookupThunk->u1.Ordinal));
          functionAddress = loaderData->getProcAddress(
              loadedDll,
              reinterpret_cast<LPCSTR>(static_cast<ULONG_PTR>(ordinal)));
        } else {
          IMAGE_IMPORT_BY_NAME *importByName =
              reinterpret_cast<IMAGE_IMPORT_BY_NAME *>(
                  imageBase + lookupThunk->u1.AddressOfData);
          functionAddress = loaderData->getProcAddress(
              loadedDll, reinterpret_cast<LPCSTR>(importByName->Name));
        }

        if (functionAddress == nullptr) {
          loaderData->success = FALSE;
          return 1;
        }

        addressThunk->u1.Function =
            reinterpret_cast<ULONGLONG>(functionAddress);
        lookupThunk++;
        addressThunk++;
      }

      importDescriptor++;
    }
  }

  DWORD entryPointRva = ntHeader->OptionalHeader.AddressOfEntryPoint;
  if (entryPointRva != 0) {
    DllMainFn dllMain = reinterpret_cast<DllMainFn>(imageBase + entryPointRva);
    loaderData->success = dllMain(reinterpret_cast<HINSTANCE>(imageBase),
                                  DLL_PROCESS_ATTACH, nullptr);
    return loaderData->success ? 0 : 1;
  }

  loaderData->success = TRUE;
  return 0;
}
#pragma optimize("", on)

bool runRemoteLoader(HANDLE procHandle, LPVOID remoteImageBase) {
  RemoteLoaderData loaderData{.imageBase =
                                  reinterpret_cast<BYTE *>(remoteImageBase),
                              .loadLibraryA = LoadLibraryA,
                              .getProcAddress = GetProcAddress};

  // TODO: Clean up remote allocations on every failure path.
  SIZE_T loaderDataSize = sizeof(loaderData);
  LPVOID remoteLoaderData =
      VirtualAllocEx(procHandle, nullptr, loaderDataSize,
                     MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);

  if (remoteLoaderData == nullptr) {
    return false;
  }

  constexpr SIZE_T stubSize = 0x1000; // Hopefully the stub isnt bigger ^^
                                      // Otherwise we got a little problem

  LPVOID remoteStub =
      VirtualAllocEx(procHandle, nullptr, stubSize, MEM_RESERVE | MEM_COMMIT,
                     PAGE_EXECUTE_READWRITE);

  if (remoteStub == nullptr) {
    return false;
  }

  if (!WriteProcessMemory(procHandle, remoteLoaderData, &loaderData,
                          loaderDataSize, nullptr)) {
    return false;
  }

  if (!WriteProcessMemory(procHandle, remoteStub,
                          reinterpret_cast<LPCVOID>(remoteLoaderStub), stubSize,
                          nullptr)) {
    return false;
  }

  HANDLE remoteThread =
      CreateRemoteThread(procHandle, nullptr, 0,
                         reinterpret_cast<LPTHREAD_START_ROUTINE>(remoteStub),
                         remoteLoaderData, 0, nullptr);

  if (remoteThread == nullptr) {
    return false;
  }

  WaitForSingleObject(remoteThread, INFINITE);
  CloseHandle(remoteThread);

  if (!ReadProcessMemory(procHandle, remoteLoaderData, &loaderData,
                         loaderDataSize, nullptr)) {
    return false;
  }
  return loaderData.success == TRUE;
}