#pragma once

#include <Windows.h>

// Compatibility shim used only while compiling ElectronToolbox.cpp.
// It keeps the existing bridge/controller logic intact while ensuring that
// no named-pipe WriteFile call ever blocks Creo's UI thread.
BOOL WINAPI AvtElectronConnectNamedPipe(HANDLE pipe, LPOVERLAPPED overlapped);
BOOL WINAPI AvtElectronReadFile(HANDLE file,
                                LPVOID buffer,
                                DWORD bytesToRead,
                                LPDWORD bytesRead,
                                LPOVERLAPPED overlapped);
BOOL WINAPI AvtElectronWriteFile(HANDLE file,
                                 LPCVOID buffer,
                                 DWORD bytesToWrite,
                                 LPDWORD bytesWritten,
                                 LPOVERLAPPED overlapped);
