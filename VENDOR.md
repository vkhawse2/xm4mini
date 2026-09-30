# Vendored sources

The Bluetooth transport and Sony protocol code under `vendor/` is taken from:

- Repository: https://github.com/vkhawse2/Sound-connect
- Commit: `5624c6ed740254beea614245bae0731b9ec0ec9b` (main, 2026-09-30)

## Files

Transport (`vendor/libs/sony-transport`):

- `include/sony/transport/BluetoothConnectorTransport.h`
- `include/sony/transport/Logger.h`
- `include/sony/transport/SonyDeviceFilter.h`
- `src/BluetoothConnectorTransport.cpp`
- `src/Logger.cpp`
- `src/SonyDeviceFilter.cpp`
- `src/SonyOuiTable.h`

Protocol (`vendor/libs/sony-protocol`):

- `include/sony/protocol/FrameCodec.h`
- `include/sony/protocol/SonyProtocolSession.h`
- `include/sony/protocol/ProtocolV1.h`
- `include/sony/protocol/SemanticTypes.h`
- `include/sony/protocol/EqualizerPresets.h`
- `include/sony/protocol/DeviceProfileRegistry.h`
- `src/FrameCodec.cpp`
- `src/SonyProtocolSession.cpp`
- `src/ProtocolV1.cpp`
- `src/EqualizerPresets.cpp`
- `src/DeviceProfileRegistry.cpp`
- `src/ProtocolHelpers.h`

Windows connector (`vendor/Client`):

- `IBluetoothConnector.h`
- `ByteMagic.h`, `ByteMagic.cpp`
- `Constants.h`
- `Exceptions.h`
- `windows/WindowsBluetoothConnector.h`
- `windows/WindowsBluetoothConnector.cpp`

## What was left out

Protocol V2, `sony-core`, the daemon IPC layer, Qt UI code, JSON
dispatchers and capability discovery were deliberately excluded — the
WH-1000XM4 speaks V1 only, and XM4 Mini connects directly with no daemon.

## License

All files above are MIT licensed, Copyright (c) 2020 Nir Harel, Mor Gal,
Sem Visscher, jimzrt, guilhermealbm, and other contributors (see `LICENSE`).
They are used unmodified apart from being compiled directly into this app.
