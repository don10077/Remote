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

#include <algorithm>
#include <filesystem>
#include <string>
#include <vector>

#ifndef HARMONY_DUMPS_DIR
#define HARMONY_DUMPS_DIR ""
#endif

namespace harmony::test {

/// '*' и '?' в имени файла. Имена с датой (`Harmony-665-20261119-1105.remote.json`) сравниваются
/// как есть, поэтому тест выбирает самый свежий файл по маске и пишет, какой взял.
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
 * @brief Файл дампа по папке и маске, самый свежий по имени.
 * @param folder Папка внутри `dumps/`, например `EZHex/harmony900` или `Json/665`.
 * @return Путь; пусто — папки или файла нет (тест должен пропуститься).
 */
inline std::filesystem::path dumpFile(const std::string& folder, const std::string& mask) {
    const std::filesystem::path root = std::filesystem::path(HARMONY_DUMPS_DIR) / folder;
    if (root.empty() || !std::filesystem::exists(root)) return {};
    std::vector<std::string> names;
    for (const auto& entry : std::filesystem::directory_iterator(root)) {
        if (!entry.is_regular_file()) continue;
        const std::string name = entry.path().filename().string();
        if (matchesMask(name, mask)) names.push_back(name);
    }
    if (names.empty()) return {};
    std::sort(names.begin(), names.end());
    return root / names.back();
}

} // namespace harmony::test
