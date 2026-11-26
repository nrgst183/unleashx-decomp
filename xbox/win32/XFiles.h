// The Xbox's drives as folders on the PC (XFiles.cpp), for the rest of
// xbox/win32.
#pragma once

#include <string>
#include <vector>

// An Xbox path (E:\..., \Device\Harddisk0\Partition1\..., \??\E:\..., or one
// relative to D:) as a path on the PC.
std::string UxMapPath(const char *xboxPath);
// The PC folder of an Xbox drive letter.
std::string UxDriveFolder(char letter);
// A whole file, by its Xbox path.
bool UxReadFile(const char *xboxPath, std::vector<unsigned char> &data);
// Points a drive letter at a kernel device (IoCreateSymbolicLink), and back.
void UxSetDriveDevice(char letter, const char *device);
std::string UxDriveDevice(char letter);
// A kernel device the Windows build has (a partition or the DVD drive).
bool UxDeviceExists(const char *device);
// Where UnleashX itself lives on the Xbox (its XBE), E:\Apps\UnleashX.
#define UX_DASH_FOLDER "E:\\Apps\\UnleashX"
#define UX_DASH_DEVICE_PATH "\\Device\\Harddisk0\\Partition1\\Apps\\UnleashX\\default.xbe"
