#pragma once

/**
 * @file dumpFiles.h
 * @brief Помощник для тестов, которые проверяют разбор настоящих дампов.

 * Файлы берутся из `dumps/` (макрос `HARMONY_DUMPS_DIR`, задаётся в `tests/CMakeLists.txt`).
 * Папка в git не попадает, поэтому без неё тест пропускается: это регрессия на машине
 * владельца, а не проверка в CI (правило 19).
 *
 * Ожидания — только решения программы: формат по сигнатуре, модель по skin, код ошибки,
 * отказ писать на пульт, имя клавиши из словаря. Числа, привязанные к содержимому
 * конкретного файла, не утверждаются.
 */

#include "harmony/core/formats/ezhex.h"
#include "harmony/core/formats/zipArchive.h"

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

#ifndef HARMONY_DUMPS_DIR
#define HARMONY_DUMPS_DIR ""
#endif

namespace harmony::test {

using formats::Bytes;

/// Корень локальных дампов; переменная окружения позволяет проверить запуск без них.
inline std::filesystem::path dumpRoot() {
    if (const auto* path = std::getenv("HARMONY_DUMPS_DIR")) return path;
    return HARMONY_DUMPS_DIR;
}

/// '*' и '?' в имени файла. Имена с датой (`Harmony-665-20261119-1105.remote.json`)
/// сравниваются как есть, поэтому тест берёт самый свежий файл по маске и пишет, какой взял.
inline bool matchesMask(const std::string& name, const std::string& mask) {
    std::size_t i = 0, j = 0, star = std::string::npos, match = 0;
    while (j < name.size()) {
        if (i < mask.size() && (mask[i] == '?' || mask[i] == name[j])) { ++i; ++j; }
        else if (i < mask.size() && mask[i] == '*') { star = i; match = j; ++i; }
        else if (star != std::string::npos) { i = star + 1; j = ++match; }
        else return false;
    }
    while (i < mask.size() && mask[i] == '*') ++i;
    return i == mask.size();
}

/**
 * @brief Файлы дампа по папке и маске, по алфавиту (самый свежий — последний).
 * @param folder Папка внутри `dumps/`, например `EZHex` или `Json/665`.
 * @return Пути; пусто — папки или файлов нет (тест должен пропуститься).
 */
inline std::vector<std::filesystem::path> dumpFiles(const std::string& folder, const std::string& mask) {
    const auto base = dumpRoot();
    if (base.empty()) return {};
    const std::filesystem::path root = base / folder;
    if (root.empty() || !std::filesystem::exists(root)) return {};
    std::vector<std::filesystem::path> found;
    for (const auto& entry : std::filesystem::recursive_directory_iterator(root)) {
        if (!entry.is_regular_file()) continue;
        if (matchesMask(entry.path().filename().string(), mask)) found.push_back(entry.path());
    }
    std::sort(found.begin(), found.end());
    return found;
}

/// Самый свежий файл по маске.
inline std::filesystem::path dumpFile(const std::string& folder, const std::string& mask) {
    const auto files = dumpFiles(folder, mask);
    return files.empty() ? std::filesystem::path{} : files.back();
}

/// Содержимое файла.
inline Bytes readFile(const std::filesystem::path& path) {
    std::ifstream stream(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>()};
}

/// Содержимое текстового файла (XML, JSON).
inline std::string readText(const std::filesystem::path& path) {
    std::ifstream stream(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>()};
}

/**
 * @brief Архив внутри .EZHex.
 * @param path Файл дампа.
 * @return Архив; пусто — файл не читается или это не ZIP (binary HID без архива).
 */
inline formats::ZipArchive archiveOf(const std::filesystem::path& path) {
    const Bytes bytes = readFile(path);
    const auto container = formats::parseEzHex(bytes);
    if (!container) return {};
    const auto archive = formats::readZip(container->payload);
    return archive ? std::move(*archive) : formats::ZipArchive{};
}

/**
 * @brief Распакованное содержимое записи архива.
 * @param archive Архив из `archiveOf()`.
 * @param name Имя записи, например `userconfig/IrProto.bin`.
 * @return Данные; пусто — записи нет или она испорчена.
 */
inline Bytes entryData(const formats::ZipArchive& archive, const std::string& name) {
    const auto* entry = archive.find(name);
    if (entry == nullptr) return {};
    const auto data = formats::entryContent(*entry);
    return data ? std::move(*data) : Bytes{};
}

/// Содержимое записи архива как текст (XML).
inline std::string entryText(const formats::ZipArchive& archive, const std::string& name) {
    const Bytes data = entryData(archive, name);
    return {data.begin(), data.end()};
}

} // namespace harmony::test
