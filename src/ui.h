#pragma once
// Ui: the whole window is owner-drawn (no dialog controls) so the app can
// match the approved dark/amber mockup exactly while staying dependency-free.
// Layout is recomputed on every paint/input event; the window is resized to
// fit the content height.

#include <windows.h>
#include <objidl.h>  // IStream — needed by gdiplus.h when WIN32_LEAN_AND_MEAN is on
#include <gdiplus.h>

#include <array>
#include <functional>
#include <string>
#include <vector>

#include "device_link.h"
#include "settings.h"

class Ui {
public:
    using NcCallback = std::function<void(sony::protocol::NoiseControlState)>;
    using EqPresetCallback = std::function<void(int)>;
    using EqCustomCallback = std::function<void(int clearBass, const std::array<int, 5>& bands)>;
    using SimpleCallback = std::function<void()>;

    explicit Ui(HWND hwnd);
    ~Ui();

    void setCallbacks(NcCallback nc, EqPresetCallback eqp, EqCustomCallback eqc,
                      SimpleCallback menu, SimpleCallback power);

    void setDeviceState(const DeviceState& st);
    void setBatteryText(const std::wstring& t);
    void applySettings(const AppSettings& s);  // pre-connect defaults
    AppSettings currentSettings() const;       // for saving on exit

    int contentHeight();  // recompute layout, returns needed client height
    void paint(HDC hdc);
    void onDpiChanged();
    void tickBlink() { blinkOn_ = !blinkOn_; }  // connection-dot blink

    void onLButtonDown(int x, int y);
    void onLButtonUp(int x, int y);
    void onMouseMove(int x, int y);
    void onWheel(int x, int y, int delta);

private:
    enum HitId {
        HitNone = 0,
        HitMenu,
        HitPower,
        HitModeOff,
        HitModeAnc,
        HitModeAmbient,
        HitAmbSlider,
        HitFovToggle,
        HitPill,       // data: 0=Focus 1=Office 2=Aware
        HitEqHeader,
        HitEqPreset,   // data: preset byte
        HitEqScroll,   // preset-list scrollbar
        HitCbSlider,
        HitBandSlider,  // data: band index 0..4
    };

    struct Hit {
        HitId id = HitNone;
        int data = 0;
        RECT rc{};
    };

    struct Colors {
        COLORREF bg, card, cardBorder, text, muted, faint, amber, amberLight, green, trackBg, circleBorder;
    };

    int S(int px) const;  // dpi scale
    void layout();        // fills hits_ and contentH_
    HitId hitTest(int x, int y, int* data) const;

    void drawSlider(HDC hdc, const RECT& rc, int min, int max, int value);
    void drawToggle(HDC hdc, const RECT& rc, bool on);
    void drawModeIcon(HDC hdc, const RECT& rc, HitId which, bool active);

    void setNcMode(sony::protocol::NoiseControlMode mode);
    void setAmbientLevel(int level);
    void setFov(bool on);
    void applyPill(int pill);
    void setEqPresetUi(int preset);
    void setBandValue(int band, int value);
    void setClearBass(int cb);

    HWND hwnd_;
    int dpi_ = 96;
    Colors c_{};

    HFONT fTitle_ = nullptr, fNormal_ = nullptr, fSmall_ = nullptr, fTiny_ = nullptr,
          fBold_ = nullptr;
    void makeFonts();

    DeviceState dev_;
    std::wstring batteryText_ = L"—";
    bool eqExpanded_ = false;
    bool blinkOn_ = true;  // connection-dot blink phase

    // EQ preset dropdown: a 3-row viewport over the full preset list.
    int eqScroll_ = 0;      // pixel offset of the viewport into the list
    int eqListX_ = 0, eqListY_ = 0, eqListW_ = 0, eqListH_ = 0;
    bool eqScrollDrag_ = false;
    int eqScrollDragY_ = 0;
    int eqScrollDragOff_ = 0;
    RECT eqThumbRect() const;      // scrollbar thumb, or empty when none
    void scrollEqToPreset(int preset);

    // Local echo of user edits (device state arrives async and converges).
    sony::protocol::NoiseControlState ncUi_;
    sony::protocol::EqualizerState eqUi_;

    std::vector<Hit> hits_;
    int contentH_ = 0;

    // Drag state
    HitId dragId_ = HitNone;
    int dragData_ = 0;

    Gdiplus::Image* hpImg_ = nullptr;  // WH-1000XM4 render (may be null)

    NcCallback onNc_;
    EqPresetCallback onEqPreset_;
    EqCustomCallback onEqCustom_;
    SimpleCallback onMenu_;
    SimpleCallback onPower_;
};
