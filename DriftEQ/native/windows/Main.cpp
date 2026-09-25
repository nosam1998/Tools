#include "Wasapi.h"
#include <commctrl.h>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <memory>
#include <shellapi.h>
#include <shlobj.h>
#include <sstream>

namespace {
constexpr UINT trayMessage = WM_APP + 1;
constexpr int inputId = 101, outputId = 102, startId = 103, stopId = 104, bypassId = 105,
              presetId = 106, depthId = 107, paceId = 108, volumeId = 109, refreshId = 110,
              soundId = 111, showId = 112, exitId = 113;
drift::Controls controls;
WasapiAudio audio(controls);
HWND windowHandle{}, inputBox{}, outputBox{}, presetBox{}, depthSlider{}, paceSlider{},
    volumeSlider{}, depthText{}, paceText{}, volumeText{}, statusText{}, startButton{},
    stopButton{}, bypassButton{}, refreshButton{};
HFONT font{}, titleFont{};
NOTIFYICONDATAW tray{};
std::vector<Device> inputs, outputs;
std::wstring settingsPath, savedInput, savedOutput;
HWND control(const wchar_t *cls, const wchar_t *text, DWORD style, int x, int y, int w, int h,
             int id = 0) {
    HWND result =
        CreateWindowExW(0, cls, text, WS_CHILD | WS_VISIBLE | style, x, y, w, h, windowHandle,
                        reinterpret_cast<HMENU>(INT_PTR(id)), GetModuleHandleW(nullptr), nullptr);
    SendMessageW(result, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
    return result;
}
void soundSettings() {
    ShellExecuteW(windowHandle, L"open", L"ms-settings:sound", nullptr, nullptr, SW_SHOWNORMAL);
}
void labelValues() {
    const auto p = controls.read();
    wchar_t text[128];
    swprintf_s(text, L"Maximum movement: +/- %.1f dB", p.depth);
    SetWindowTextW(depthText, text);
    swprintf_s(text,
               p.pulse ? L"Time per tonal pulse: %.1f seconds"
                       : L"Typical time between targets: %.1f seconds",
               p.seconds);
    SetWindowTextW(paceText, text);
    swprintf_s(text, L"Output level: %.0f%%", p.volume * 100);
    SetWindowTextW(volumeText, text);
    SetWindowTextW(bypassButton, p.enabled ? L"Bypass EQ" : L"Enable EQ");
}
void setSliders() {
    const auto p = controls.read();
    SendMessageW(depthSlider, TBM_SETPOS, TRUE, LPARAM(p.depth * 10));
    SendMessageW(paceSlider, TBM_SETPOS, TRUE, LPARAM(p.seconds * 10));
    SendMessageW(volumeSlider, TBM_SETPOS, TRUE, LPARAM(p.volume * 100));
    labelValues();
}
int selected(HWND box) {
    return int(SendMessageW(box, CB_GETCURSEL, 0, 0));
}
void save() {
    if (settingsPath.empty())
        return;
    const auto p = controls.read();
    WritePrivateProfileStringW(L"Drift", L"Depth", std::to_wstring(p.depth).c_str(),
                               settingsPath.c_str());
    WritePrivateProfileStringW(L"Drift", L"Seconds", std::to_wstring(p.seconds).c_str(),
                               settingsPath.c_str());
    WritePrivateProfileStringW(L"Drift", L"Volume", std::to_wstring(p.volume).c_str(),
                               settingsPath.c_str());
    WritePrivateProfileStringW(L"Drift", L"Pulse", p.pulse ? L"1" : L"0", settingsPath.c_str());
    WritePrivateProfileStringW(L"Drift", L"Enabled", p.enabled ? L"1" : L"0", settingsPath.c_str());
    const int i = selected(inputBox), o = selected(outputBox);
    if (i >= 0 && std::size_t(i) < inputs.size())
        WritePrivateProfileStringW(L"Drift", L"Input", inputs[i].id.c_str(), settingsPath.c_str());
    if (o >= 0 && std::size_t(o) < outputs.size())
        WritePrivateProfileStringW(L"Drift", L"Output", outputs[o].id.c_str(),
                                   settingsPath.c_str());
}
void load() {
    PWSTR directory = nullptr;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_LocalAppData, 0, nullptr, &directory))) {
        std::filesystem::path folder = std::filesystem::path(directory) / L"DriftEQ";
        CoTaskMemFree(directory);
        std::error_code error;
        std::filesystem::create_directories(folder, error);
        if (error)
            return;
        settingsPath = (folder / L"settings.ini").wstring();
    }
    if (settingsPath.empty())
        return;
    wchar_t text[2048];
    auto number = [&](const wchar_t *key, const wchar_t *fallback) {
        GetPrivateProfileStringW(L"Drift", key, fallback, text, 2048, settingsPath.c_str());
        return float(wcstod(text, nullptr));
    };
    drift::Parameters p;
    p.depth = number(L"Depth", L"0.8");
    p.seconds = number(L"Seconds", L"6");
    p.volume = number(L"Volume", L"0.6");
    p.pulse = GetPrivateProfileIntW(L"Drift", L"Pulse", 0, settingsPath.c_str()) != 0;
    p.enabled = GetPrivateProfileIntW(L"Drift", L"Enabled", 1, settingsPath.c_str()) != 0;
    controls.set(p);
    GetPrivateProfileStringW(L"Drift", L"Input", L"", text, 2048, settingsPath.c_str());
    savedInput = text;
    GetPrivateProfileStringW(L"Drift", L"Output", L"", text, 2048, settingsPath.c_str());
    savedOutput = text;
}
void refresh() {
    if (audio.running() || audio.starting())
        return;
    inputs.clear();
    outputs.clear();
    SendMessageW(inputBox, CB_RESETCONTENT, 0, 0);
    SendMessageW(outputBox, CB_RESETCONTENT, 0, 0);
    for (auto &d : audioDevices(true))
        if (cableDevice(d.name))
            inputs.push_back(d);
    for (auto &d : audioDevices(false))
        if (!cableDevice(d.name))
            outputs.push_back(d);
    const auto defaultId = defaultOutputId();
    int in = inputs.empty() ? -1 : 0, out = outputs.empty() ? -1 : 0;
    for (std::size_t i = 0; i < inputs.size(); ++i) {
        SendMessageW(inputBox, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(inputs[i].name.c_str()));
        if (inputs[i].id == savedInput)
            in = int(i);
    }
    for (std::size_t i = 0; i < outputs.size(); ++i) {
        SendMessageW(outputBox, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(outputs[i].name.c_str()));
        if (outputs[i].id == defaultId)
            out = int(i);
    }
    for (std::size_t i = 0; i < outputs.size(); ++i)
        if (outputs[i].id == savedOutput)
            out = int(i);
    SendMessageW(inputBox, CB_SETCURSEL, in, 0);
    SendMessageW(outputBox, CB_SETCURSEL, out, 0);
    SetWindowTextW(
        statusText,
        inputs.empty()
            ? L"VB-CABLE was not found. Install it separately, then click Refresh devices. Your "
              L"normal audio is unchanged."
            : L"Ready. Start Drift, then select CABLE Input as your Windows playback device.");
}
bool routedToCable() {
    const auto id = defaultOutputId();
    for (const auto &d : audioDevices(false))
        if (d.id == id)
            return cableDevice(d.name);
    return false;
}
void exitApp() {
    if (routedToCable()) {
        const auto choice = MessageBoxW(
            windowHandle,
            L"Windows is still sending sound into the virtual cable. Closing Drift may leave that "
            L"audio silent.\n\nOpen Sound settings before quitting?\n\nYes: open settings and keep "
            L"Drift running.\nNo: quit now.\nCancel: keep running.",
            L"Restore your normal audio output", MB_YESNOCANCEL | MB_ICONINFORMATION);
        if (choice == IDYES) {
            soundSettings();
            return;
        }
        if (choice != IDNO)
            return;
    }
    DestroyWindow(windowHandle);
}
LRESULT CALLBACK procedure(HWND window, UINT message, WPARAM wp, LPARAM lp) {
    switch (message) {
    case WM_COMMAND: {
        const int id = LOWORD(wp);
        if (id == startId) {
            const int i = selected(inputBox), o = selected(outputBox);
            if (i < 0 || o < 0)
                return 0;
            save();
            audio.start(inputs.at(i).id, outputs.at(o).id);
            SetWindowTextW(statusText, L"Starting audio...");
        } else if (id == stopId) {
            audio.stop();
            SetWindowTextW(statusText, L"Stopped. Select your physical headphones/speakers in "
                                       L"Windows Sound settings to restore normal playback.");
        } else if (id == bypassId) {
            controls.enabled = !controls.enabled.load();
            labelValues();
            save();
        } else if (id == refreshId)
            refresh();
        else if (id == soundId)
            soundSettings();
        else if (id == presetId && HIWORD(wp) == CBN_SELCHANGE) {
            const auto old = controls.read();
            auto p = drift::preset(unsigned(selected(presetBox)));
            p.volume = old.volume;
            p.enabled = old.enabled;
            controls.set(p);
            setSliders();
            save();
        } else if (id == showId) {
            ShowWindow(window, SW_SHOW);
            SetForegroundWindow(window);
        } else if (id == exitId)
            exitApp();
        return 0;
    }
    case WM_HSCROLL: {
        controls.depth = float(SendMessageW(depthSlider, TBM_GETPOS, 0, 0)) / 10;
        controls.seconds = float(SendMessageW(paceSlider, TBM_GETPOS, 0, 0)) / 10;
        controls.volume = float(SendMessageW(volumeSlider, TBM_GETPOS, 0, 0)) / 100;
        SendMessageW(presetBox, CB_SETCURSEL, WPARAM(-1), 0);
        labelValues();
        save();
        return 0;
    }
    case WM_TIMER: {
        const bool active = audio.running() || audio.starting();
        EnableWindow(startButton, !active && !inputs.empty() && !outputs.empty());
        EnableWindow(stopButton, active);
        EnableWindow(inputBox, !active);
        EnableWindow(outputBox, !active);
        EnableWindow(refreshButton, !active);
        if (audio.running()) {
            std::wostringstream text;
            text << (controls.enabled ? L"Drift is processing" : L"EQ bypassed; audio still routed")
                 << L" | 48 kHz stereo | Buffer underruns: " << audio.underruns()
                 << L" | Overruns: " << audio.overruns();
            SetWindowTextW(statusText, text.str().c_str());
        } else if (!active) {
            const auto error = audio.error();
            if (!error.empty())
                SetWindowTextW(statusText, error.c_str());
        }
        return 0;
    }
    case trayMessage:
        if (lp == WM_LBUTTONDBLCLK) {
            ShowWindow(window, SW_SHOW);
            SetForegroundWindow(window);
        } else if (lp == WM_RBUTTONUP) {
            HMENU menu = CreatePopupMenu();
            AppendMenuW(menu, MF_STRING, showId, L"Open Drift EQ");
            AppendMenuW(menu, MF_STRING, bypassId, controls.enabled ? L"Bypass EQ" : L"Enable EQ");
            AppendMenuW(menu, MF_STRING, soundId, L"Windows Sound settings");
            AppendMenuW(menu, MF_STRING, exitId, L"Quit");
            POINT point;
            GetCursorPos(&point);
            SetForegroundWindow(window);
            TrackPopupMenu(menu, TPM_RIGHTBUTTON, point.x, point.y, 0, window, nullptr);
            DestroyMenu(menu);
        }
        return 0;
    case WM_CLOSE:
        ShowWindow(window, SW_HIDE);
        return 0;
    case WM_QUERYENDSESSION:
        return TRUE;
    case WM_DESTROY:
        save();
        KillTimer(window, 1);
        audio.stop();
        Shell_NotifyIconW(NIM_DELETE, &tray);
        DeleteObject(font);
        DeleteObject(titleFont);
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(window, message, wp, lp);
}
} // namespace
int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int) {
    int argc = 0;
    LPWSTR *argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    if (argc == 3 && std::wstring(argv[1]) == L"--diagnostics") {
        std::wofstream file{std::filesystem::path(argv[2])};
        file << L"Drift EQ device enumeration (no capture/playback started)\n";
        for (const auto &d : audioDevices(true))
            file << L"Input: " << d.name << L"\n";
        for (const auto &d : audioDevices(false))
            file << L"Output: " << d.name << L"\n";
        LocalFree(argv);
        return file ? 0 : 1;
    }
    LocalFree(argv);
    HANDLE singleton = CreateMutexW(nullptr, TRUE, L"Local\\DriftEQDesktop");
    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        if (auto existing = FindWindowW(L"DriftEQWindow", nullptr)) {
            ShowWindow(existing, SW_SHOW);
            SetForegroundWindow(existing);
        }
        if (singleton)
            CloseHandle(singleton);
        return 0;
    }
    SetProcessDPIAware();
    INITCOMMONCONTROLSEX common{sizeof(common), ICC_BAR_CLASSES};
    InitCommonControlsEx(&common);
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    font = CreateFontW(-17, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                       OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH,
                       L"Segoe UI");
    titleFont = CreateFontW(-30, 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                            OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                            DEFAULT_PITCH, L"Segoe UI");
    WNDCLASSW cls{};
    cls.lpfnWndProc = procedure;
    cls.hInstance = instance;
    cls.lpszClassName = L"DriftEQWindow";
    cls.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    cls.hIcon = LoadIconW(nullptr, IDI_APPLICATION);
    cls.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
    RegisterClassW(&cls);
    windowHandle =
        CreateWindowW(cls.lpszClassName, L"Drift EQ - Calm chaos",
                      WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX, CW_USEDEFAULT,
                      CW_USEDEFAULT, 660, 710, nullptr, nullptr, instance, nullptr);
    if (!windowHandle)
        return 1;
    auto title = control(L"STATIC", L"Drift EQ", 0, 24, 18, 580, 40);
    SendMessageW(title, WM_SETFONT, reinterpret_cast<WPARAM>(titleFont), TRUE);
    control(L"STATIC", L"Subtle, moving tone for the sound you already enjoy.", 0, 24, 64, 595, 24);
    control(L"STATIC", L"Input from virtual cable (CABLE Output)", 0, 24, 110, 570, 24);
    inputBox = control(L"COMBOBOX", L"", CBS_DROPDOWNLIST | WS_TABSTOP | WS_VSCROLL, 24, 138, 594,
                       160, inputId);
    control(L"STATIC", L"Play through these speakers or headphones", 0, 24, 178, 570, 24);
    outputBox = control(L"COMBOBOX", L"", CBS_DROPDOWNLIST | WS_TABSTOP | WS_VSCROLL, 24, 206, 594,
                        160, outputId);
    refreshButton = control(L"BUTTON", L"Refresh devices", WS_TABSTOP, 24, 247, 165, 32, refreshId);
    control(L"BUTTON", L"Windows Sound settings", WS_TABSTOP, 201, 247, 245, 32, soundId);
    control(L"STATIC", L"Preset", 0, 24, 300, 75, 25);
    presetBox =
        control(L"COMBOBOX", L"", CBS_DROPDOWNLIST | WS_TABSTOP, 105, 294, 245, 150, presetId);
    for (auto name : {L"Subtle", L"Wander", L"Soft pulse"})
        SendMessageW(presetBox, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(name));
    depthText = control(L"STATIC", L"", 0, 24, 340, 580, 24);
    depthSlider = control(TRACKBAR_CLASSW, L"", TBS_HORZ | WS_TABSTOP, 24, 364, 590, 30, depthId);
    SendMessageW(depthSlider, TBM_SETRANGE, TRUE, MAKELPARAM(0, 20));
    paceText = control(L"STATIC", L"", 0, 24, 402, 580, 24);
    paceSlider = control(TRACKBAR_CLASSW, L"", TBS_HORZ | WS_TABSTOP, 24, 426, 590, 30, paceId);
    SendMessageW(paceSlider, TBM_SETRANGE, TRUE, MAKELPARAM(5, 160));
    volumeText = control(L"STATIC", L"", 0, 24, 464, 580, 24);
    volumeSlider = control(TRACKBAR_CLASSW, L"", TBS_HORZ | WS_TABSTOP, 24, 488, 590, 30, volumeId);
    SendMessageW(volumeSlider, TBM_SETRANGE, TRUE, MAKELPARAM(0, 100));
    startButton =
        control(L"BUTTON", L"Start", WS_TABSTOP | BS_DEFPUSHBUTTON, 24, 536, 120, 36, startId);
    stopButton = control(L"BUTTON", L"Stop routing", WS_TABSTOP, 156, 536, 145, 36, stopId);
    bypassButton = control(L"BUTTON", L"Bypass EQ", WS_TABSTOP, 313, 536, 145, 36, bypassId);
    control(L"BUTTON", L"Quit", WS_TABSTOP, 470, 536, 145, 36, exitId);
    statusText = control(L"STATIC", L"", 0, 24, 588, 595, 64);
    load();
    setSliders();
    refresh();
    SetTimer(windowHandle, 1, 250, nullptr);
    tray.cbSize = sizeof(tray);
    tray.hWnd = windowHandle;
    tray.uID = 1;
    tray.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP;
    tray.uCallbackMessage = trayMessage;
    tray.hIcon = cls.hIcon;
    wcscpy_s(tray.szTip, L"Drift EQ - double-click to open");
    Shell_NotifyIconW(NIM_ADD, &tray);
    ShowWindow(windowHandle, SW_SHOW);
    MSG message;
    while (GetMessageW(&message, nullptr, 0, 0) > 0) {
        if (!IsDialogMessageW(windowHandle, &message)) {
            TranslateMessage(&message);
            DispatchMessageW(&message);
        }
    }
    CoUninitialize();
    if (singleton)
        CloseHandle(singleton);
    return 0;
}
