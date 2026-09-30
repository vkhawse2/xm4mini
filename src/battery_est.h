#pragma once
// Battery time-left estimator.
//
// Keeps a small on-disk log of (timestamp, level) samples taken while the
// headphones are discharging, fits a least-squares line to the recent
// history, and converts the drain rate into "≈ 7 h 15 min left".
// Tiny by design: a capped CSV, no database, no background work.

#include <string>
#include <vector>
#include <ctime>

class BatteryEstimator {
public:
    explicit BatteryEstimator(const std::wstring& csvPath);

    // Call on every battery poll. Samples are only stored while discharging.
    void addSample(int levelPercent, bool charging);

    // Human text for the current state, e.g. L"≈ 7 h 15 min left".
    // Returns L"Charging…" while charging, L"—" when there is not
    // enough history yet.
    std::wstring text(int levelPercent, bool charging);

private:
    struct Sample {
        std::time_t t{};
        int level{};
    };

    std::wstring path_;
    std::vector<Sample> samples_;

    void load();
    void persist();
    void prune(std::time_t now);
    // Drain rate in percent per hour, > 0 while discharging. -1 when unknown.
    double drainPerHour() const;
};
