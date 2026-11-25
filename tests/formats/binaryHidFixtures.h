#pragma once

/**
 * @file binaryHidFixtures.h
 * @brief Синтетический образ бинарного HID: команда и назначения на заданные скан-коды.
 *
 * Фикстура собирается теми же модулями, что и проверяется, поэтому ожидания следуют из её
 * конструкции, а не из содержимого файла (правило 19). Рамка контейнера берётся из таблицы
 * семейств, а не из списка архитектур в тесте.
 */

#include "formatFixtures.h"

#include <algorithm>
#include <cstdint>
#include <initializer_list>
#include <ranges>

namespace harmony::test {

/**
 * @brief Образ с одной командой и назначениями на заданные скан-коды.
 * @param architecture Семейство рамки контейнера.
 * @param skin Модель в заголовке EZHex; 999 — неизвестная.
 * @param scans Коды физических клавиш, каждая вызывает команду 0 устройства 0.
 * @return EZHex с независимыми таблицами устройств и режима, без экранных подписей.
 */
inline formats::Bytes keyFixture(int architecture, int skin, std::initializer_list<unsigned> scans) {
    auto file = *formats::parseEzHex(minimalEzHex(architecture, skin));
    auto& data = file.payload;
    data.resize(300, 0);
    const auto put = [&](std::size_t at, std::uint32_t value, unsigned width) {
        for (unsigned i = 0; i < width; ++i)
            data[at + i] = static_cast<std::uint8_t>(value >> (8 * i));
    };
    // Сигнал и устройство минимальной фикстуры остаются; рамка выбирается по семейству.
    const auto& families = formats::containerFamilies();
    const auto family = std::ranges::find(families, architecture, &formats::ContainerFamilyInfo::architecture);
    std::fill(data.begin(), data.begin() + 128, 0);
    std::ranges::copy(family->cookie, data.begin());
    data[9] = static_cast<std::uint8_t>(family->slots);
    std::ranges::copy(family->marker, data.begin() + family->markerOffset());
    put(4, 0x30000 + 296, 4);
    put(12 + 4 * 5, 0x30000 + 128, 3); // устройства: секция 5 во всех проверяемых семействах
    put(12 + 4 * 6, 0x30000 + 200, 3); // режимы: секция 6
    std::fill(data.begin() + 184, data.end(), 0);
    put(200, 1, 3);
    put(203, 0x30000 + 208, 3);
    put(209, 0x30000 + 220, 3); // trailer + 1 → таблица клавиш; страниц нет
    data[220] = static_cast<std::uint8_t>(scans.size());
    std::size_t at = 221;
    for (unsigned scan : scans) {
        data[at] = static_cast<std::uint8_t>(0x80 | scan);
        data[at + 3] = 0x7d; // команда 0 устройства 0
        at += 4;
    }
    std::ranges::copy(family->endMarker, data.begin() + 296);
    put(294, formats::containerChecksum(data, 294), 2);
    return *formats::writeEzHex(file);
}

} // namespace harmony::test
