#pragma once
// Battery runtime model, ported from the "Battery Runtime Estimator"
// calculator (capacity/draw model):
//
//   cell capacity 1000 mAh, base playback draw 26 mA,
//   +7.3 mA when the noise-processing DSP is on (ANC or Ambient),
//   x1.3 for LDAC, x1.3 for DSEE, volume scales 1 + (vol-50)*0.004,
//   usable capacity scaled by battery health.
//
// Unlike the old discharge-history fit, this gives an instant estimate
// from the live listening state — no warm-up samples, no log file.

#include <string>

struct BatteryModelInputs {
    int batteryPct = -1;        // current charge 0..100, -1 = unknown
    bool processingOn = false;  // ANC or Ambient mode (DSP + mics active)
    bool ldac = false;          // LDAC codec (otherwise AAC/SBC)
    bool dsee = false;          // DSEE upscaling (not tracked by the app; off)
    int volumePct = 50;         // Windows endpoint volume, 10..100
    int healthPct = 100;        // battery health, 50..100 (settings)
};

struct BatteryModelResult {
    double runtimeHours = 0.0;   // at the current charge
    double currentDrawMa = 0.0;  // estimated total draw
    bool valid = false;
};

BatteryModelResult estimateBatteryRuntime(const BatteryModelInputs& in);

// Human text, e.g. L"≈ 7 h 15 min left · ~34 mA".
// L"Charging…" while charging, L"—" when the estimate is not valid.
std::wstring formatBatteryEta(const BatteryModelResult& r, bool charging);

// Master volume (clamped to 10..100) of the WH-1000XM4 render endpoint.
// Falls back to 50 when the endpoint is not present.
// Requires COM initialized on the calling thread.
int xm4EndpointVolumePct();
