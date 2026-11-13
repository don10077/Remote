#pragma once

/**
 * @file userConfiguration.h
 * @brief Чтение UserConfiguration.xml — основного файла конфигурации пульта.
 *
 * Файл описывает устройства, их ИК-команды, кнопки экрана пульта и активности.
 * Корневой элемент <Root>, внутри: <Properties>, <User>, <Controller>,
 * <Device> × N, <Activity> × M, <Protocols>.
 *
 * Сейчас поддерживается только чтение в модель; запись без потерь появится
 * следующим шагом.
 */

#include "harmony/core/error.h"
#include "harmony/core/model/remoteConfig.h"

#include <filesystem>
#include <string_view>

namespace harmony::formats {

/// Имя файла конфигурации на пульте и в резервной копии.
inline constexpr std::string_view kUserConfigurationFileName = "UserConfiguration.xml";

/**
 * @brief Разбирает содержимое UserConfiguration.xml.
 * @param xml Текст файла в UTF-8.
 * @return Модель конфигурации или ошибка ErrorCode::Format с описанием,
 *         где именно файл не соответствует ожидаемой структуре.
 */
[[nodiscard]] Result<model::RemoteConfig> parseUserConfiguration(std::string_view xml);

/**
 * @brief Читает конфигурацию с диска.
 * @param path Путь к UserConfiguration.xml или к папке резервной копии, в которой он лежит.
 * @return Модель конфигурации; ошибка ErrorCode::NotFound, если файла нет,
 *         ErrorCode::Io — если его не удалось прочитать, ErrorCode::Format — если он испорчен.
 */
[[nodiscard]] Result<model::RemoteConfig> readUserConfiguration(const std::filesystem::path& path);

} // namespace harmony::formats
