#pragma once
// DeviceLink: owns the whole Bluetooth conversation on a single worker thread.
//
// The UI thread must never block on Bluetooth I/O, so every session call
// (discovery, connect, GET/SET) runs here. State snapshots are pushed to the
// UI through a callback (the app forwards them with PostMessage).
//
// Reused Sound-connect pieces (see VENDOR.md):
//   WindowsBluetoothConnector -> BluetoothConnectorTransport
//   -> SonyProtocolSession -> ProtocolV1 (WH-1000XM4 speaks V1 only).

#include <array>
#include <atomic>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "sony/protocol/SemanticTypes.h"

struct DeviceState {
    bool connected = false;
    std::string name = "WH-1000XM4";
    int battery = -1;  // percent, -1 = unknown
    bool charging = false;
    sony::protocol::NoiseControlState nc;
    sony::protocol::EqualizerState eq;
    std::string codec;
    std::string firmware;
};

class DeviceLink {
public:
    using StateCallback = std::function<void(const DeviceState&)>;

    DeviceLink();
    ~DeviceLink();

    DeviceLink(const DeviceLink&) = delete;
    DeviceLink& operator=(const DeviceLink&) = delete;

    void setStateCallback(StateCallback cb);
    void start();  // launches the worker (auto-connect loop)
    void stop();

    // Thread-safe; executed on the worker thread.
    void asyncConnectNow();  // force a (re)connect attempt right away
    void asyncDisconnect();
    void setNoiseControl(sony::protocol::NoiseControlState st);
    void setEqPreset(int preset);
    void setEqCustom(int clearBass, const std::array<int, 5>& bands);
    void refreshBattery();  // poll the battery now instead of waiting

private:
    void workerLoop();
    bool tryConnectOnce();
    void teardown();
    void publish(const DeviceState& st);
    void processCommands();

    struct Command {
        enum class Kind { ConnectNow, Disconnect, SetNc, SetEqPreset, SetEqCustom, RefreshBattery };
        Kind kind;
        sony::protocol::NoiseControlState nc;
        int eqPreset = 0;
        int eqClearBass = 0;
        std::array<int, 5> eqBands{0, 0, 0, 0, 0};
    };

    std::thread worker_;
    std::atomic<bool> running_{false};
    std::mutex cmdMtx_;
    std::vector<Command> commands_;

    std::mutex stateMtx_;
    DeviceState state_;
    StateCallback callback_;

    // Worker-thread-owned session objects (never touched from the UI thread).
    struct Session;
    std::unique_ptr<Session> session_;
};
