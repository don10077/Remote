#include "harmony/transport/harmonyHidReader.h"
#include <hidapi.h>
#include <algorithm>

namespace harmony::transport {
namespace {
/// HIDAPI отвечает только за системный USB-доступ; протокол реализован в нашем reader.
class NativeHidChannel final : public HidChannel {
public:
    explicit NativeHidChannel(hid_device* device) : device_(device) {}
    ~NativeHidChannel() override { hid_close(device_); }
    Result<void> send(const std::array<std::uint8_t, 64>& report) override {
        std::array<unsigned char, 65> wire{}; // report ID = 0
        std::copy(report.begin(), report.end(), wire.begin() + 1);
        if (hid_write(device_, wire.data(), wire.size()) != static_cast<int>(wire.size()))
            return makeError(ErrorCode::Io, "Could not send HID read request");
        return {};
    }
    Result<std::vector<std::uint8_t>> receive() override {
        std::vector<std::uint8_t> bytes(64);
        const int count = hid_read_timeout(device_, bytes.data(), bytes.size(), 2000);
        if (count <= 0) return makeError(ErrorCode::Io, "HID read failed or timed out. Check the USB cable and close other remote software.");
        bytes.resize(static_cast<std::size_t>(count));
        return bytes;
    }
private:
    hid_device* device_;
};
}

Result<std::unique_ptr<HidChannel>> openHarmonyHid() {
    if (hid_init() != 0) return makeError(ErrorCode::Io, "Could not initialize USB HID");
    const std::unique_ptr<hid_device_info, decltype(&hid_free_enumeration)> devices(
        hid_enumerate(0x046d, 0), hid_free_enumeration);
    std::string path;
    bool resetUsb = false;
    for (auto* device = devices.get(); device; device = device->next) {
        const auto pid = device->product_id;
        if (pid < 0xc110 || pid > 0xc14f) continue;
        // Z-Wave и MH используют другие команды; не посылаем им classic GET_VERSION.
        if ((pid >= 0xc112 && pid <= 0xc115) || pid == 0xc124 || pid == 0xc125 ||
            pid == 0xc126 || pid == 0xc127 || pid == 0xc129 || pid == 0xc12b) continue;
        if (!device->path) continue;
        if (!path.empty()) return makeError(ErrorCode::Io, "More than one Harmony HID interface found. Connect only one remote.");
        path = device->path;
        resetUsb = pid == 0xc121; // One: инициализация USB перед GET_VERSION, как в libconcord.
    }
    if (path.empty()) return makeError(ErrorCode::NotFound, "Harmony USB HID remote not found");
    auto* device = hid_open_path(path.c_str());
    if (!device) return makeError(ErrorCode::Io, "Could not open Harmony USB HID. Check device permissions and close other remote software.");
    auto channel = std::make_unique<NativeHidChannel>(device);
    if (resetUsb) {
        // RESET_USB сбрасывает только обмен USB, не перезапускает пульт и не меняет flash.
        std::array<std::uint8_t, 64> request{};
        request[0] = 0xe1;
        request[1] = 0x01;
        if (auto sent = channel->send(request); !sent) return std::unexpected(sent.error());
    }
    return std::unique_ptr<HidChannel>(std::move(channel));
}
} // namespace harmony::transport
