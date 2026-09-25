#pragma once
#include <windows.h>
#include <objbase.h>
#include <propsys.h>

namespace drift::apo {
// Versioned separately from the cable app. Change for an incompatible APO release.
inline constexpr GUID classId{
    0x9d91f96a, 0xe57f, 0x4e78, {0xa2, 0x68, 0x14, 0x36, 0x93, 0xe0, 0x9e, 0x66}};
inline constexpr wchar_t classIdText[] = L"{9D91F96A-E57F-4E78-A268-143693E09E66}";
inline constexpr PROPERTYKEY initialSettingsKey{classId, 1};
inline constexpr wchar_t settingsRoot[] = L"SOFTWARE\\DriftEQ\\APO\\Devices\\";
} // namespace drift::apo
