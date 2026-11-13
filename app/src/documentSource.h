#pragma once

/**
 * @file documentSource.h
 * @brief Откуда взять конфигурацию: пустая, встроенный пример или папка на диске.
 *
 * Одно место для всех диалогов, где выбирают «основу» или «источник»:
 * создание новой конфигурации, перенос устройств из другой.
 */

#include "harmony/core/configDocument.h"

#include <QString>

#include <filesystem>

namespace harmony::app {

/// Значение источника для встроенного примера.
inline constexpr const char* kDemoSource = ":demo";

/// QString → путь файловой системы без потери кириллицы на Windows.
[[nodiscard]] std::filesystem::path toPath(const QString& path);

/// Путь файловой системы → QString без потери кириллицы.
[[nodiscard]] QString fromPath(const std::filesystem::path& path);

/**
 * @brief Встроенный пример: настоящая (обезличенная) конфигурация со всеми файлами пульта.
 * @param name Имя документа.
 */
[[nodiscard]] Result<ConfigDocument> loadDemo(const QString& name);

/**
 * @brief Загружает конфигурацию по источнику.
 * @param source Пусто — новая пустая; kDemoSource — пример; иначе — папка конфигурации.
 * @param name Имя для новой конфигурации и примера (у папки — имя папки).
 */
[[nodiscard]] Result<ConfigDocument> loadDocument(const QString& source, const QString& name);

} // namespace harmony::app
