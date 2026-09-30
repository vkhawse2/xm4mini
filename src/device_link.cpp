#include "device_link.h"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <vector>

#include "sony/protocol/ProtocolV1.h"
#include "sony/protocol/SonyProtocolSession.h"
#include "sony/transport/BluetoothConnectorTransport.h"
#include "sony/transport/DeviceAddress.h"
#include "windows/WindowsBluetoothConnector.h"

namespace {

constexpr const char* kTargetName = "WH-1000XM4";
constexpr std::chrono::seconds kRetryInterval{15};
constexpr std::chrono::seconds kBatteryPollInterval{60};

std::string toLower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}

}  // namespace

struct DeviceLink::Session {
    std::unique_ptr<sony::protocol::SonyProtocolSession> session;
    std::unique_ptr<sony::protocol::ProtocolV1> proto;
    IBluetoothConnector* connector = nullptr;  // owned by the transport
};

DeviceLink::DeviceLink() = default;

DeviceLink::~DeviceLink() { stop(); }

void DeviceLink::setStateCallback(StateCallback cb) {
    std::lock_guard<std::mutex> lock(stateMtx_);
    callback_ = std::move(cb);
}

void DeviceLink::publish(const DeviceState& st) {
    StateCallback cb;
    {
        std::lock_guard<std::mutex> lock(stateMtx_);
        state_ = st;
        cb = callback_;
    }
    if (cb)
        cb(st);
}

void DeviceLink::start() {
    if (running_.exchange(true))
        return;
    worker_ = std::thread([this] { workerLoop(); });
}

void DeviceLink::stop() {
    if (!running_.exchange(false))
        return;
    if (worker_.joinable())
        worker_.join();
    session_.reset();
}

void DeviceLink::asyncConnectNow() {
    std::lock_guard<std::mutex> lock(cmdMtx_);
    commands_.push_back(Command{Command::Kind::ConnectNow});
}

void DeviceLink::asyncDisconnect() {
    std::lock_guard<std::mutex> lock(cmdMtx_);
    commands_.push_back(Command{Command::Kind::Disconnect});
}

void DeviceLink::setNoiseControl(sony::protocol::NoiseControlState st) {
    std::lock_guard<std::mutex> lock(cmdMtx_);
    Command c{Command::Kind::SetNc};
    c.nc = st;
    commands_.push_back(c);
}

void DeviceLink::setEqPreset(int preset) {
    std::lock_guard<std::mutex> lock(cmdMtx_);
    Command c{Command::Kind::SetEqPreset};
    c.eqPreset = preset;
    commands_.push_back(c);
}

void DeviceLink::setEqCustom(int clearBass, const std::array<int, 5>& bands) {
    std::lock_guard<std::mutex> lock(cmdMtx_);
    Command c{Command::Kind::SetEqCustom};
    c.eqClearBass = clearBass;
    c.eqBands = bands;
    commands_.push_back(c);
}

void DeviceLink::refreshBattery() {
    std::lock_guard<std::mutex> lock(cmdMtx_);
    commands_.push_back(Command{Command::Kind::RefreshBattery});
}

void DeviceLink::teardown() {
    session_.reset();
    DeviceState st;
    st.connected = false;
    publish(st);
}

void DeviceLink::processCommands() {
    std::vector<Command> cmds;
    {
        std::lock_guard<std::mutex> lock(cmdMtx_);
        cmds.swap(commands_);
    }
    for (const auto& c : cmds) {
        try {
            switch (c.kind) {
                case Command::Kind::ConnectNow:
                    if (!session_)
                        tryConnectOnce();
                    break;
                case Command::Kind::Disconnect:
                    teardown();
                    break;
                case Command::Kind::SetNc:
                    if (session_) {
                        session_->proto->setNoiseControl(c.nc);
                        DeviceState st;
                        {
                            std::lock_guard<std::mutex> lock(stateMtx_);
                            state_.nc = c.nc;
                            st = state_;
                        }
                        publish(st);
                    }
                    break;
                case Command::Kind::SetEqPreset:
                    if (session_) {
                        session_->proto->setEqualizerPreset(c.eqPreset);
                        DeviceState st;
                        {
                            std::lock_guard<std::mutex> lock(stateMtx_);
                            state_.eq.preset = c.eqPreset;
                            st = state_;
                        }
                        publish(st);
                    }
                    break;
                case Command::Kind::SetEqCustom:
                    if (session_) {
                        session_->proto->setEqualizerCustom(c.eqClearBass, c.eqBands);
                        DeviceState st;
                        {
                            std::lock_guard<std::mutex> lock(stateMtx_);
                            state_.eq.preset = 0xa0;  // Manual
                            state_.eq.clearBass = c.eqClearBass;
                            state_.eq.bands = c.eqBands;
                            st = state_;
                        }
                        publish(st);
                    }
                    break;
                case Command::Kind::RefreshBattery:
                    if (session_) {
                        auto b = session_->proto->getBattery();
                        DeviceState st;
                        {
                            std::lock_guard<std::mutex> lock(stateMtx_);
                            if (b.main)
                                state_.battery = *b.main;
                            state_.charging = b.charging;
                            st = state_;
                        }
                        publish(st);
                    }
                    break;
            }
        } catch (...) {
            // Any I/O failure drops the link; the loop will reconnect.
            teardown();
        }
    }
}

