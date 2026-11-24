#pragma once

/**
 * @file cliFiles.h
 * @brief Запись файлов консольной утилиты: через временный файл и без перезаписи чужих копий.
 */

#include <cstdint>
#include <filesystem>
#include "harmony/core/writeFileNew.h"
#include <vector>

namespace harmony::cli {

/**
 * @brief Свободное имя файла рядом с @p wanted.
 *
 * Если файла нет — само @p wanted; иначе к имени добавляется номер:
 * `backup.EZHex` → `backup-2.EZHex`, `backup-3.EZHex`…
 * @param wanted Желаемый путь.
 * @return Путь, по которому сейчас нет файла; пусто — свободного имени не нашлось (за 999 попыток).
 */
[[nodiscard]] std::filesystem::path freePath(const std::filesystem::path& wanted);

/**
 * @brief Записать файл через временный (`<имя>.tmp`) и переименовать его в @p path.
 *
 * Пока запись не закончена, на месте @p path нет недописанного файла: при сбое или
 * нехватке места временный файл удаляется, а прежний файл (если был) остаётся.
 * @param path Куда записать.
 * @param bytes Содержимое.
 * @return true — записано; false — ошибка записи или переименования.
 */
[[nodiscard]] bool writeFileAtomic(const std::filesystem::path& path, const std::vector<std::uint8_t>& bytes);

/**
 * @brief Создаёт новый файл, атомарно отказывая при существующем имени.
 * @param path Путь нового файла, включая Unicode на Windows.
 * @param bytes Содержимое файла.
 * @return true при полной записи; false при ошибке или существующем файле.
 */
using harmony::writeFileNew;

} // namespace harmony::cli
