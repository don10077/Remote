#pragma once

#include "harmony/transport/hidChannel.h"
#include "remoteLink.h"
#include <array>
#include <span>

namespace harmony::transport {

/// Исходная копия для исследования формата, не документ редактора Harmony 900.
struct HidBackup {
    RemoteInfo info;
    std::uint32_t configBase = 0; ///< Фактический адрес конфигурации для этой архитектуры.
    std::vector<std::uint8_t> identityReport;
    std::vector<std::uint8_t> config;
};

/// Читает identity и flash архитектур 12 (One) и 14 (600/650/665/700).
/// Команд стирания, программирования, сброса или установки часов здесь нет.
class HarmonyHidReader {
public:
    explicit HarmonyHidReader(HidChannel& channel) : channel_(channel) {}
    /// Читает исходные байты; неизвестные skin/архитектура отклоняются до чтения flash.
    Result<HidBackup> read(const ProgressHandler& progress = {});

private:
    /// Читает ограниченный диапазон flash с проверкой длины и последовательности пакетов.
    Result<std::vector<std::uint8_t>> readFlash(std::uint32_t address, std::size_t size,
                                               const ProgressHandler& progress);
    HidChannel& channel_;
    bool legacyProtocol_ = false;
};

} // namespace harmony::transport
