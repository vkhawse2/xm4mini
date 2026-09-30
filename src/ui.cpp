#include "ui.h"

#include <gdiplus.h>

#include <algorithm>
#include <cmath>
#include <string_view>

#include "sony/protocol/EqualizerPresets.h"

namespace {

constexpr int kClientW = 340;

struct Brush {
    HBRUSH h = nullptr;
    explicit Brush(COLORREF c) : h(CreateSolidBrush(c)) {}
    ~Brush() { if (h) DeleteObject(h); }
};
struct Pen {
    HPEN h = nullptr;
    Pen(int w, COLORREF c) : h(CreatePen(PS_SOLID, w, c)) {}
    ~Pen() { if (h) DeleteObject(h); }
};

std::wstring widen(std::string_view s) {
    return std::wstring(s.begin(), s.end());  // preset/codec names are ASCII
};

void fillRect(HDC hdc, const RECT& rc, COLORREF c) {
    Brush b(c);
    FillRect(hdc, &rc, b.h);
}

// --- Antialiased GDI+ drawing -------------------------------------------
// All icon/shape work goes through these so edges are smooth at any DPI.
// (The old raw-GDI versions drew jagged, muddy-looking glyphs.)

inline Gdiplus::Color gcol(COLORREF c) {
    return Gdiplus::Color(255, GetRValue(c), GetGValue(c), GetBValue(c));
}

struct Gfx {
    Gdiplus::Graphics g;
    explicit Gfx(HDC hdc) : g(hdc) {
        g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
        g.SetPixelOffsetMode(Gdiplus::PixelOffsetModeHalf);
    }
};

struct GpPen {
    Gdiplus::Pen p;
    GpPen(float w, COLORREF c) : p(gcol(c), w) {
        p.SetStartCap(Gdiplus::LineCapRound);
        p.SetEndCap(Gdiplus::LineCapRound);
        p.SetLineJoin(Gdiplus::LineJoinRound);
    }
};

static void addRoundedRect(Gdiplus::GraphicsPath& path, const RECT& rc, int radius) {
    const float x = static_cast<float>(rc.left), y = static_cast<float>(rc.top);
    const float w = static_cast<float>(rc.right - rc.left);
    const float h = static_cast<float>(rc.bottom - rc.top);
    float d = static_cast<float>(radius);
    if (d > w)
        d = w;
    if (d > h)
        d = h;
    if (d < 2.0f) {
        path.AddRectangle(Gdiplus::RectF(x, y, w, h));
        return;
    }
    path.AddArc(x, y, d, d, 180.0f, 90.0f);
    path.AddArc(x + w - d, y, d, d, 270.0f, 90.0f);
    path.AddArc(x + w - d, y + h - d, d, d, 0.0f, 90.0f);
    path.AddArc(x, y + h - d, d, d, 90.0f, 90.0f);
    path.CloseFigure();
}

void roundCard(HDC hdc, const RECT& rc, int radius, COLORREF fill, COLORREF border) {
    Gfx gfx(hdc);
    Gdiplus::GraphicsPath path;
    addRoundedRect(path, rc, radius);
    Gdiplus::SolidBrush b(gcol(fill));
    gfx.g.FillPath(&b, &path);
    Gdiplus::Pen p(gcol(border), 1.0f);
    gfx.g.DrawPath(&p, &path);
}

void circle(HDC hdc, const RECT& rc, COLORREF fill, COLORREF border, int borderW) {
    Gfx gfx(hdc);
    const float x = static_cast<float>(rc.left), y = static_cast<float>(rc.top);
    const float w = static_cast<float>(rc.right - rc.left);
    const float h = static_cast<float>(rc.bottom - rc.top);
    Gdiplus::SolidBrush b(gcol(fill));
    gfx.g.FillEllipse(&b, x, y, w, h);
    if (borderW > 0) {
        GpPen p(static_cast<float>(borderW), border);
        const float hw = borderW / 2.0f;
        gfx.g.DrawEllipse(&p.p, x + hw, y + hw, w - borderW, h - borderW);
    }
}

}  // namespace

Ui::Ui(HWND hwnd) : hwnd_(hwnd) {
    dpi_ = GetDpiForWindow(hwnd_);
    if (dpi_ <= 0)
        dpi_ = 96;
    c_.bg = RGB(0x0c, 0x0c, 0x0e);
    c_.card = RGB(0x17, 0x17, 0x1b);
    c_.cardBorder = RGB(0x24, 0x24, 0x29);
    c_.text = RGB(0xf0, 0xf0, 0xf0);
    c_.muted = RGB(0x8e, 0x8e, 0x93);
    c_.faint = RGB(0x6e, 0x6e, 0x74);
    c_.amber = RGB(0xf0, 0xa2, 0x2e);
    c_.amberLight = RGB(0xf5, 0xb5, 0x4a);
    c_.green = RGB(0x35, 0xd0, 0x5a);
    c_.trackBg = RGB(0x33, 0x33, 0x38);
    c_.circleBorder = RGB(0x3c, 0x3c, 0x42);
    makeFonts();

    // Headphone render next to the exe (shipped in assets/).
    wchar_t exe[MAX_PATH]{};
    GetModuleFileNameW(nullptr, exe, MAX_PATH);
    std::wstring dir = exe;
    const size_t slash = dir.find_last_of(L"\\/");
    if (slash != std::wstring::npos)
        dir.resize(slash);
    Gdiplus::Bitmap* bmp = Gdiplus::Bitmap::FromFile((dir + L"\\assets\\wh-1000xm4.png").c_str());
    if (bmp && bmp->GetLastStatus() == Gdiplus::Ok)
        hpImg_ = bmp;
    else
        delete bmp;
}

