#include <windows.h>
#include <initguid.h>
#include <mmdeviceapi.h>
#include <functiondiscoverykeys_devpkey.h>
#include <commctrl.h>
#include <shellapi.h>
#include <wrl/client.h>
#include <fstream>
#include <filesystem>
#include <vector>
#include "Settings.h"
using Microsoft::WRL::ComPtr;
namespace {
struct Device {
    std::wstring id, guid, name;
};
std::vector<Device> devices;
HWND window{}, picker{}, preset{}, depth{}, pace{}, volume{}, depthText{}, paceText{}, volumeText{},
    toggle{}, status{};
HFONT font{}, titleFont{};
NOTIFYICONDATAW tray{};
drift::Parameters parameters;
constexpr int deviceId = 100, presetId = 101, toggleId = 102, refreshId = 103, helpId = 104,
              quitId = 105;
std::vector<Device> outputs(std::wstring &defaultId) {
    std::vector<Device> result;
    ComPtr<IMMDeviceEnumerator> e;
    if (FAILED(
            CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL, IID_PPV_ARGS(&e))))
        return result;
    ComPtr<IMMDevice> chosen;
    if (SUCCEEDED(e->GetDefaultAudioEndpoint(eRender, eConsole, &chosen))) {
        LPWSTR id = nullptr;
        if (SUCCEEDED(chosen->GetId(&id))) {
            defaultId = id;
            CoTaskMemFree(id);
        }
    }
    ComPtr<IMMDeviceCollection> list;
    if (FAILED(e->EnumAudioEndpoints(eRender, DEVICE_STATE_ACTIVE, &list)))
        return result;
    UINT count = 0;
    list->GetCount(&count);
    for (UINT i = 0; i < count; ++i) {
        ComPtr<IMMDevice> d;
        ComPtr<IPropertyStore> p;
        if (FAILED(list->Item(i, &d)) || FAILED(d->OpenPropertyStore(STGM_READ, &p)))
            continue;
        Device value;
        LPWSTR id = nullptr;
        if (FAILED(d->GetId(&id)))
            continue;
        value.id = id;
        CoTaskMemFree(id);
        PROPVARIANT v;
        PropVariantInit(&v);
        if (SUCCEEDED(p->GetValue(PKEY_Device_FriendlyName, &v)) && v.vt == VT_LPWSTR)
            value.name = v.pwszVal;
        PropVariantClear(&v);
        if (SUCCEEDED(p->GetValue(PKEY_AudioEndpoint_GUID, &v)) && v.vt == VT_LPWSTR)
            value.guid = drift::apo::normalizeGuid(v.pwszVal);
        PropVariantClear(&v);
        if (!value.guid.empty())
            result.push_back(value);
    }
    return result;
}
const Device *selected() {
    const auto i = SendMessageW(picker, CB_GETCURSEL, 0, 0);
    return i >= 0 && std::size_t(i) < devices.size() ? &devices[std::size_t(i)] : nullptr;
}
void labels() {
    wchar_t text[160];
    swprintf_s(text, L"Maximum EQ movement: +/- %.1f dB", parameters.depth);
    SetWindowTextW(depthText, text);
    swprintf_s(text, L"%s: %.1f seconds",
               parameters.pulse ? L"Time per tonal pulse" : L"Typical time between targets",
               parameters.seconds);
    SetWindowTextW(paceText, text);
    swprintf_s(text, L"Processed output level: %.0f%%", parameters.volume * 100);
    SetWindowTextW(volumeText, text);
    SetWindowTextW(toggle, parameters.enabled ? L"Disable effect" : L"Enable effect");
}
void sliders() {
    SendMessageW(depth, TBM_SETPOS, TRUE, LPARAM(parameters.depth * 10));
    SendMessageW(pace, TBM_SETPOS, TRUE, LPARAM(parameters.seconds * 10));
    SendMessageW(volume, TBM_SETPOS, TRUE, LPARAM(parameters.volume * 100));
    labels();
}
void load() {
    std::uint32_t word = 0;
    const auto *d = selected();
    const bool installed = d && drift::apo::readSettings(d->guid, word);
    parameters = drift::apo::unpack(word);
    sliders();
    EnableWindow(toggle, installed);
    EnableWindow(preset, installed);
    EnableWindow(depth, installed);
    EnableWindow(pace, installed);
    EnableWindow(volume, installed);
    SendMessageW(preset, CB_SETCURSEL, WPARAM(-1), 0);
    SetWindowTextW(status,
                   installed
                       ? L"Settings loaded. Windows audio enhancements must be enabled. Settings "
                         L"alone do not confirm that Windows loaded the effect."
                       : L"This device has not been set up for Drift APO. Open the setup guide. "
                         L"The unsigned preview cannot be installed into the system audio engine.");
}
void save() {
    const auto *d = selected();
    if (d && drift::apo::writeSettings(d->guid, drift::apo::pack(parameters)))
        SetWindowTextW(status, parameters.enabled
                                   ? L"Effect enabled in saved settings. An installed, loaded APO "
                                     L"applies changes within about 100 ms."
                                   : L"Effect disabled. The loaded APO fades back to unchanged "
                                     L"audio within about 150 ms.");
    else
        SetWindowTextW(status, L"Settings could not be saved. Setup must grant access to this "
                               L"device's Drift settings.");
    labels();
}
void refresh() {
    std::wstring previous, defaultId;
    if (const auto *d = selected())
        previous = d->id;
    devices = outputs(defaultId);
    SendMessageW(picker, CB_RESETCONTENT, 0, 0);
    int chosen = 0;
    for (std::size_t i = 0; i < devices.size(); ++i) {
        SendMessageW(picker, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(devices[i].name.c_str()));
        if (devices[i].id == (previous.empty() ? defaultId : previous))
            chosen = int(i);
    }
    SendMessageW(picker, CB_SETCURSEL, chosen, 0);
    load();
}
HWND add(const wchar_t *cls, const wchar_t *text, DWORD style, int x, int y, int w, int h,
         int id = 0) {
    auto child = CreateWindowW(cls, text, WS_CHILD | WS_VISIBLE | style, x, y, w, h, window,
                               reinterpret_cast<HMENU>(INT_PTR(id)), nullptr, nullptr);
    SendMessageW(child, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
    return child;
}
void show() {
    ShowWindow(window, SW_SHOW);
    SetForegroundWindow(window);
}
LRESULT CALLBACK proc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_COMMAND:
        switch (LOWORD(wp)) {
        case deviceId:
            if (HIWORD(wp) == CBN_SELCHANGE)
                load();
            break;
        case presetId:
            if (HIWORD(wp) == CBN_SELCHANGE) {
                const auto prior = parameters;
                parameters = drift::preset(unsigned(SendMessageW(preset, CB_GETCURSEL, 0, 0)));
                parameters.volume = prior.volume;
                parameters.enabled = prior.enabled;
                sliders();
                save();
            }
            break;
        case toggleId:
            parameters.enabled = !parameters.enabled;
            save();
            break;
        case refreshId:
            refresh();
            break;
        case helpId: {
            wchar_t exe[32768]{};
            GetModuleFileNameW(nullptr, exe, 32768);
            const auto path = std::filesystem::path(exe).parent_path() / L"README-APO.md";
            if (INT_PTR(ShellExecuteW(hwnd, L"open", path.c_str(), nullptr, nullptr,
                                      SW_SHOWNORMAL)) <= 32)
                MessageBoxW(hwnd,
                            L"See README-APO.md in the development package. The effect requires "
                            L"signed deployment before it can be enabled on your playback device.",
                            L"Drift APO setup", MB_OK);
        } break;
        case quitId:
            DestroyWindow(hwnd);
            break;
        }
        return 0;
    case WM_HSCROLL:
        parameters.depth = float(SendMessageW(depth, TBM_GETPOS, 0, 0)) / 10;
        parameters.seconds = float(SendMessageW(pace, TBM_GETPOS, 0, 0)) / 10;
        parameters.volume = float(SendMessageW(volume, TBM_GETPOS, 0, 0)) / 100;
        SendMessageW(preset, CB_SETCURSEL, WPARAM(-1), 0);
        save();
        return 0;
    case WM_APP + 1:
        if (lp == WM_LBUTTONDBLCLK)
            show();
        if (lp == WM_RBUTTONUP) {
            auto menu = CreatePopupMenu();
            AppendMenuW(menu, MF_STRING, 1, L"Open Drift EQ");
            AppendMenuW(menu, MF_STRING, 2, L"Quit controls (keep effect)");
            POINT p;
            GetCursorPos(&p);
            SetForegroundWindow(hwnd);
            const auto choice =
                TrackPopupMenu(menu, TPM_RETURNCMD | TPM_RIGHTBUTTON, p.x, p.y, 0, hwnd, nullptr);
            DestroyMenu(menu);
            if (choice == 1)
                show();
            if (choice == 2)
                DestroyWindow(hwnd);
        }
        return 0;
    case WM_CLOSE:
        ShowWindow(hwnd, SW_HIDE);
        return 0;
    case WM_DESTROY:
        Shell_NotifyIconW(NIM_DELETE, &tray);
        DeleteObject(font);
        DeleteObject(titleFont);
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}
} // namespace
int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int) {
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    int argc = 0;
    auto argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    if (argc == 3 && std::wstring(argv[1]) == L"--diagnostics") {
        std::wofstream file{std::filesystem::path(argv[2])};
        std::wstring defaultId;
        for (const auto &d : outputs(defaultId)) {
            std::uint32_t word = 0;
            file << d.name << L"\nEndpoint: " << d.id << L"\nGUID: " << d.guid
                 << L"\nDrift settings installed: " << drift::apo::readSettings(d.guid, word)
                 << L"\n\n";
        }
        LocalFree(argv);
        CoUninitialize();
        return file ? 0 : 1;
    }
    LocalFree(argv);
    HANDLE single = CreateMutexW(nullptr, TRUE, L"Local\\DriftEQAPOControls");
    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        if (auto prior = FindWindowW(L"DriftAPOControls", nullptr)) {
            ShowWindow(prior, SW_SHOW);
            SetForegroundWindow(prior);
        }
        if (single)
            CloseHandle(single);
        CoUninitialize();
        return 0;
    }
    SetProcessDPIAware();
    INITCOMMONCONTROLSEX cc{sizeof(cc), ICC_BAR_CLASSES};
    InitCommonControlsEx(&cc);
    font = CreateFontW(-17, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                       OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH,
                       L"Segoe UI");
    titleFont = CreateFontW(-28, 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                            OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                            DEFAULT_PITCH, L"Segoe UI");
    WNDCLASSW cls{};
    cls.lpfnWndProc = proc;
    cls.hInstance = instance;
    cls.lpszClassName = L"DriftAPOControls";
    cls.hIcon = LoadIconW(nullptr, IDI_APPLICATION);
    cls.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    cls.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
    RegisterClassW(&cls);
    window = CreateWindowW(cls.lpszClassName, L"Drift EQ - Native Windows effect",
                           WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX, CW_USEDEFAULT,
                           CW_USEDEFAULT, 650, 680, nullptr, nullptr, instance, nullptr);
    if (!window)
        return 1;
    auto title = add(L"STATIC", L"Drift EQ", 0, 24, 18, 570, 38);
    SendMessageW(title, WM_SETFONT, reinterpret_cast<WPARAM>(titleFont), TRUE);
    add(L"STATIC", L"Gentle motion on your normal playback device. No virtual cable.", 0, 24, 65,
        586, 42);
    picker = add(L"COMBOBOX", L"", CBS_DROPDOWNLIST | WS_TABSTOP | WS_VSCROLL, 24, 113, 586, 160,
                 deviceId);
    add(L"BUTTON", L"Refresh devices", WS_TABSTOP, 24, 153, 180, 32, refreshId);
    add(L"BUTTON", L"Setup guide", WS_TABSTOP, 218, 153, 180, 32, helpId);
    preset = add(L"COMBOBOX", L"", CBS_DROPDOWNLIST | WS_TABSTOP, 24, 213, 285, 150, presetId);
    for (auto name : {L"Subtle", L"Wander", L"Soft pulse"})
        SendMessageW(preset, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(name));
    depthText = add(L"STATIC", L"", 0, 24, 258, 580, 24);
    depth = add(TRACKBAR_CLASSW, L"", TBS_HORZ | WS_TABSTOP, 24, 286, 580, 28);
    SendMessageW(depth, TBM_SETRANGE, TRUE, MAKELPARAM(0, 20));
    paceText = add(L"STATIC", L"", 0, 24, 330, 580, 24);
    pace = add(TRACKBAR_CLASSW, L"", TBS_HORZ | WS_TABSTOP, 24, 358, 580, 28);
    SendMessageW(pace, TBM_SETRANGE, TRUE, MAKELPARAM(5, 160));
    volumeText = add(L"STATIC", L"", 0, 24, 402, 580, 24);
    volume = add(TRACKBAR_CLASSW, L"", TBS_HORZ | WS_TABSTOP, 24, 430, 580, 28);
    SendMessageW(volume, TBM_SETRANGE, TRUE, MAKELPARAM(0, 100));
    toggle = add(L"BUTTON", L"Enable effect", WS_TABSTOP, 24, 480, 190, 36, toggleId);
    add(L"BUTTON", L"Quit controls", WS_TABSTOP, 230, 480, 170, 36, quitId);
    status = add(L"STATIC", L"", 0, 24, 531, 586, 60);
    add(L"STATIC", L"Closing these controls keeps the installed effect running.", 0, 24, 600, 586,
        24);
    refresh();
    tray.cbSize = sizeof(tray);
    tray.hWnd = window;
    tray.uID = 1;
    tray.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
    tray.uCallbackMessage = WM_APP + 1;
    tray.hIcon = cls.hIcon;
    wcscpy_s(tray.szTip, L"Drift EQ native controls");
    Shell_NotifyIconW(NIM_ADD, &tray);
    ShowWindow(window, SW_SHOW);
    MSG message;
    while (GetMessageW(&message, nullptr, 0, 0) > 0)
        if (!IsDialogMessageW(window, &message)) {
            TranslateMessage(&message);
            DispatchMessageW(&message);
        }
    if (single)
        CloseHandle(single);
    CoUninitialize();
    return 0;
}
