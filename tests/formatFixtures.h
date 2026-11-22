#pragma once

/**
 * @file formatFixtures.h
 * @brief Фикстуры форматов, собранные в коде. Тест на них зелёный на любой машине, потому что
 *        ожидания следуют из конструкции фикстуры, а не из содержимого файла (правило 19).
 *        Настоящие байты проверяются отдельно — через `dumpFiles.h`.
 */

#include "harmony/core/configDocument.h"
#include "harmony/core/formats/container.h"
#include "harmony/core/formats/ezhex.h"
#include "harmony/core/formats/irTables.h"

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <random>
#include <string>

namespace harmony::test {

using formats::Bytes;

/**
 * @brief Временная папка теста; удаляется при выходе из объёма.
 *
 * Тесту нужна папка, когда проверяется чтение файла с диска. Содержимое — фикстура из кода,
 * поэтому ожидания не зависят от того, что лежит в git (правило 19).
 */
class TempFolder {
public:
    explicit TempFolder(std::string_view name) {
        std::random_device random;
        do {
            path_ = std::filesystem::temp_directory_path() /
                    ("harmony-test-" + std::string(name) + "-" + std::to_string(random()));
        } while (!std::filesystem::create_directory(path_));
    }
    ~TempFolder() { std::filesystem::remove_all(path_); }
    TempFolder(const TempFolder&) = delete;
    TempFolder& operator=(const TempFolder&) = delete;

