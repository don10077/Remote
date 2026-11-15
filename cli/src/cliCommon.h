#pragma once

/**
 * @file cliCommon.h
 * @brief Общее для команд консольной утилиты: аргументы-пути, связь с пультом, печать проверки и отличий.
 */

#include "harmony/core/configCheck.h"
#include "harmony/core/configDiff.h"
#include "harmony/core/configDocument.h"
#include "harmony/core/error.h"
#include "harmony/transport/harmony900Link.h"

#include <cstddef>
#include <filesystem>
#include <span>
#include <string>
#include <vector>

namespace harmony::cli {

/// Аргумент командной строки (UTF-8) как путь; args[index] должен существовать.
[[nodiscard]] std::filesystem::path argumentPath(std::span<char*> args, std::size_t index);

/// Подключается к пульту по адресу @p host и печатает модель, прошивку и серийный номер.
harmony::Result<harmony::transport::RemoteInfo> connectRemote(harmony::transport::Harmony900Link& link,
                                                              const std::string& host);

/// Печатает проблемы проверки и итог; возвращает число ошибок.
std::size_t printProblems(const std::vector<harmony::Problem>& problems);

/// Печатает отличия конфигураций: «+ device …», «* activity …» с подробностями.
void printChanges(const std::vector<harmony::ConfigChange>& changes);

/// Сохраняет изменения в EZHex; для старой папки создаёт соседний архив.
harmony::Result<void> saveArchive(harmony::ConfigDocument& document);

} // namespace harmony::cli
