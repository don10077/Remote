// Фаззинг usbnet::receive: входные байты — то, что «прислал пульт».
#include "harmony/transport/usbnet.h"

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace {

using harmony::ErrorCode;
using harmony::Result;
using harmony::makeError;

/// Канал, отдающий данные фаззера; когда они кончились — ошибка, как при обрыве связи.
class FuzzChannel final : public harmony::transport::Channel {
public:
    explicit FuzzChannel(std::span<const std::uint8_t> data) : data_(data) {}

    Result<void> send(std::span<const std::uint8_t> /*bytes*/) override { return {}; }

    Result<std::vector<std::uint8_t>> receive(std::size_t count,
                                              std::chrono::milliseconds /*timeout*/) override {
        if (count > data_.size()) {
            return makeError(ErrorCode::Io, "end of fuzz input");
        }
        std::vector<std::uint8_t> out(data_.begin(), data_.begin() + static_cast<std::ptrdiff_t>(count));
        data_ = data_.subspan(count);
        return out;
    }

private:
    std::span<const std::uint8_t> data_;
};

} // namespace

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) {
    namespace usbnet = harmony::transport::usbnet;
    FuzzChannel channel(std::span<const std::uint8_t>(data, size));
    // Несколько пакетов подряд, как в реальном потоке.
    for (int i = 0; i < 8; ++i) {
        auto packet = usbnet::receive(channel, std::chrono::milliseconds(0));
        if (!packet) {
            break;
        }
        (void)usbnet::encode(*packet);
        for (const auto& param : packet->params) {
            (void)usbnet::toNumber(param);
        }
    }
    return 0;
}
