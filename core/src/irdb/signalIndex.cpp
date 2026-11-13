/**
 * @file signalIndex.cpp
 * @brief Индекс сигналов базы: первый кадр каждой команды → устройства, которые его шлют.
 */

#include "harmony/core/irdb/signalIndex.h"

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <numeric>

namespace harmony::irdb {

namespace {

void put16(std::string& out, std::uint16_t value) {
    out.push_back(static_cast<char>(value & 0xFF));
    out.push_back(static_cast<char>(value >> 8));
}

void put32(std::string& out, std::uint32_t value) {
    for (int i = 0; i < 4; ++i) {
        out.push_back(static_cast<char>((value >> (8 * i)) & 0xFF));
    }
}

/// Читатель little endian с проверкой границ.
class Reader {
public:
    explicit Reader(std::string_view data) : data_(data) {}

    bool read16(std::uint16_t& value) {
        if (at_ + 2 > data_.size()) {
            return false;
        }
        value = static_cast<std::uint16_t>(byte(0) | (byte(1) << 8));
        at_ += 2;
        return true;
    }

    bool read32(std::uint32_t& value) {
        if (at_ + 4 > data_.size()) {
            return false;
        }
        value = byte(0) | (byte(1) << 8) | (byte(2) << 16) | (byte(3) << 24);
        at_ += 4;
        return true;
    }

    [[nodiscard]] std::size_t left() const { return data_.size() - at_; }

private:
    [[nodiscard]] std::uint32_t byte(std::size_t offset) const {
        return static_cast<unsigned char>(data_[at_ + offset]);
    }

