// XM4 Mini — minimal native Windows companion for the Sony WH-1000XM4.
// Single EXE, no Qt: Win32 window + tray icon + worker thread for Bluetooth.

#include <windows.h>
#include <windowsx.h>
#include <objidl.h>  // IStream — needed by gdiplus.h when WIN32_LEAN_AND_MEAN is on
#include <gdiplus.h>
#include <shellapi.h>
#include <dbt.h>
#include <dwmapi.h>
#include <objbase.h>  // CoInitializeEx for the endpoint-volume lookup

// DWMWA_USE_IMMERSIVE_DARK_MODE (20) needs a recent SDK; fall back gracefully.
#ifndef DWMWA_USE_IMMERSIVE_DARK_MODE
#define DWMWA_USE_IMMERSIVE_DARK_MODE 20
#endif

#include <algorithm>
#include <cctype>
#include <memory>
#include <string>

#include "battery_est.h"
#include "device_link.h"
#include "settings.h"
#include "ui.h"

#define WM_TRAYICON (WM_APP + 1)
#define WM_APP_DEVICE_STATE (WM_APP + 2)
#define IDI_APP 1

namespace {

constexpr wchar_t kClassName[] = L"XM4MiniWindow";
constexpr wchar_t kMutexName[] = L"XM4MiniSingleInstance";

enum TrayCmd { ID_TRAY_ANC = 1001, ID_TRAY_AMBIENT, ID_TRAY_OFF, ID_TRAY_SHOW, ID_TRAY_QUIT };
enum MenuCmd {
    ID_MENU_RECONNECT = 2001,
    ID_MENU_DATAFOLDER,
    ID_MENU_ABOUT,
    ID_MENU_QUIT,
    ID_MENU_HEALTH_BASE = 2100  // 2100..2105 -> 50%,60%,70%,80%,90%,100%
};

int dpiOf(HWND hwnd) {
    const int d = GetDpiForWindow(hwnd);
    return d > 0 ? d : 96;
}
int scaled(HWND hwnd, int px) { return MulDiv(px, dpiOf(hwnd), 96); }

}  // namespace

class App {
public:
    App(HINSTANCE inst, int dpi)
        : inst_(inst), ui_(nullptr), link_(std::make_unique<DeviceLink>()) {
        (void)dpi;
    }

    bool init(int showCmd) {
        WNDCLASSEXW wc{};
        wc.cbSize = sizeof(wc);
        wc.style = CS_HREDRAW | CS_VREDRAW;
        wc.lpfnWndProc = &App::wndProc;
        wc.hInstance = inst_;
        wc.hIcon = LoadIconW(inst_, MAKEINTRESOURCEW(IDI_APP));
        wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        wc.hbrBackground = CreateSolidBrush(RGB(0x0c, 0x0c, 0x0e));
        wc.lpszClassName = kClassName;
        if (!RegisterClassExW(&wc))
            return false;
        // Class icon: prefer our resource icon; fall back to the stock one
        // (shared icons must not be destroyed, so ownership is conditional).
        HICON rawIcon = LoadIconW(inst_, MAKEINTRESOURCEW(IDI_APP));
        if (rawIcon) {
            wc.hIcon = rawIcon;
            hIcon_.reset(static_cast<HICON>(CopyIcon(rawIcon)));
            trayIcon_ = hIcon_.get();
        } else {
            trayIcon_ =
                static_cast<HICON>(LoadImageW(nullptr, IDI_APPLICATION, IMAGE_ICON, 0, 0, LR_SHARED));
        }

        hwnd_ = CreateWindowExW(0, kClassName, L"XM4 Mini",
                                WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX,
                                CW_USEDEFAULT, CW_USEDEFAULT, 400, 600, nullptr, nullptr, inst_,
                                this);
        if (!hwnd_)
            return false;

        // Dark title bar to match the app theme (Windows 10 1809+).
        {
            const BOOL dark = TRUE;
            DwmSetWindowAttribute(hwnd_, DWMWA_USE_IMMERSIVE_DARK_MODE, &dark, sizeof(dark));
        }

        ui_ = std::make_unique<Ui>(hwnd_);
        const AppSettings loaded = loadSettings();
        ui_->applySettings(loaded);
        batteryHealth_ = loaded.batteryHealth;
        ui_->setCallbacks(
            [this](sony::protocol::NoiseControlState st) { link_->setNoiseControl(st); },
            [this](int preset) { link_->setEqPreset(preset); },
            [this](int cb, const std::array<int, 5>& bands) { link_->setEqCustom(cb, bands); },
            [this] { showMenuPopup(); }, [this] { link_->asyncPowerOff(); });

        link_->setStateCallback([this](const DeviceState& st) {
            auto* copy = new DeviceState(st);
            PostMessageW(hwnd_, WM_APP_DEVICE_STATE, 0, reinterpret_cast<LPARAM>(copy));
        });

        addTrayIcon();
        fitWindow(true);
        ShowWindow(hwnd_, showCmd);
        UpdateWindow(hwnd_);
        link_->start();
        return true;
    }

