# XM4 Mini

A minimal native Windows companion app for the **Sony WH-1000XM4** — ANC /
Ambient / Off, ambient level, Focus on Voice, Sony EQ presets + custom 5-band
EQ + Clear Bass, and battery percentage with a learned time-left estimate.

One small EXE, no Qt, no daemon, no installer. It talks to the headphones
directly over Bluetooth RFCOMM using Sony's V1 protocol, and lives in the
system tray.

## Features

- **Listening mode** — Off / Noise Cancelling / Ambient Sound, with ambient
  level 1–20 and Focus on Voice toggle
- **Quick pills** — Focus / Office / Aware one-tap ambient shortcuts
- **Equalizer** — all 10 Sony presets, custom 5-band EQ, Clear Bass −10…+10
- **Battery** — live percentage plus a time-left estimate learned from your
  own discharge history (least-squares fit over the last 24 h of samples,
  stored in `%APPDATA%\XM4Mini\battery.csv`)
- **Status** — codec and firmware version in the footer
- Tray icon with quick ANC/Ambient/Off menu and battery tooltip

## Requirements

- Windows 10 or 11 (64-bit)
- WH-1000XM4 paired and connected over classic Bluetooth

## Build

```powershell
cmake -S . -B build -A x64
cmake --build build --config Release
```

The portable zip (`xm4mini.exe` + `assets/`) is produced by the
`build` GitHub Actions workflow.

## Protocol

The Bluetooth framing, session handling and V1 command set are vendored from
[Sound-connect](https://github.com/vkhawse2/Sound-connect) (see `VENDOR.md`),
which is MIT licensed. XM4 Mini only ever speaks protocol V1 — it never sends
V2 frames (opcode `0x22` is battery-query on V2 but **power-off** on V1).

## License

MIT — see `LICENSE`.