    std::string_view data_;
    std::size_t at_ = 0;
};

} // namespace

Frame SignalIndex::firstFrame(const ir::IrSignal& signal) {
    Frame frame;
    frame.reserve(signal.durations.size());
    for (const int duration : signal.durations) {
        if (duration == 0 || (frame.empty() && duration < 0)) {
            continue; // пауза до первого импульса — не часть кадра
        }
        const bool mark = duration > 0;
        const int us = std::abs(duration);
        if (mark != (frame.size() % 2 == 0)) {
            // Два импульса или две паузы подряд — склеиваем, чтобы чередование сохранилось.
            const int merged = frame.back() + us;
            if (!mark && merged > kGapUs) {
                frame.pop_back();
                break;
            }
            frame.back() = static_cast<std::uint16_t>(std::min(0xFFFF, merged));
            continue;
        }
        if (!mark && us > kGapUs) {
            break;
        }
        frame.push_back(static_cast<std::uint16_t>(std::min(0xFFFF, us)));
    }
    return frame;
}

bool SignalIndex::similar(std::span<const std::uint16_t> a, std::span<const std::uint16_t> b) {
    if (a.size() != b.size() || a.empty()) {
        return false;
    }
    for (std::size_t i = 0; i + 1 < a.size(); ++i) {
        const int p = a[i];
        const int q = b[i];
        if (std::abs(p - q) > std::max(200, std::max(p, q) / 4)) {
            return false;
        }
    }
    return true;
}

Result<SignalIndex> SignalIndex::parse(std::string_view data) {
    auto corrupt = [] {
        return makeError(ErrorCode::Format,
                         "the IR code database is corrupt: the signal index cannot be read");
    };
    Reader reader(data);
    std::uint32_t count = 0;
    if (!reader.read32(count)) {
        return corrupt();
    }
    SignalIndex index;
    index.frames_.reserve(count);
    for (std::uint32_t i = 0; i < count; ++i) {
        Entry entry;
        if (!reader.read16(entry.length) || reader.left() < entry.length * 2U) {
            return corrupt();
        }
        entry.values = index.values_.size();
        for (std::uint16_t k = 0; k < entry.length; ++k) {
            std::uint16_t value = 0;
            reader.read16(value);
            index.values_.push_back(value);
        }
        if (!reader.read32(entry.deviceCount) || reader.left() < entry.deviceCount * 4ULL) {
            return corrupt();
        }
        entry.devices = index.devices_.size();
        for (std::uint32_t k = 0; k < entry.deviceCount; ++k) {
            std::uint32_t device = 0;
            reader.read32(device);
            index.devices_.push_back(device);
        }
        // Кадры записаны по возрастанию длины: диапазон каждой длины — подряд.
        if (!index.frames_.empty() && index.frames_.back().length > entry.length) {
            return corrupt();
        }
        auto [range, added] = index.byLength_.try_emplace(entry.length, i, i + 1);
        if (!added) {
            range->second.second = i + 1;
        }
        index.frames_.push_back(entry);
    }
    return index;
}

std::vector<std::uint32_t> SignalIndex::lookup(std::span<const std::uint16_t> frame) const {
    std::vector<std::uint32_t> out;
    if (frame.size() < kMinFrame || frame.size() > 0xFFFF) {
        return out;
    }
    const auto range = byLength_.find(static_cast<std::uint16_t>(frame.size()));
    if (range == byLength_.end()) {
        return out;
    }
    for (std::size_t i = range->second.first; i < range->second.second; ++i) {
        const Entry& entry = frames_[i];
        if (similar(std::span(values_).subspan(entry.values, entry.length), frame)) {
            const auto first = devices_.begin() + static_cast<std::ptrdiff_t>(entry.devices);
            out.insert(out.end(), first, first + entry.deviceCount);
        }
    }
    std::ranges::sort(out);
    out.erase(std::unique(out.begin(), out.end()), out.end());
    return out;
}

void SignalIndexBuilder::add(std::uint32_t device, const ir::IrSignal& signal) {
    Frame frame = SignalIndex::firstFrame(signal);
    if (frame.size() < SignalIndex::kMinFrame || frame.size() > 0xFFFF) {
        return;
    }
    std::string key(reinterpret_cast<const char*>(frame.data()), frame.size() * 2);
    const auto [it, added] =
        ids_.try_emplace(std::move(key), static_cast<std::uint32_t>(frames_.size()));
    if (added) {
        frames_.push_back(&it->first);
        devices_.emplace_back();
    }
    auto& list = devices_[it->second];
    // Команды устройства идут подряд: повтор — только последний.
    if (list.empty() || list.back() != device) {
        list.push_back(device);
    }
}

std::string SignalIndexBuilder::write(const std::vector<std::uint32_t>& order) const {
    // По возрастанию длины, внутри — по значениям: при чтении диапазоны длин идут подряд.
    std::vector<std::uint32_t> sorted(frames_.size());
    std::iota(sorted.begin(), sorted.end(), 0U);
    std::ranges::sort(sorted, [this](std::uint32_t a, std::uint32_t b) {
        const std::string& left = *frames_[a];
        const std::string& right = *frames_[b];
        if (left.size() != right.size()) {
            return left.size() < right.size();
        }
        return left < right; // порядок внутри длины — любой, лишь бы постоянный
    });
    std::string out;
    put32(out, static_cast<std::uint32_t>(frames_.size()));
    std::vector<std::uint32_t> devices;
    for (const std::uint32_t id : sorted) {
        const std::string& frame = *frames_[id];
        put16(out, static_cast<std::uint16_t>(frame.size() / 2));
        for (std::size_t i = 0; i < frame.size(); i += 2) {
            std::uint16_t value = 0;
            std::memcpy(&value, frame.data() + i, 2); // байты кадра — в порядке этой машины
            put16(out, value);
        }
        devices = devices_[id];
        if (!order.empty()) {
            for (std::uint32_t& device : devices) {
                device = device < order.size() ? order[device] : device;
            }
        }
        std::ranges::sort(devices);
        devices.erase(std::unique(devices.begin(), devices.end()), devices.end());
        put32(out, static_cast<std::uint32_t>(devices.size()));
        for (const std::uint32_t device : devices) {
            put32(out, device);
        }
    }
    return out;
}

} // namespace harmony::irdb
