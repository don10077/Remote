#pragma once

/**
 * @file remoteCommands.h
 * @brief Команды, работающие с подключённым пультом: backup, write, learn, settings, set, log, shell.
 */

#include <filesystem>
#include <string>
#include <string_view>

namespace harmony::cli {

/// Команда backup: конфигурация с пульта → файл .EZHex (существующий файл не затирается).
int runBackup(const std::filesystem::path& file, const std::string& host);

/// Команда write: проверка, резервная копия с пульта, отличия, запись. 0 / 2 / 3 — ошибки без --force.
int runWrite(const std::filesystem::path& path, const std::string& host, bool force);

/// Команда learn: выучить сигнал родного пульта и напечатать протокол и длительности.
int runLearn(const std::string& host);

/// Команда settings: настройки пульта — подключённого (@p path пуст) или в файле.
int runSettings(const std::filesystem::path& path, const std::string& host);

/// Команда set: меняет настройку на пульте (@p path пуст) или в файле .EZHex.
int runSet(std::string_view key, const std::string& value, const std::filesystem::path& path,
           const std::string& host);

/// Команда log (@p command пуст) или shell: вывод команды в консоли пульта.
int runShell(const std::string& command, const std::string& host);

} // namespace harmony::cli
