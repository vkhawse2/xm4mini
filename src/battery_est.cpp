#include "battery_est.h"

#include <cstdio>
#include <cwchar>

namespace {

constexpr std::time_t kWindowSecs = 24 * 3600;  // only use the last 24 h
constexpr size_t kMaxSamples = 2000;
constexpr double kMinSpanSecs = 20 * 60;        // need >= 20 min of history
constexpr double kMinDrainPerHour = 0.05;       // ignore noise below this

}  // namespace

BatteryEstimator::BatteryEstimator(const std::wstring& csvPath) : path_(csvPath) {
    load();
}

void BatteryEstimator::load() {
    samples_.clear();
    FILE* f = nullptr;
    if (_wfopen_s(&f, path_.c_str(), L"r") != 0 || !f)
        return;
    long long t = 0;
    int level = 0;
    while (fwscanf_s(f, L"%lld,%d\n", &t, &level) == 2) {
        if (level >= 0 && level <= 100)
            samples_.push_back(Sample{static_cast<std::time_t>(t), level});
        if (samples_.size() > kMaxSamples)
            break;
    }
    fclose(f);
    prune(std::time(nullptr));
}

void BatteryEstimator::prune(std::time_t now) {
    while (!samples_.empty() && now - samples_.front().t > kWindowSecs)
        samples_.erase(samples_.begin());
    while (samples_.size() > kMaxSamples)
        samples_.erase(samples_.begin());
}

void BatteryEstimator::persist() {
    FILE* f = nullptr;
    if (_wfopen_s(&f, path_.c_str(), L"w") != 0 || !f)
        return;
    for (const auto& s : samples_)
        fwprintf_s(f, L"%lld,%d\n", static_cast<long long>(s.t), s.level);
    fclose(f);
}

void BatteryEstimator::addSample(int levelPercent, bool charging) {
    if (levelPercent < 0 || levelPercent > 100)
        return;
    const std::time_t now = std::time(nullptr);
    if (charging) {
        // A charge invalidates the old drain curve; keep history only if the
        // level did not jump (i.e. it was just plugged in without charging).
        if (!samples_.empty() && levelPercent > samples_.back().level + 1) {
            samples_.clear();
            persist();
        }
        return;
    }
    prune(now);
    if (!samples_.empty()) {
        const Sample& last = samples_.back();
        if (levelPercent > last.level) {
            // Level rose while not charging (measurement noise / reconnect):
            // restart the curve rather than fit nonsense.
            samples_.clear();
        } else if (levelPercent == last.level && now - last.t < 300) {
            return;  // no new information
        }
    }
    samples_.push_back(Sample{now, levelPercent});
    if (samples_.size() > kMaxSamples) {
        samples_.erase(samples_.begin(), samples_.begin() + 500);
    }
    persist();
}

double BatteryEstimator::drainPerHour() const {
    const size_t n = samples_.size();
    if (n < 4)
        return -1.0;
    const double span = std::difftime(samples_.back().t, samples_.front().t);
    if (span < kMinSpanSecs)
        return -1.0;
    // Least-squares slope of level over time (percent per second).
    double sumT = 0, sumL = 0, sumTT = 0, sumTL = 0;
    const double t0 = static_cast<double>(samples_.front().t);
    for (const auto& s : samples_) {
        const double t = static_cast<double>(s.t) - t0;
        const double l = static_cast<double>(s.level);
        sumT += t;
        sumL += l;
        sumTT += t * t;
        sumTL += t * l;
    }
    const double denom = n * sumTT - sumT * sumT;
    if (denom <= 0)
        return -1.0;
    const double slopePerSec = (n * sumTL - sumT * sumL) / denom;
    const double drain = -slopePerSec * 3600.0;  // positive while discharging
    return drain >= kMinDrainPerHour ? drain : -1.0;
}

std::wstring BatteryEstimator::text(int levelPercent, bool charging) {
    if (levelPercent < 0 || levelPercent > 100)
        return L"—";
    if (charging)
        return L"Charging…";
    const double drain = drainPerHour();
    if (drain <= 0)
        return L"—";
    const double hours = levelPercent / drain;
    wchar_t buf[64]{};
    if (hours < 1.0) {
        swprintf_s(buf, L"≈ %d min left", static_cast<int>(hours * 60.0 + 0.5));
    } else if (hours < 24.0) {
        const int h = static_cast<int>(hours);
        const int m = static_cast<int>((hours - h) * 60.0 + 0.5);
        if (m == 60)
            swprintf_s(buf, L"≈ %d h left", h + 1);
        else
            swprintf_s(buf, L"≈ %d h %d min left", h, m);
    } else {
        swprintf_s(buf, L"≈ %d day%s left", static_cast<int>(hours / 24.0 + 0.5),
                   hours >= 48.0 ? L"s" : L"");
    }
    return buf;
}