Ui::~Ui() {
    delete hpImg_;
    for (HFONT f : {fTitle_, fNormal_, fSmall_, fTiny_, fBold_})
        if (f)
            DeleteObject(f);
}

void Ui::makeFonts() {
    for (HFONT f : {fTitle_, fNormal_, fSmall_, fTiny_, fBold_})
        if (f)
            DeleteObject(f);
    auto mk = [this](int px, bool bold) {
        return CreateFontW(-S(px), 0, 0, 0, bold ? FW_BOLD : FW_NORMAL, FALSE, FALSE, FALSE,
                           DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                           CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
    };
    fTitle_ = mk(20, true);
    fNormal_ = mk(13, false);
    fSmall_ = mk(12, false);
    fTiny_ = mk(11, false);
    fBold_ = mk(14, true);
}

void Ui::onDpiChanged() {
    dpi_ = GetDpiForWindow(hwnd_);
    if (dpi_ <= 0)
        dpi_ = 96;
    makeFonts();
}

int Ui::S(int px) const { return MulDiv(px, dpi_, 96); }

void Ui::setCallbacks(NcCallback nc, EqPresetCallback eqp, EqCustomCallback eqc,
                      SimpleCallback menu, SimpleCallback power) {
    onNc_ = std::move(nc);
    onEqPreset_ = std::move(eqp);
    onEqCustom_ = std::move(eqc);
    onMenu_ = std::move(menu);
    onPower_ = std::move(power);
}

void Ui::setDeviceState(const DeviceState& st) {
    dev_ = st;
    if (st.connected) {
        ncUi_ = st.nc;
        eqUi_ = st.eq;
    }
}

void Ui::setBatteryText(const std::wstring& t) { batteryText_ = t; }

void Ui::applySettings(const AppSettings& s) {
    ncUi_.mode = sony::protocol::NoiseControlMode::Off;
    ncUi_.ambientLevel = s.ambientLevel;
    ncUi_.focusOnVoice = s.focusOnVoice;
    eqUi_.preset = s.eqPreset;
    eqUi_.bands = s.eqBands;
    eqUi_.clearBass = s.clearBass;
}

AppSettings Ui::currentSettings() const {
    AppSettings s;
    s.ambientLevel = ncUi_.ambientLevel;
    s.focusOnVoice = ncUi_.focusOnVoice;
    s.eqPreset = eqUi_.preset;
    s.eqBands = eqUi_.bands;
    s.clearBass = eqUi_.clearBass;
    return s;
}

// ---------------------------------------------------------------- layout ---

void Ui::layout() {
    hits_.clear();
    auto addHit = [this](HitId id, const RECT& rc, int data = 0) {
        hits_.push_back(Hit{id, data, rc});
    };

    const int W = S(kClientW);
    const int pad = S(18);
    int y = S(16);

    // Header text (title only; the hamburger sits top-left beside it)
    y += S(44);

    // Menu (top-left) / power (top-right): exact mirror images
    {
        const int d = S(34);
        RECT rcPower{ W - pad - d, S(16), W - pad, S(16) + d };
        RECT rcMenu{ pad, S(16), pad + d, S(16) + d };
        addHit(HitMenu, rcMenu);
        addHit(HitPower, rcPower);
    }

    // Device row
    const int rowY = y;
    y += S(142);

    if (!dev_.connected) {
        contentH_ = y + S(44);  // room for the hint line below
        return;
    }

    // Listening mode label + circles
    y += S(8) + S(18) + S(10);  // caption line + breathing room
    const int circD = S(52);
    const int inset = S(10);  // outer circles sit slightly inward
    const int gap = (W - (pad + inset) * 2 - circD * 3) / 2;  // even gaps
    int cx = pad + inset;
    const HitId modes[3] = {HitModeAnc, HitModeAmbient, HitModeOff};
    for (int i = 0; i < 3; ++i) {
        RECT rc{ cx, y, cx + circD, y + circD };
        addHit(modes[i], rc);
        cx += circD + gap;
    }
    y += circD + S(26);  // labels

    const bool ambient = ncUi_.mode == sony::protocol::NoiseControlMode::Ambient;
    if (ambient) {
        // Ambient card
        const int cardH = S(118);
        RECT card{ pad, y, W - pad, y + cardH };
        addHit(HitAmbSlider, RECT{ card.left + S(14), y + S(44), card.right - S(14), y + S(70) });
        addHit(HitFovToggle, RECT{ card.right - S(14) - S(44), y + S(78), card.right - S(14), y + S(103) });
        y += cardH + S(10);
        // Pills
        const int pw = (W - pad * 2 - S(16)) / 3;
        for (int i = 0; i < 3; ++i) {
            RECT rc{ pad + i * (pw + S(8)), y, pad + i * (pw + S(8)) + pw, y + S(40) };
            addHit(HitPill, rc, i);
        }
        y += S(40) + S(10);
    }

    // EQ card
    {
        const int cardX = pad, cardW = W - pad * 2;
        const int headH = S(48);
        addHit(HitEqHeader, RECT{ cardX, y, cardX + cardW, y + headH });
        y += headH;
        if (eqExpanded_) {
            // Compact dropdown: a 3-row viewport over the preset list,
            // the rest reachable via the scrollbar / mouse wheel.
            const auto& presets = sony::protocol::equalizerPresets();
            const int rowH = S(36);
            const int visRows = 3;
            const int listH = rowH * visRows;
            const int contentH = (int)presets.size() * rowH;
            const int maxScroll = std::max(0, contentH - listH);
            eqScroll_ = std::clamp(eqScroll_, 0, maxScroll);
            eqListX_ = cardX;
            eqListY_ = y;
            eqListW_ = cardW;
            eqListH_ = listH;
            const int scrollW = maxScroll > 0 ? S(12) : 0;
            for (size_t i = 0; i < presets.size(); ++i) {
                const int ry0 = y + (int)i * rowH - eqScroll_;
                const int ry1 = ry0 + rowH;
                if (ry1 <= y || ry0 >= y + listH)
                    continue;  // fully outside the viewport
                RECT rc{ cardX, ry0, cardX + cardW - scrollW, ry1 };
                addHit(HitEqPreset, rc, static_cast<int>(presets[i].preset));
            }
            if (maxScroll > 0)
                addHit(HitEqScroll, RECT{ cardX + cardW - scrollW, y, cardX + cardW, y + listH });
            y += listH + S(6);
            if (eqUi_.preset == 0xa0) {  // Custom bands
                y += S(12);
                const int bw = S(26), bh = S(96);
                const int totalBands = 5;
                const int spanW = cardW - S(28);
                for (int i = 0; i < totalBands; ++i) {
                    int bx = cardX + S(14) + i * spanW / (totalBands - 1) - bw / 2;
                    if (i == 0) bx = cardX + S(14);
                    if (i == totalBands - 1) bx = cardX + cardW - S(14) - bw;
                    RECT rc{ bx, y + S(16), bx + bw, y + S(16) + bh };
                    addHit(HitBandSlider, rc, i);
                }
                y += S(16) + bh + S(34);  // db labels + freq labels
                // Clear Bass slider
                addHit(HitCbSlider, RECT{ cardX + S(14), y, cardX + cardW - S(14), y + S(26) });
                y += S(26) + S(8);
            }
            y += S(6);
        }
    }

    y += S(12);
    contentH_ = y + S(14);
    (void)rowY;
}

int Ui::contentHeight() {
    layout();
    return contentH_;
}

Ui::HitId Ui::hitTest(int x, int y, int* data) const {
    for (auto it = hits_.rbegin(); it != hits_.rend(); ++it) {
        const RECT& rc = it->rc;
        if (x >= rc.left && x < rc.right && y >= rc.top && y < rc.bottom) {
            // Preset rows are drawn through a 3-row viewport; ignore hits
            // on the parts of a row that are scrolled out of view.
            if (it->id == HitEqPreset && (y < eqListY_ || y >= eqListY_ + eqListH_))
                continue;
            if (data)
                *data = it->data;
            // When disconnected, only menu/power respond.
            if (!dev_.connected && it->id != HitMenu && it->id != HitPower)
                return HitNone;
            return it->id;
        }
    }
    return HitNone;
}

RECT Ui::eqThumbRect() const {
    const RECT empty{ 0, 0, 0, 0 };
    if (!eqExpanded_)
        return empty;
    const int rowH = S(36), listH = rowH * 3;
    const int contentH = (int)sony::protocol::equalizerPresets().size() * rowH;
    const int maxScroll = std::max(0, contentH - listH);
    if (maxScroll <= 0)
        return empty;
    const int scrollW = S(12);
    const int tx = eqListX_ + eqListW_ - scrollW;
    const int thumbH = std::max(S(24), listH * listH / contentH);
    const int thumbY = eqListY_ + eqScroll_ * (listH - thumbH) / maxScroll;
    return RECT{ tx + S(4), thumbY, tx + scrollW - S(2), thumbY + thumbH };
}

void Ui::scrollEqToPreset(int preset) {
    const auto& presets = sony::protocol::equalizerPresets();
    const int rowH = S(36), listH = rowH * 3;
    const int contentH = (int)presets.size() * rowH;
    const int maxScroll = std::max(0, contentH - listH);
    for (size_t i = 0; i < presets.size(); ++i) {
        if (static_cast<int>(presets[i].preset) == preset) {
            const int top = (int)i * rowH;
            if (top < eqScroll_)
                eqScroll_ = top;
            else if (top + rowH > eqScroll_ + listH)
                eqScroll_ = top + rowH - listH;
            break;
        }
    }
    eqScroll_ = std::clamp(eqScroll_, 0, maxScroll);
}

void Ui::onWheel(int x, int y, int delta) {
    if (!eqExpanded_ || delta == 0)
        return;
    layout();
    bool overEq = false;
    for (const Hit& ht : hits_) {
        if (ht.id == HitEqHeader || ht.id == HitEqPreset || ht.id == HitEqScroll) {
            const RECT& rc = ht.rc;
            if (x >= rc.left && x < rc.right && y >= rc.top && y < rc.bottom) {
                overEq = true;
                break;
            }
        }
    }
    if (!overEq)
        return;
    const int rowH = S(36), listH = rowH * 3;
    const int contentH = (int)sony::protocol::equalizerPresets().size() * rowH;
    const int maxScroll = std::max(0, contentH - listH);
    if (maxScroll <= 0)
        return;
    eqScroll_ = std::clamp(eqScroll_ - delta * rowH / 120, 0, maxScroll);
    InvalidateRect(hwnd_, nullptr, FALSE);
}

// ---------------------------------------------------------------- drawing ---

void Ui::drawSlider(HDC hdc, const RECT& rc, int min, int max, int value) {
    const int cy = (rc.top + rc.bottom) / 2;
    const int th = S(4);
    RECT track{ rc.left, cy - th / 2, rc.right, cy + th / 2 };
    roundCard(hdc, track, th, c_.trackBg, c_.trackBg);
    const double p = max > min ? double(value - min) / double(max - min) : 0.0;
    const int fx = rc.left + int(p * (rc.right - rc.left));
    if (fx > rc.left) {
        RECT fill{ rc.left, cy - th / 2, fx, cy + th / 2 };
        roundCard(hdc, fill, th, c_.amber, c_.amber);
    }
    const int td = S(18);
    RECT thumb{ fx - td / 2, cy - td / 2, fx + td / 2, cy + td / 2 };
    circle(hdc, thumb, RGB(0xff, 0xff, 0xff), RGB(0xff, 0xff, 0xff), 1);
}

void Ui::drawToggle(HDC hdc, const RECT& rc, bool on) {
    roundCard(hdc, rc, (rc.bottom - rc.top), on ? c_.amber : RGB(0x3a, 0x3a, 0x40),
              on ? c_.amber : RGB(0x3a, 0x3a, 0x40));
    const int d = (rc.bottom - rc.top) - S(5);
    const int x = on ? rc.right - S(3) - d : rc.left + S(3);
    const int cy = (rc.top + rc.bottom) / 2;
    RECT knob{ x, cy - d / 2, x + d, cy + d / 2 };
    circle(hdc, knob, RGB(0xff, 0xff, 0xff), RGB(0xff, 0xff, 0xff), 1);
}

void Ui::drawModeIcon(HDC hdc, const RECT& rc, HitId which, bool active) {
    const COLORREF col = active ? RGB(0x1a, 0x1a, 0x1a) : RGB(0xcf, 0xcf, 0xcf);
    Gfx gfx(hdc);
    GpPen p(static_cast<float>(S(12)) / 5.0f, col);  // 2.4px at 96 dpi, bolder glyphs
    const float cx = (rc.left + rc.right) / 2.0f, cy = (rc.top + rc.bottom) / 2.0f;
    if (which == HitModeOff) {
        const float r = static_cast<float>(S(8));
        gfx.g.DrawEllipse(&p.p, cx - r, cy - r, r * 2.0f, r * 2.0f);
    } else if (which == HitModeAnc) {
        // waveform bars, tallest in the middle, with a diagonal slash: sound, cancelled
        const float xs[5] = { -S(6.0f), -S(3.0f), 0.0f, S(3.0f), S(6.0f) };
        const float hs[5] = { S(2.0f), S(4.5f), S(6.8f), S(4.5f), S(2.0f) };
        for (int i = 0; i < 5; ++i)
            gfx.g.DrawLine(&p.p, cx + xs[i], cy - hs[i], cx + xs[i], cy + hs[i]);
        const float s = static_cast<float>(S(7.2));
        gfx.g.DrawLine(&p.p, cx - s, cy - s, cx + s, cy + s);
    } else {
        // ambient: three air-flow streamlines with curled ends, sound flowing in
        auto windLine = [&](float x0, float y, float x1, float r, bool hookUp) {
            gfx.g.DrawLine(&p.p, x0, y, x1 - r, y);
            if (hookUp)
                gfx.g.DrawArc(&p.p, x1 - 2.0f * r, y - 2.0f * r, 2.0f * r, 2.0f * r,
                              90.0f, 180.0f);
            else
                gfx.g.DrawArc(&p.p, x1 - 2.0f * r, y, 2.0f * r, 2.0f * r,
                              270.0f, -180.0f);
        };
        windLine(cx - S(9.2f), cy - S(4.5f), cx + S(5.8f), S(2.8f), true);
        windLine(cx - S(9.2f), cy + S(0.5f), cx + S(1.8f), S(2.2f), true);
        windLine(cx - S(9.2f), cy + S(5.0f), cx + S(5.8f), S(2.8f), false);
    }
}

static void drawText(HDC hdc, HFONT f, COLORREF col, int x, int y, int w, int h,
                     const std::wstring& s, UINT fmt = DT_LEFT) {
    SetBkMode(hdc, TRANSPARENT);
    SetTextColor(hdc, col);
    HGDIOBJ old = SelectObject(hdc, f);
    RECT rc{ x, y, x + w, y + h };
    DrawTextW(hdc, s.c_str(), -1, &rc, fmt | DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS);
    SelectObject(hdc, old);
}

void Ui::paint(HDC hdc) {
    layout();
    const int W = S(kClientW);
    const int pad = S(18);

    RECT full{ 0, 0, W, contentH_ };
    fillRect(hdc, full, c_.bg);

    int y = S(16);
    const int titleX = pad + S(34) + S(12);  // clears the top-left hamburger
    drawText(hdc, fTitle_, c_.text, titleX, y, S(200), S(28), L"WH-1000XM4");

    // Menu / power glyphs (no rings)
    for (const Hit& ht : hits_) {
        if (ht.id == HitMenu || ht.id == HitPower) {
            const float cx = (ht.rc.left + ht.rc.right) / 2.0f;
            const float cy = (ht.rc.top + ht.rc.bottom) / 2.0f;
            Gfx gfx(hdc);
            GpPen p(static_cast<float>(S(2)), RGB(0xd7, 0xd7, 0xd7));
            if (ht.id == HitMenu) {
                // hamburger: three horizontal lines
                const float hw = static_cast<float>(S(7));
                for (int i = -1; i <= 1; ++i) {
                    const float yy = cy + i * static_cast<float>(S(5));
                    gfx.g.DrawLine(&p.p, cx - hw, yy, cx + hw, yy);
                }
            } else {
                // power symbol: ring with a gap at the top, plus the stem
                const float r = static_cast<float>(S(7));
                gfx.g.DrawArc(&p.p, cx - r, cy - r, r * 2.0f, r * 2.0f, 330.0f, 240.0f);
                gfx.g.DrawLine(&p.p, cx, cy - r - static_cast<float>(S(1)), cx,
                               cy + static_cast<float>(S(1)));
            }
        }
    }
    y += S(44);

    // Device row
    {
        const int rowY = y;
        // connection dot + text (dot blinks while connected, centered on the text line)
        const int dotD = S(8);
        const int dotY = rowY + (S(20) - dotD) / 2;
        RECT dot{ pad, dotY, pad + dotD, dotY + dotD };
        if (!dev_.connected || blinkOn_)
            circle(hdc, dot, dev_.connected ? c_.green : c_.faint, dev_.connected ? c_.green : c_.faint, 1);
        const wchar_t* connTxt = dev_.connected ? L"Connected" : L"Looking for WH-1000XM4…";
        drawText(hdc, fNormal_, c_.text, pad + S(14), rowY, S(170), S(20), connTxt);
        if (dev_.connected && !dev_.codec.empty()) {
            // Codec badge right after "Connected", same type size.
            SIZE tsz{};
            HGDIOBJ oldF = SelectObject(hdc, fNormal_);
            GetTextExtentPoint32W(hdc, connTxt, static_cast<int>(wcslen(connTxt)), &tsz);
            SelectObject(hdc, oldF);
            std::wstring codec = widen(dev_.codec);
            for (wchar_t& ch : codec)
                if (ch >= L'a' && ch <= L'z')
                    ch = static_cast<wchar_t>(ch - 32);
            drawText(hdc, fNormal_, c_.muted, pad + S(14) + tsz.cx + S(10), rowY,
                     S(90), S(20), codec.c_str());
        }

        // battery glyph + %
        {
            Gfx gfx(hdc);
            const float bx = static_cast<float>(pad), by = static_cast<float>(rowY + S(28));
            const float bw = static_cast<float>(S(30)), bh = static_cast<float>(S(15));
            RECT body{ pad, rowY + S(28), pad + S(30), rowY + S(28) + S(15) };
            Gdiplus::GraphicsPath path;
            addRoundedRect(path, body, S(6));
            Gdiplus::SolidBrush bg(gcol(c_.bg));
            gfx.g.FillPath(&bg, &path);
            GpPen pen(1.5f, c_.muted);
            gfx.g.DrawPath(&pen.p, &path);
            // nub
            Gdiplus::SolidBrush nb(gcol(c_.muted));
            gfx.g.FillRectangle(&nb, bx + bw + 1.0f, by + bh / 2.0f - static_cast<float>(S(3)),
                                static_cast<float>(S(3)), static_cast<float>(S(6)));
            if (dev_.connected && dev_.battery >= 0) {
                const float fw = (bw - static_cast<float>(S(6))) * dev_.battery / 100.0f;
                if (fw > 1.0f) {
                    Gdiplus::SolidBrush fb(gcol(c_.green));
                    gfx.g.FillRectangle(&fb, bx + static_cast<float>(S(3)),
                                        by + static_cast<float>(S(3)), fw,
                                        bh - static_cast<float>(S(6)));
                }
            }
        }
        std::wstring pct = (dev_.connected && dev_.battery >= 0)
                               ? std::to_wstring(dev_.battery) + L"%"
                               : L"—";
        drawText(hdc, fBold_, c_.text, pad + S(38), rowY + S(24), S(90), S(24), pct);
        drawText(hdc, fTiny_, c_.muted, pad, rowY + S(52), S(170), S(16), batteryText_);

        // Headphone render, right side of the device row.
        if (hpImg_) {
            Gdiplus::Graphics g(hdc);
            g.SetInterpolationMode(Gdiplus::InterpolationModeHighQualityBicubic);
            const int isz = S(138);
            g.DrawImage(hpImg_, W - pad - isz, rowY, isz, isz);
        }
        y += S(142);
    }

    if (!dev_.connected) {
        drawText(hdc, fSmall_, c_.faint, pad, y + S(6), W - pad * 2, S(20),
                 L"Make sure the headphones are connected over Bluetooth.", DT_CENTER);
        return;
    }

    drawText(hdc, fSmall_, c_.muted, pad, y + S(8), S(200), S(18), L"Listening mode");
    y += S(8) + S(18) + S(10);

    // Mode circles
    const wchar_t* modeNames[3] = {L"ANC", L"Ambient", L"Off"};
    const HitId modeIds[3] = {HitModeAnc, HitModeAmbient, HitModeOff};
    const sony::protocol::NoiseControlMode modes[3] = {
        sony::protocol::NoiseControlMode::NoiseCancelling,
        sony::protocol::NoiseControlMode::Ambient,
        sony::protocol::NoiseControlMode::Off};
    int mi = 0;
    for (const Hit& ht : hits_) {
        bool isMode = ht.id == HitModeOff || ht.id == HitModeAnc || ht.id == HitModeAmbient;
        if (!isMode)
            continue;
        const bool active = ncUi_.mode == modes[mi];
        circle(hdc, ht.rc, active ? c_.amber : c_.bg, active ? c_.amber : c_.circleBorder, S(2));
        drawModeIcon(hdc, ht.rc, ht.id, active);
        drawText(hdc, fSmall_, active ? c_.amberLight : c_.text, ht.rc.left - S(10), ht.rc.bottom + S(4),
                 (ht.rc.right - ht.rc.left) + S(20), S(18), modeNames[mi], DT_CENTER);
        ++mi;
    }
    y += S(52) + S(26);

    const bool ambient = ncUi_.mode == sony::protocol::NoiseControlMode::Ambient;
    if (ambient) {
        // Ambient card
        RECT card{ pad, y, W - pad, y + S(118) };
        roundCard(hdc, card, S(28), c_.card, c_.cardBorder);
        drawText(hdc, fNormal_, c_.text, card.left + S(14), y + S(11), S(170), S(22), L"Ambient sound");
        wchar_t av[32]{};
        swprintf_s(av, L"%d / 20", ncUi_.ambientLevel);
        drawText(hdc, fSmall_, RGB(0xc9, 0xc9, 0xce), card.left, y + S(13), card.right - card.left - S(14),
                 S(20), av, DT_RIGHT);
        for (const Hit& ht : hits_)
            if (ht.id == HitAmbSlider)
                drawSlider(hdc, ht.rc, 1, 20, ncUi_.ambientLevel);
        drawText(hdc, fNormal_, c_.text, card.left + S(14), y + S(78), S(170), S(24), L"Focus on voice");
        for (const Hit& ht : hits_)
            if (ht.id == HitFovToggle)
                drawToggle(hdc, ht.rc, ncUi_.focusOnVoice);
        y += S(118) + S(10);

        // Pills
        const wchar_t* pills[3] = {L"Focus", L"Office", L"Aware"};
        int pi = 0;
        for (const Hit& ht : hits_) {
            if (ht.id != HitPill)
                continue;
            const bool on = (pi == 1);  // visual only; pills are shortcuts
            roundCard(hdc, ht.rc, S(20), on ? RGB(0x2a, 0x1f, 0x0e) : c_.card,
                      on ? c_.amber : RGB(0x33, 0x33, 0x38));
            drawText(hdc, fSmall_, on ? c_.amberLight : c_.text, ht.rc.left, ht.rc.top,
                     ht.rc.right - ht.rc.left, ht.rc.bottom - ht.rc.top, pills[pi], DT_CENTER);
            ++pi;
        }
        y += S(40) + S(10);
    }

    // EQ card
    {
        const int cardX = pad, cardW = W - pad * 2;
        int ey = y + S(48);
        if (eqExpanded_) {
            ey += S(36) * 3 + S(6);  // 3-row preset viewport
            if (eqUi_.preset == 0xa0)
                ey += S(12) + S(16) + S(96) + S(34) + S(26) + S(8) + S(6);
            else
                ey += S(6);
        }
        RECT card{ cardX, y, cardX + cardW, ey };
        roundCard(hdc, card, S(28), c_.card, c_.cardBorder);
        drawText(hdc, fNormal_, c_.text, cardX + S(14), y, S(150), S(48), L"Equalizer");
        std::wstring cur = L"?";
        for (const auto& p : sony::protocol::equalizerPresets()) {
            if (static_cast<int>(p.preset) == eqUi_.preset) {
                cur = (p.preset == sony::protocol::EqualizerPreset::Manual) ? L"Custom"
                                                                            : widen(p.displayName);
                break;
            }
        }
        drawText(hdc, fNormal_, c_.text, cardX, y, cardW - S(40), S(48), cur, DT_RIGHT);
        // chevron
        {
            Gfx gfx(hdc);
            GpPen p(static_cast<float>(S(2)), c_.amber);
            const float cx = static_cast<float>(cardX + cardW - S(24));
            const float cy = static_cast<float>(y + S(24));
            const float dx = static_cast<float>(S(5)), dy = static_cast<float>(S(3));
            if (eqExpanded_) {
                gfx.g.DrawLine(&p.p, cx - dx, cy + dy, cx, cy - dy);
                gfx.g.DrawLine(&p.p, cx, cy - dy, cx + dx, cy + dy);
            } else {
                gfx.g.DrawLine(&p.p, cx - dx, cy - dy, cx, cy + dy);
                gfx.g.DrawLine(&p.p, cx, cy + dy, cx + dx, cy - dy);
            }
        }
        int ry = y + S(48);
        if (eqExpanded_) {
            const int rowH = S(36);
            const int listH = rowH * 3;
            const size_t nPresets = sony::protocol::equalizerPresets().size();
            const int contentH = (int)nPresets * rowH;
            const int maxScroll = std::max(0, contentH - listH);
            const int scrollW = maxScroll > 0 ? S(12) : 0;
            // Subtle divider between the header and the dropdown list.
            {
                Pen dp(1, c_.cardBorder);
                HGDIOBJ oldP = SelectObject(hdc, dp.h);
                MoveToEx(hdc, cardX + S(14), ry, nullptr);
                LineTo(hdc, cardX + cardW - S(14), ry);
                SelectObject(hdc, oldP);
            }
            // Rows, clipped to the 3-row viewport.
            const int savedDc = SaveDC(hdc);
            IntersectClipRect(hdc, cardX, ry, cardX + cardW - scrollW, ry + listH);
            for (const Hit& ht : hits_) {
                if (ht.id != HitEqPreset)
                    continue;
                const bool on = ht.data == eqUi_.preset;
                std::wstring name = L"?";
                for (const auto& p : sony::protocol::equalizerPresets()) {
                    if (static_cast<int>(p.preset) == ht.data) {
                        name = (p.preset == sony::protocol::EqualizerPreset::Manual)
                                   ? L"Custom"
                                   : widen(p.displayName);
                        break;
                    }
                }
                drawText(hdc, fNormal_, on ? c_.amberLight : c_.text, ht.rc.left + S(14), ht.rc.top,
                         S(220), ht.rc.bottom - ht.rc.top, name);
                if (on)
                    drawText(hdc, fNormal_, c_.amber, ht.rc.left, ht.rc.top,
                             ht.rc.right - ht.rc.left - S(16), ht.rc.bottom - ht.rc.top, L"✓", DT_RIGHT);
            }
            RestoreDC(hdc, savedDc);
            // Scrollbar.
            if (maxScroll > 0) {
                const int tx = cardX + cardW - scrollW;
                RECT track{ tx + S(4), ry, tx + scrollW - S(2), ry + listH };
                roundCard(hdc, track, S(8), c_.trackBg, c_.trackBg);
                const RECT thumb = eqThumbRect();
                roundCard(hdc, thumb, S(8), eqScrollDrag_ ? c_.amber : c_.muted,
                          eqScrollDrag_ ? c_.amber : c_.muted);
            }
            ry += listH + S(6);
            if (eqUi_.preset == 0xa0) {
                ry += S(12);
                const wchar_t* freqs[5] = {L"400 Hz", L"1 kHz", L"2.5 kHz", L"6.3 kHz", L"16 kHz"};
                int bi = 0;
                for (const Hit& ht : hits_) {
                    if (ht.id != HitBandSlider)
                        continue;
                    const int v = eqUi_.bands[bi];
                    wchar_t db[16]{};
                    swprintf_s(db, L"%s%d", v > 0 ? L"+" : (v < 0 ? L"−" : L""), std::abs(v));
                    drawText(hdc, fTiny_, c_.amberLight, ht.rc.left - S(14), ht.rc.top - S(16), S(54),
                             S(14), db, DT_CENTER);
                    // vertical track
                    const int vcx = (ht.rc.left + ht.rc.right) / 2;
                    const int tw = S(6);
                    RECT tr{ vcx - tw / 2, ht.rc.top, vcx + tw / 2, ht.rc.bottom };
                    roundCard(hdc, tr, tw, c_.trackBg, c_.trackBg);
                    // zero line
                    const int zy = (ht.rc.top + ht.rc.bottom) / 2;
                    Pen zp(1, RGB(0x4a, 0x4a, 0x4a));
                    HGDIOBJ oldP = SelectObject(hdc, zp.h);
                    MoveToEx(hdc, ht.rc.left, zy, nullptr);
                    LineTo(hdc, ht.rc.right, zy);
                    SelectObject(hdc, oldP);
                    // fill
                    const int fh = int(std::abs(v) / 10.0 * (ht.rc.bottom - ht.rc.top) / 2);
                    RECT fr;
                    if (v >= 0)
                        fr = RECT{ vcx - tw / 2, zy - fh, vcx + tw / 2, zy };
                    else
                        fr = RECT{ vcx - tw / 2, zy, vcx + tw / 2, zy + fh };
                    if (fh > 0)
                        roundCard(hdc, fr, tw, c_.amber, c_.amber);
                    // thumb at value
                    const int ty = zy - int(v / 10.0 * (ht.rc.bottom - ht.rc.top) / 2);
                    RECT th{ vcx - S(9), ty - S(9), vcx + S(9), ty + S(9) };
                    circle(hdc, th, RGB(0xff, 0xff, 0xff), RGB(0xff, 0xff, 0xff), 1);
                    drawText(hdc, fTiny_, c_.muted, ht.rc.left - S(14), ht.rc.bottom + S(4), S(54),
                             S(14), freqs[bi], DT_CENTER);
                    ++bi;
                }
                ry += S(16) + S(96) + S(34);
                drawText(hdc, fNormal_, c_.text, cardX + S(14), ry, S(170), S(24), L"Clear Bass");
                wchar_t cb[16]{};
                swprintf_s(cb, L"%s%d", eqUi_.clearBass > 0 ? L"+" : (eqUi_.clearBass < 0 ? L"−" : L""),
                           std::abs(eqUi_.clearBass));
                drawText(hdc, fSmall_, c_.amberLight, cardX, ry + S(2), cardW - S(14), S(20), cb, DT_RIGHT);
                for (const Hit& ht : hits_)
                    if (ht.id == HitCbSlider)
                        drawSlider(hdc, ht.rc, -10, 10, eqUi_.clearBass);
            }
        }
        y = ey;
    }
}

// ---------------------------------------------------------------- input ----

void Ui::setNcMode(sony::protocol::NoiseControlMode mode) {
    ncUi_.mode = mode;
    if (onNc_)
        onNc_(ncUi_);
}

void Ui::setAmbientLevel(int level) {
    level = std::clamp(level, 1, 20);
    if (ncUi_.ambientLevel == level)
        return;
    ncUi_.ambientLevel = level;
}

void Ui::setFov(bool on) {
    ncUi_.focusOnVoice = on;
    if (onNc_)
        onNc_(ncUi_);
}

void Ui::applyPill(int pill) {
    // Local shortcuts: the V1 protocol has no ambient presets, so the pills
    // just pick sensible (level, focus-on-voice) combos.
    ncUi_.mode = sony::protocol::NoiseControlMode::Ambient;
    if (pill == 0) {  // Focus
        ncUi_.ambientLevel = 8;
        ncUi_.focusOnVoice = true;
    } else if (pill == 1) {  // Office
        ncUi_.ambientLevel = 12;
        ncUi_.focusOnVoice = true;
    } else {  // Aware
        ncUi_.ambientLevel = 20;
        ncUi_.focusOnVoice = false;
    }
    if (onNc_)
        onNc_(ncUi_);
}

void Ui::setEqPresetUi(int preset) {
    eqUi_.preset = preset;
    if (onEqPreset_)
        onEqPreset_(preset);
}

void Ui::setBandValue(int band, int value) {
    value = std::clamp(value, -10, 10);
    eqUi_.bands[band] = value;
}

void Ui::setClearBass(int cb) {
    eqUi_.clearBass = std::clamp(cb, -10, 10);
}

static int sliderValueFromX(const RECT& rc, int x, int min, int max) {
    const double p = double(x - rc.left) / double(std::max(1L, rc.right - rc.left));
    return std::clamp(int(std::round(min + p * (max - min))), min, max);
}

static int bandValueFromY(const RECT& rc, int y, int min, int max) {
    const double p = 1.0 - double(y - rc.top) / double(std::max(1L, rc.bottom - rc.top));
    return std::clamp(int(std::round(min + p * (max - min))), min, max);
}

void Ui::onLButtonDown(int x, int y) {
    layout();
    int data = 0;
    const HitId id = hitTest(x, y, &data);
    switch (id) {
        case HitMenu:
            if (onMenu_)
                onMenu_();
            break;
        case HitPower:
            if (onPower_)
                onPower_();
            break;
        case HitModeOff:
            setNcMode(sony::protocol::NoiseControlMode::Off);
            break;
        case HitModeAnc:
            setNcMode(sony::protocol::NoiseControlMode::NoiseCancelling);
            break;
        case HitModeAmbient:
            setNcMode(sony::protocol::NoiseControlMode::Ambient);
            break;
        case HitAmbSlider:
            dragId_ = id;
            break;
        case HitFovToggle:
            setFov(!ncUi_.focusOnVoice);
            break;
        case HitPill:
            applyPill(data);
            break;
        case HitEqHeader:
            eqExpanded_ = !eqExpanded_;
            if (eqExpanded_)
                scrollEqToPreset(eqUi_.preset);  // reveal the active preset
            break;
        case HitEqPreset:
            setEqPresetUi(data);
            if (data != 0xa0)  // Custom keeps the list open so the bands stay reachable
                eqExpanded_ = false;
            break;
        case HitEqScroll: {
            const RECT thumb = eqThumbRect();
            const bool onThumb = x >= thumb.left && x < thumb.right && y >= thumb.top && y < thumb.bottom;
            if (onThumb) {
                eqScrollDrag_ = true;
                eqScrollDragY_ = y;
                eqScrollDragOff_ = eqScroll_;
                SetCapture(hwnd_);
            } else {
                // Click above/below the thumb pages the list.
                const int rowH = S(36), listH = rowH * 3;
                const int contentH = (int)sony::protocol::equalizerPresets().size() * rowH;
                const int maxScroll = std::max(0, contentH - listH);
                eqScroll_ = std::clamp(eqScroll_ + (y < thumb.top ? -listH : listH), 0, maxScroll);
            }
            break;
        }
        case HitCbSlider:
        case HitBandSlider:
            dragId_ = id;
            dragData_ = data;
            break;
        default:
            break;
    }
    // Fix slider drag start using the actual hit rect.
    if (dragId_ == HitAmbSlider || dragId_ == HitCbSlider || dragId_ == HitBandSlider) {
        for (const Hit& ht : hits_) {
            if (ht.id == dragId_ && ht.data == dragData_) {
                if (dragId_ == HitAmbSlider)
                    setAmbientLevel(sliderValueFromX(ht.rc, x, 1, 20));
                else if (dragId_ == HitCbSlider)
                    setClearBass(sliderValueFromX(ht.rc, x, -10, 10));
                else
                    setBandValue(dragData_, bandValueFromY(ht.rc, y, -10, 10));
                break;
            }
        }
        SetCapture(hwnd_);
    }
    InvalidateRect(hwnd_, nullptr, FALSE);
}

void Ui::onLButtonUp(int x, int y) {
    (void)x;
    (void)y;
    eqScrollDrag_ = false;
    if (dragId_ == HitAmbSlider) {
        if (onNc_)
            onNc_(ncUi_);  // commit slider value
    } else if (dragId_ == HitCbSlider || dragId_ == HitBandSlider) {
        if (onEqCustom_)
            onEqCustom_(eqUi_.clearBass, eqUi_.bands);  // commit custom EQ
    }
    dragId_ = HitNone;
    if (GetCapture() == hwnd_)
        ReleaseCapture();
    InvalidateRect(hwnd_, nullptr, FALSE);
}

void Ui::onMouseMove(int x, int y) {
    if (eqScrollDrag_) {
        const int rowH = S(36), listH = rowH * 3;
        const int contentH = (int)sony::protocol::equalizerPresets().size() * rowH;
        const int maxScroll = std::max(0, contentH - listH);
        const int thumbH = std::max(S(24), listH * listH / contentH);
        const int range = listH - thumbH;
        if (range > 0 && maxScroll > 0)
            eqScroll_ = std::clamp(eqScrollDragOff_ + (y - eqScrollDragY_) * maxScroll / range,
                                   0, maxScroll);
        InvalidateRect(hwnd_, nullptr, FALSE);
        return;
    }
    if (dragId_ == HitNone)
        return;
    for (const Hit& ht : hits_) {
        if (ht.id == dragId_ && ht.data == dragData_) {
            if (dragId_ == HitAmbSlider)
                setAmbientLevel(sliderValueFromX(ht.rc, x, 1, 20));
            else if (dragId_ == HitCbSlider)
                setClearBass(sliderValueFromX(ht.rc, x, -10, 10));
            else
                setBandValue(dragData_, bandValueFromY(ht.rc, y, -10, 10));
            break;
        }
    }
    InvalidateRect(hwnd_, nullptr, FALSE);
}
