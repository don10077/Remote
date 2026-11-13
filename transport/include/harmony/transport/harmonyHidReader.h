#pragma once

#include "remoteLink.h"
#include <array>
#include <memory>
#include <span>

namespace harmony::transport {

/// Канал HID без report ID в данных; подменяется в тестах протокола.
class HidChannel {
public:
    virtual ~HidChannel() = default;
    /// Отправляет один 64-байтный отчёт.
    virtual Result<void> send(const std::array<std::uint8_t, 64>& report) = 0;
    /// Принимает отчёт с ограниченным временем ожидания.
    virtual Result<std::vector<std::uint8_t>> receive() = 0;
};

/// Открывает единственный классический Harmony HID; несколько устройств — ошибка.
Result<std::unique_ptr<HidChannel>> openHarmonyHid();

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
