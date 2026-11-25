#pragma once

/**
 * @file hidChannel.h
 * @brief Канал HID без report ID в данных; подменяется в тестах протокола.
 *
 * Интерфейс вынесен из `harmonyHidReader.h`, чтобы читатель файлового протокола зависел от
 * канала, а не от читателя classic HID: протоколы у них разные, канал один.
 */

#include "harmony/core/error.h"

#include <array>
#include <cstdint>
#include <memory>
#include <span>
#include <vector>

namespace harmony::transport {

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

} // namespace harmony::transport