    const std::filesystem::path& path() const { return path_; }

private:
    std::filesystem::path path_;
};

/// Записывает файл фикстуры.
inline void writeFile(const std::filesystem::path& path, std::string_view content) {
    std::ofstream stream(path, std::ios::binary);
    stream.write(content.data(), static_cast<std::streamsize>(content.size()));
}

/**
 * @brief Минимальный .EZHex: архив с одним устройством и одной ИК-командой из четырёх длительностей.
 * @param architecture Архитектура в identity, как её сообщает пульт.
 * @param skin Skin в identity; модели с skin 999 в каталоге нет.
 * @return Байты файла; фикстура собирается теми же модулями, что и проверяется.
 */
inline Bytes minimalEzHex(int architecture, int skin) {
    formats::EzHex file;
    file.identity.architecture = architecture;
    file.identity.skin = skin;
    auto& data = file.payload;
    data.resize(224);
    const auto put = [&](std::size_t at, std::uint32_t value, unsigned width) {
        for (unsigned i = 0; i < width; ++i) data[at + i] = static_cast<std::uint8_t>(value >> (8 * i));
    };
    std::copy_n("GSPM", 4, data.begin());
    put(4, 0x30000 + 220, 4);
    data[9] = 20;
    std::copy_n("LWJL", 4, data.begin() + 91);
    put(12 + 4 * 5, 0x30000 + 128, 3);
    data[128] = 1;
    put(129, 0x30000 + 132, 3);
    put(133, 1, 2);
    put(135, 0x30000 + 151, 3);
    put(145, 26316, 3);
    put(148, 13158, 3);
    data[151] = 1;
    put(152, 0x30000 + 144, 3);
    data[155] = 1;
    put(156, 0x30000 + 176, 3);
    put(176, 0x8000 + 9000, 2);
    put(178, 4500, 2);
    put(180, 0x8000 + 560, 2);
    put(182, 1690, 2);
    std::copy_n("PTYY", 4, data.begin() + 220);
    put(218, formats::containerChecksum(data, 218), 2);
    return *formats::writeEzHex(file);
}

/**
 * @brief Запись протокола ИК, собранная в коде: заголовок, секции таймингов, указатели.
 *
 * Указатели — относительные, как их отдаёт `parseIrProto()`. Фикстура строится вручную, чтобы
 * ожидание в тесте следовало из её конструкции, а не из чужого файла.
 * @param marker Байт, которым записи различаются.
 * @param sections Число секций таймингов.
 */
inline Bytes protocolEntry(std::uint8_t marker, std::size_t sections = 1) {
    constexpr std::size_t kProtocolHeader = 7;   ///< 01, несущая (2), 00, 00, 32, число секций
    constexpr std::size_t kSectionHeader = 16;   ///< Заголовок секции таймингов
    constexpr std::size_t kSectionPointers = 10; ///< Смещение трёх указателей внутри секции
    const std::size_t timings = 4;               // данные должны остаться внутри записи
    Bytes entry(kProtocolHeader + 2 * sections + sections * (kSectionHeader + timings), 0);

    const auto put16 = [&](std::size_t at, std::uint32_t value) {
        entry[at] = static_cast<std::uint8_t>(value & 0xFF);
        entry[at + 1] = static_cast<std::uint8_t>((value >> 8) & 0xFF);
    };
    entry[0] = 0x01;
    entry[1] = 0x4B;                            // несущая
    entry[2] = static_cast<std::uint8_t>(marker);
    entry[5] = 0x32;
    entry[6] = static_cast<std::uint8_t>(sections);

    for (std::size_t i = 0; i < sections; ++i) {
        const std::size_t section = kProtocolHeader + 2 * sections + i * (kSectionHeader + timings);
        put16(kProtocolHeader + 2 * i, static_cast<std::uint32_t>(section));
        put16(section + kSectionPointers, static_cast<std::uint32_t>(section + kSectionHeader));
        put16(section + kSectionPointers + 2, static_cast<std::uint32_t>(section + kSectionHeader));
        put16(section + kSectionPointers + 4, 0);   // конца кадра нет
        entry[section + kSectionHeader] = marker;
    }
    return entry;
}

/**
 * @brief Запись протокола, которую `ir::renderProtocol` может исполнить: один символ, два слова.
 *
 * Указатели — относительные, как их отдаёт `parseIrProto()`. Несущая 38 кГц (период 26316),
 * интервал кадра 40000, символ 0 — импульс 560 и пауза 560, символ 1 — импульс 1680 и пауза 560,
 * начало кадра — импульс 9000. Эти числа и есть ожидание теста: они заданы здесь.
 */
inline Bytes renderableProtocolEntry() {
    constexpr std::size_t kSection = 9;      ///< Секция сразу после указателей
    constexpr std::size_t kData = 25;        ///< Слова символа 0 и символа 1
    constexpr std::size_t kStart = 33;       ///< Блок начала кадра
    constexpr std::size_t kEnd = 36;         ///< Блок конца кадра
    Bytes entry(kEnd + 3, 0);

    const auto put = [&](std::size_t at, std::uint32_t value) {
        entry[at] = static_cast<std::uint8_t>(value & 0xFF);
        entry[at + 1] = static_cast<std::uint8_t>((value >> 8) & 0xFF);
    };
    entry[0] = 0x01;
    put(1, 26316);                                   // период несущей → 38 кГц
    entry[5] = 0x32;
    entry[6] = 1;                                    // одна секция
    put(7, kSection);
    put(kSection, 2);                                // два бита в символе
    entry[kSection + 8] = 2;                        // слов на символ — два (простой разбор)
    entry[kSection + 9] = 2;                        // символов: 0 и 1
    put(kSection + 4, 40000);                       // интервал кадра
    put(kSection + 10, kData);
    put(kSection + 12, kStart);
    put(kSection + 14, kEnd);
    put(kData, 0x8000 + 560);   // символ 0: импульс
    put(kData + 2, 560);         // символ 0: пауза
    put(kData + 4, 0x8000 + 1680); // символ 1: импульс
    put(kData + 6, 560);           // символ 1: пауза
    entry[kStart] = 1;
    put(kStart + 1, 0x8000 + 9000);
    entry[kEnd] = 1;
    put(kEnd + 1, 5000);
    return entry;
}

/// Код команды для `renderableProtocolEntry()`: протокол 0, один кадр, два бита 0 и 0.
inline const std::string kRenderableCode = "0x00000000010001000300";

/**
 * @brief Файлы таблиц, собранные в коде: IrProto.bin с одной записью и пустая SsIr.bin.
 * @param entry Запись (в относительном виде, как после чтения).
 */
inline harmony::ConfigDocument::Files codeTables(const Bytes& entry = renderableProtocolEntry()) {
    harmony::ConfigDocument::Files files;
    formats::IrTable table;
    table.entries.push_back(entry);
    files.irProto = formats::writeIrProto(table);
    files.ssIr = formats::writeSsIr(formats::IrTable{});
    return files;
}

} // namespace harmony::test