    int run() {
        MSG msg;
        while (GetMessageW(&msg, nullptr, 0, 0)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
        return static_cast<int>(msg.wParam);
    }

    Ui* ui() { return ui_.get(); }
    HWND hwnd() const { return hwnd_; }

private:
    struct IconDeleter {
        void operator()(HICON h) const {
            if (h)
                DestroyIcon(h);
        }
    };
    using IconPtr = std::unique_ptr<std::remove_pointer<HICON>::type, IconDeleter>;

    static LRESULT CALLBACK wndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
        App* app = nullptr;
        if (msg == WM_NCCREATE) {
            auto* cs = reinterpret_cast<CREATESTRUCTW*>(lParam);
            app = static_cast<App*>(cs->lpCreateParams);
            SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(app));
            app->hwnd_ = hwnd;
        } else {
            app = reinterpret_cast<App*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
        }
        if (app)
            return app->handle(msg, wParam, lParam);
        return DefWindowProcW(hwnd, msg, wParam, lParam);
    }

    LRESULT handle(UINT msg, WPARAM wParam, LPARAM lParam) {
        switch (msg) {
            case WM_PAINT: {
                PAINTSTRUCT ps;
                HDC hdc = BeginPaint(hwnd_, &ps);
                RECT rc;
                GetClientRect(hwnd_, &rc);
                HDC mem = CreateCompatibleDC(hdc);
                HBITMAP bmp = CreateCompatibleBitmap(hdc, std::max(1L, rc.right), std::max(1L, rc.bottom));
                HGDIOBJ old = SelectObject(mem, bmp);
                ui_->paint(mem);
                BitBlt(hdc, 0, 0, rc.right, rc.bottom, mem, 0, 0, SRCCOPY);
                SelectObject(mem, old);
                DeleteObject(bmp);
                DeleteDC(mem);
                EndPaint(hwnd_, &ps);
                return 0;
            }
            case WM_LBUTTONDOWN:
                ui_->onLButtonDown(GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));
                fitWindow(false);
                return 0;
            case WM_LBUTTONUP:
                ui_->onLButtonUp(GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));
                fitWindow(false);
                return 0;
            case WM_MOUSEMOVE:
                ui_->onMouseMove(GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));
                return 0;
            case WM_MOUSEWHEEL: {
                // lParam is in screen coords for WM_MOUSEWHEEL.
                POINT pt{ GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
                ScreenToClient(hwnd_, &pt);
                ui_->onWheel(pt.x, pt.y, GET_WHEEL_DELTA_WPARAM(wParam));
                fitWindow(false);
                return 0;
            }
            case WM_DPICHANGED: {
                ui_->onDpiChanged();
                auto* rc = reinterpret_cast<RECT*>(lParam);
                SetWindowPos(hwnd_, nullptr, rc->left, rc->top, rc->right - rc->left,
                             rc->bottom - rc->top, SWP_NOZORDER | SWP_NOACTIVATE);
                fitWindow(true);
                return 0;
            }
            case WM_DEVICECHANGE:
                if (wParam == DBT_DEVICEARRIVAL)
                    link_->asyncConnectNow();
                return 0;
            case WM_TRAYICON:
                if (lParam == WM_RBUTTONUP)
                    showTrayMenu();
                else if (lParam == WM_LBUTTONDBLCLK)
                    showWindow();
                return 0;
            case WM_APP_DEVICE_STATE: {
                auto* st = reinterpret_cast<DeviceState*>(lParam);
                onDeviceState(*st);
                delete st;
                return 0;
            }
            case WM_COMMAND:
                onCommand(LOWORD(wParam));
                return 0;
            case WM_CLOSE:
                // Close hides to the tray; Quit (tray menu) really exits.
                ShowWindow(hwnd_, SW_HIDE);
                if (!trayHintShown_) {
                    trayHintShown_ = true;
                    NOTIFYICONDATAW nid{};
                    nid.cbSize = sizeof(nid);
                    nid.hWnd = hwnd_;
                    nid.uID = 1;
                    nid.uFlags = NIF_INFO;
                    wcscpy_s(nid.szInfoTitle, L"XM4 Mini");
                    wcscpy_s(nid.szInfo, L"Still running in the tray — right-click the icon to quit.");
                    nid.dwInfoFlags = NIIF_INFO;
                    Shell_NotifyIconW(NIM_MODIFY, &nid);
                }
                return 0;
            case WM_DESTROY: {
                AppSettings s = ui_->currentSettings();
                s.batteryHealth = batteryHealth_;
                saveSettings(s);
                removeTrayIcon();
                link_->stop();
                PostQuitMessage(0);
                return 0;
            }
        }
        return DefWindowProcW(hwnd_, msg, wParam, lParam);
    }

    void onDeviceState(const DeviceState& st) {
        ui_->setDeviceState(st);
        ui_->setBatteryText(batteryTextFor(st));
        updateTrayTip(st);
        fitWindow(false);
        InvalidateRect(hwnd_, nullptr, FALSE);
    }

    // Runtime estimate from the live listening state (capacity/draw model).
    std::wstring batteryTextFor(const DeviceState& st) {
        if (!st.connected || st.battery < 0)
            return L"—";
        BatteryModelInputs in;
        in.batteryPct = st.battery;
        in.processingOn = st.nc.mode != sony::protocol::NoiseControlMode::Off;
        std::string codec = st.codec;
        std::transform(codec.begin(), codec.end(), codec.begin(),
                       [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        in.ldac = codec.find("ldac") != std::string::npos;
        in.dsee = false;  // DSEE state is not tracked by the app
        in.volumePct = xm4EndpointVolumePct();
        in.healthPct = batteryHealth_;
        return formatBatteryEta(estimateBatteryRuntime(in), st.charging);
    }

    void fitWindow(bool force) {
        const int ch = ui_->contentHeight();
        if (!force && ch == lastClientH_)
            return;
        lastClientH_ = ch;
        RECT rc{0, 0, scaled(hwnd_, 340), ch};
        const DWORD style = static_cast<DWORD>(GetWindowLongW(hwnd_, GWL_STYLE));
        AdjustWindowRectEx(&rc, style, FALSE, 0);
        SetWindowPos(hwnd_, nullptr, 0, 0, rc.right - rc.left, rc.bottom - rc.top,
                     SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
    }

    void addTrayIcon() {
        NOTIFYICONDATAW nid{};
        nid.cbSize = sizeof(nid);
        nid.hWnd = hwnd_;
        nid.uID = 1;
        nid.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP | NIF_SHOWTIP;
        nid.uCallbackMessage = WM_TRAYICON;
        nid.hIcon = trayIcon_;
        wcscpy_s(nid.szTip, L"XM4 Mini");
        Shell_NotifyIconW(NIM_ADD, &nid);
        nid.uFlags = NIF_STATE;
        nid.dwState = 0;
        nid.dwStateMask = NIS_HIDDEN;
        Shell_NotifyIconW(NIM_MODIFY, &nid);
    }

    void removeTrayIcon() {
        NOTIFYICONDATAW nid{};
        nid.cbSize = sizeof(nid);
        nid.hWnd = hwnd_;
        nid.uID = 1;
        Shell_NotifyIconW(NIM_DELETE, &nid);
    }

    void updateTrayTip(const DeviceState& st) {
        NOTIFYICONDATAW nid{};
        nid.cbSize = sizeof(nid);
        nid.hWnd = hwnd_;
        nid.uID = 1;
        nid.uFlags = NIF_TIP | NIF_SHOWTIP;
        if (st.connected && st.battery >= 0) {
            std::wstring tip = L"XM4 Mini — " + std::to_wstring(st.battery) + L"%";
            const std::wstring eta = batteryTextFor(st);
            if (!eta.empty() && eta != L"—")
                tip += L" (" + eta + L")";
            wcscpy_s(nid.szTip, tip.c_str());
        } else {
            wcscpy_s(nid.szTip, L"XM4 Mini — looking for WH-1000XM4…");
        }
        Shell_NotifyIconW(NIM_MODIFY, &nid);
    }

    void showWindow() {
        ShowWindow(hwnd_, SW_RESTORE);
        SetForegroundWindow(hwnd_);
    }

    void showTrayMenu() {
        HMENU menu = CreatePopupMenu();
        AppendMenuW(menu, MF_STRING, ID_TRAY_ANC, L"Noise Cancelling");
        AppendMenuW(menu, MF_STRING, ID_TRAY_AMBIENT, L"Ambient Sound");
        AppendMenuW(menu, MF_STRING, ID_TRAY_OFF, L"Off");
        AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
        AppendMenuW(menu, MF_STRING, ID_TRAY_SHOW, L"Show XM4 Mini");
        AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
        AppendMenuW(menu, MF_STRING, ID_TRAY_QUIT, L"Quit");
        POINT pt;
        GetCursorPos(&pt);
        SetForegroundWindow(hwnd_);
        TrackPopupMenu(menu, TPM_RIGHTBUTTON, pt.x, pt.y, 0, hwnd_, nullptr);
        DestroyMenu(menu);
    }

    void showMenuPopup() {
        HMENU menu = CreatePopupMenu();
        AppendMenuW(menu, MF_STRING, ID_MENU_RECONNECT, L"Reconnect");
        AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
        HMENU health = CreatePopupMenu();
        for (int i = 0; i < 6; ++i) {
            const int pct = 50 + i * 10;
            wchar_t label[16]{};
            swprintf_s(label, L"%d%%", pct);
            AppendMenuW(health, MF_STRING, ID_MENU_HEALTH_BASE + i, label);
            if (pct == batteryHealth_)
                CheckMenuItem(health, ID_MENU_HEALTH_BASE + i, MF_BYCOMMAND | MF_CHECKED);
        }
        AppendMenuW(menu, MF_STRING | MF_POPUP, reinterpret_cast<UINT_PTR>(health),
                    L"Battery health");
        AppendMenuW(menu, MF_STRING, ID_MENU_DATAFOLDER, L"Open data folder");
        AppendMenuW(menu, MF_STRING, ID_MENU_ABOUT, L"About XM4 Mini");
        AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
        AppendMenuW(menu, MF_STRING, ID_MENU_QUIT, L"Quit");
        POINT pt;
        GetCursorPos(&pt);
        SetForegroundWindow(hwnd_);
        TrackPopupMenu(menu, TPM_RIGHTBUTTON, pt.x, pt.y, 0, hwnd_, nullptr);
        DestroyMenu(menu);
    }

    void onCommand(int id) {
        using Mode = sony::protocol::NoiseControlMode;
        if (id >= ID_MENU_HEALTH_BASE && id < ID_MENU_HEALTH_BASE + 6) {
            batteryHealth_ = 50 + (id - ID_MENU_HEALTH_BASE) * 10;
            link_->refreshBattery();  // republish -> the estimate text recomputes
            return;
        }
        switch (id) {
            case ID_TRAY_ANC:
            case ID_TRAY_AMBIENT:
            case ID_TRAY_OFF: {
                sony::protocol::NoiseControlState st;
                st.mode = (id == ID_TRAY_ANC) ? Mode::NoiseCancelling
                          : (id == ID_TRAY_AMBIENT) ? Mode::Ambient
                                                    : Mode::Off;
                st.ambientLevel = ui_->currentSettings().ambientLevel;
                st.focusOnVoice = ui_->currentSettings().focusOnVoice != 0;
                link_->setNoiseControl(st);
                break;
            }
            case ID_TRAY_SHOW:
                showWindow();
                break;
            case ID_TRAY_QUIT:
            case ID_MENU_QUIT:
                DestroyWindow(hwnd_);
                break;
            case ID_MENU_RECONNECT:
                link_->asyncConnectNow();
                break;
            case ID_MENU_DATAFOLDER:
                ShellExecuteW(nullptr, L"open", appDataDir().c_str(), nullptr, nullptr, SW_SHOWNORMAL);
                break;
            case ID_MENU_ABOUT:
                MessageBoxW(hwnd_,
                            L"XM4 Mini 0.1.0\nMinimal companion for the Sony WH-1000XM4.\n\n"
                            L"Protocol: reverse-engineered Sony V1 over Bluetooth RFCOMM\n"
                            L"(vendored from Sound-connect, MIT).",
                            L"About XM4 Mini", MB_OK | MB_ICONINFORMATION);
                break;
        }
    }

    HINSTANCE inst_;
    HWND hwnd_ = nullptr;
    IconPtr hIcon_;
    HICON trayIcon_ = nullptr;  // owned only when it is our copied resource icon
    std::unique_ptr<Ui> ui_;
    std::unique_ptr<DeviceLink> link_;
    int batteryHealth_ = 100;  // persisted; scales the runtime model
    int lastClientH_ = 0;
    bool trayHintShown_ = false;
};

int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE, LPWSTR, int nCmdShow) {
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    // COM for the audio-endpoint volume lookup (main thread only).
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);

    HANDLE mutex = CreateMutexW(nullptr, TRUE, kMutexName);
    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        HWND existing = FindWindowW(kClassName, nullptr);
        if (existing) {
            ShowWindow(existing, SW_RESTORE);
            SetForegroundWindow(existing);
        }
        if (mutex)
            CloseHandle(mutex);
        CoUninitialize();
        return 0;
    }

    // The discharge-history CSV is superseded by the runtime model; drop it.
    DeleteFileW((appDataDir() + L"\\battery.csv").c_str());

    Gdiplus::GdiplusStartupInput gdiplusInput;
    ULONG_PTR gdiplusToken = 0;
    Gdiplus::GdiplusStartup(&gdiplusToken, &gdiplusInput, nullptr);

    App app(hInstance, 96);
    int exitCode = 1;
    if (app.init(nCmdShow))
        exitCode = app.run();

    Gdiplus::GdiplusShutdown(gdiplusToken);
    if (mutex) {
        ReleaseMutex(mutex);
        CloseHandle(mutex);
    }
    CoUninitialize();
    return exitCode;
}