bool DeviceLink::tryConnectOnce() {
    try {
        WindowsBluetoothConnector discovery;
        std::vector<BluetoothDevice> devices = discovery.getConnectedDevices();
        const BluetoothDevice* target = nullptr;
        for (const auto& d : devices) {
            if (toLower(d.name).find(toLower(kTargetName)) != std::string::npos) {
                target = &d;
                break;
            }
        }
        if (!target)
            return false;

        auto connector = std::make_unique<WindowsBluetoothConnector>();
        IBluetoothConnector* rawConnector = connector.get();
        auto transport =
            std::make_unique<sony::transport::BluetoothConnectorTransport>(std::move(connector));
        auto sess = std::make_unique<DeviceLink::Session>();
        sess->connector = rawConnector;
        sess->session =
            std::make_unique<sony::protocol::SonyProtocolSession>(std::move(transport));
        sess->session->connect(sony::transport::DeviceAddress(target->mac));

        // XM4-only app: never drive a V2 session. (Opcode 0x22 is battery GET
        // on V2 but POWER OFF on V1.)
        if (rawConnector->getProtocolVersion() != SonyProtocolVersion::V1)
            return false;

        sess->session->start();
        sess->proto = std::make_unique<sony::protocol::ProtocolV1>(*sess->session);
        sess->proto->initDevice();  // best-effort on V1

        DeviceState st;
        st.connected = true;
        st.name = target->name;
        try {
            auto b = sess->proto->getBattery();
            if (b.main)
                st.battery = *b.main;
            st.charging = b.charging;
        } catch (...) {
        }
        try {
            st.nc = sess->proto->getNoiseControl();
        } catch (...) {
        }
        try {
            st.eq = sess->proto->getEqualizer();
        } catch (...) {
        }
        try {
            st.codec = sess->proto->getCodec();
        } catch (...) {
        }
        try {
            st.firmware = sess->proto->getFirmwareVersion();
        } catch (...) {
        }
        session_ = std::move(sess);
        publish(st);
        return true;
    } catch (...) {
        session_.reset();
        return false;
    }
}

void DeviceLink::workerLoop() {
    auto lastAttempt = std::chrono::steady_clock::now() - kRetryInterval;
    auto lastPoll = std::chrono::steady_clock::now();
    while (running_) {
        processCommands();
        const auto now = std::chrono::steady_clock::now();
        if (!session_) {
            if (now - lastAttempt >= kRetryInterval) {
                lastAttempt = now;
                if (tryConnectOnce())
                    lastPoll = now;
            }
        } else {
            bool alive = false;
            try {
                alive = session_->session->isConnected();
            } catch (...) {
                alive = false;
            }
            if (!alive) {
                teardown();
                lastAttempt = now;
            } else if (now - lastPoll >= kBatteryPollInterval) {
                lastPoll = now;
                try {
                    auto b = session_->proto->getBattery();
                    DeviceState st;
                    {
                        std::lock_guard<std::mutex> lock(stateMtx_);
                        if (b.main)
                            state_.battery = *b.main;
                        state_.charging = b.charging;
                        st = state_;
                    }
                    publish(st);
                } catch (...) {
                    teardown();
                    lastAttempt = now;
                }
            }
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
}
