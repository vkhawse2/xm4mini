#pragma once
// Tiny INI-backed settings in %APPDATA%/XM4Mini. No registry, no JSON.

#include <array>
#include <string>

struct AppSettings {
    int eqPreset = 0x00;                       // EqualizerPreset byte; 0x00 = Off
    std::array<int, 5> eqBands{0, 0, 0, 0, 0};  // -10..10
    int clearBass = 0;                         // -10..10
    int ambientLevel = 8;                      // 1..20
    bool focusOnVoice = false;
    int batteryHealth = 100;                   // 50..100, scales the runtime model
};

std::wstring appDataDir();  // %APPDATA%/XM4Mini (created on demand)
AppSettings loadSettings();
void saveSettings(const AppSettings& s);
