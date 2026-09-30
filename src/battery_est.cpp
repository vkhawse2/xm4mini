// Battery runtime model — direct C++ port of the "Battery Runtime
// Estimator" calculator's math.

#include "battery_est.h"

#include <windows.h>
#include <objbase.h>
#include <propkey.h>  // PROPID — must come before functiondiscoverykeys_devpkey.h
#include <functiondiscoverykeys_devpkey.h>
#include <mmdeviceapi.h>
#include <endpointvolume.h>

#include <algorithm>
#include <cmath>
#include <cwctype>
#include <string>

BatteryModelResult estimateBatteryRuntime(const BatteryModelInputs& in) {
    BatteryModelResult r;
    if (in.batteryPct < 0)
        return r;
    // Constants straight from the calculator.
    constexpr double kCapacityMah = 1000.0;
    constexpr double kBasePlaybackMa = 26.0;
    constexpr double kProcessingDrawMa = 7.3;  // ANC/Ambient DSP + mics

    const double volMult = 1.0 + (std::clamp(in.volumePct, 10, 100) - 50) * 0.004;
    const double dseeMult = in.dsee ? 1.3 : 1.0;
    const double codecMult = in.ldac ? 1.3 : 1.0;
    const double playbackDraw = kBasePlaybackMa * codecMult * dseeMult * volMult;
    const double totalDraw = playbackDraw + (in.processingOn ? kProcessingDrawMa : 0.0);
    if (!(totalDraw > 0.0))
        return r;
    const double effectiveCapacity = kCapacityMah * (std::clamp(in.healthPct, 50, 100) / 100.0);
    const double fullHours = effectiveCapacity / totalDraw;
    r.runtimeHours = (std::clamp(in.batteryPct, 0, 100) / 100.0) * fullHours;
    r.currentDrawMa = totalDraw;
    r.valid = true;
    return r;
}

std::wstring formatBatteryEta(const BatteryModelResult& r, bool charging) {
    if (charging)
        return L"Charging…";
    if (!r.valid)
        return L"—";
    const int totalMin = static_cast<int>(std::lround(r.runtimeHours * 60.0));
    wchar_t buf[64]{};
    swprintf_s(buf, L"≈ %d h %02d min left · ~%.0f mA", totalMin / 60, totalMin % 60,
               r.currentDrawMa);
    return buf;
}

int xm4EndpointVolumePct() {
    int vol = 50;  // fallback when the endpoint is not present
    IMMDeviceEnumerator* enumerator = nullptr;
    if (FAILED(CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL,
                                __uuidof(IMMDeviceEnumerator),
                                reinterpret_cast<void**>(&enumerator))) ||
        !enumerator)
        return vol;

    IMMDeviceCollection* coll = nullptr;
    if (SUCCEEDED(enumerator->EnumAudioEndpoints(eRender, DEVICE_STATE_ACTIVE, &coll)) && coll) {
        UINT count = 0;
        if (SUCCEEDED(coll->GetCount(&count))) {
            for (UINT i = 0; i < count; ++i) {
                IMMDevice* dev = nullptr;
                if (FAILED(coll->Item(i, &dev)) || !dev)
                    continue;
                bool match = false;
                IPropertyStore* ps = nullptr;
                if (SUCCEEDED(dev->OpenPropertyStore(STGM_READ, &ps)) && ps) {
                    PROPVARIANT pv;
                    PropVariantInit(&pv);
                    if (SUCCEEDED(ps->GetValue(PKEY_Device_FriendlyName, &pv)) &&
                        pv.vt == VT_LPWSTR && pv.pwszVal) {
                        std::wstring name(pv.pwszVal);
                        std::transform(name.begin(), name.end(), name.begin(),
                                       [](wchar_t c) { return std::towlower(c); });
                        match = name.find(L"wh-1000xm4") != std::wstring::npos;
                    }
                    PropVariantClear(&pv);
                    ps->Release();
                }
                if (match) {
                    IAudioEndpointVolume* ev = nullptr;
                    if (SUCCEEDED(dev->Activate(__uuidof(IAudioEndpointVolume), CLSCTX_ALL,
                                                nullptr, reinterpret_cast<void**>(&ev))) &&
                        ev) {
                        float scalar = 0.5f;
                        if (SUCCEEDED(ev->GetMasterVolumeLevelScalar(&scalar)))
                            vol = std::clamp(static_cast<int>(std::lround(scalar * 100.0f)), 10,
                                             100);
                        ev->Release();
                    }
                    dev->Release();
                    break;
                }
                dev->Release();
            }
        }
        coll->Release();
    }
    enumerator->Release();
    return vol;
}
