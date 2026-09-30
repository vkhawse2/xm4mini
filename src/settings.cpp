#include "settings.h"

#include <windows.h>
#include <shlobj.h>

#include <cstdio>

std::wstring appDataDir() {
    wchar_t path[MAX_PATH]{};
    if (FAILED(SHGetFolderPathW(nullptr, CSIDL_APPDATA, nullptr, 0, path)))
        return L".";
    std::wstring dir = std::wstring(path) + L"\\XM4Mini";
    CreateDirectoryW(dir.c_str(), nullptr);
    return dir;
}

static std::wstring iniPath() { return appDataDir() + L"\\settings.ini"; }

AppSettings loadSettings() {
    AppSettings s;
    const std::wstring p = iniPath();
    s.eqPreset = GetPrivateProfileIntW(L"eq", L"preset", s.eqPreset, p.c_str());
    for (int i = 0; i < 5; ++i) {
        wchar_t key[16]{};
        swprintf_s(key, L"band%d", i);
        s.eqBands[i] = GetPrivateProfileIntW(L"eq", key, 0, p.c_str());
        if (s.eqBands[i] < -10) s.eqBands[i] = -10;
        if (s.eqBands[i] > 10) s.eqBands[i] = 10;
    }
    s.clearBass = GetPrivateProfileIntW(L"eq", L"clearBass", 0, p.c_str());
    if (s.clearBass < -10) s.clearBass = -10;
    if (s.clearBass > 10) s.clearBass = 10;
    s.ambientLevel = GetPrivateProfileIntW(L"nc", L"ambientLevel", 8, p.c_str());
    if (s.ambientLevel < 1) s.ambientLevel = 1;
    if (s.ambientLevel > 20) s.ambientLevel = 20;
    s.focusOnVoice = GetPrivateProfileIntW(L"nc", L"focusOnVoice", 0, p.c_str()) != 0;
    return s;
}

static void writeInt(const std::wstring& p, const wchar_t* sec, const wchar_t* key, int v) {
    wchar_t buf[32]{};
    swprintf_s(buf, L"%d", v);
    WritePrivateProfileStringW(sec, key, buf, p.c_str());
}

void saveSettings(const AppSettings& s) {
    const std::wstring p = iniPath();
    writeInt(p, L"eq", L"preset", s.eqPreset);
    for (int i = 0; i < 5; ++i) {
        wchar_t key[16]{};
        swprintf_s(key, L"band%d", i);
        writeInt(p, L"eq", key, s.eqBands[i]);
    }
    writeInt(p, L"eq", L"clearBass", s.clearBass);
    writeInt(p, L"nc", L"ambientLevel", s.ambientLevel);
    writeInt(p, L"nc", L"focusOnVoice", s.focusOnVoice ? 1 : 0);
}
