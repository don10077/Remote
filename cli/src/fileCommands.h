#pragma once

/**
 * @file fileCommands.h
 * @brief Команды над файлами конфигурации без связи с пультом: info, check, diff, commands,
 *        bench-device, encode.
 */

#include <filesystem>
#include <string>
#include <string_view>

namespace harmony::cli {

/// Команда info: устройства и активности конфигурации 900 или Harmony Hub. 0 — успех, 2 — не прочиталась.
int runInfo(const std::filesystem::path& path);

/// Команда check: проблемы конфигурации; с @p fix — исправить и сохранить. 0 / 3 — есть ошибки / 2.
int runCheck(const std::filesystem::path& path, bool fix);

/// Команда diff: что изменится, если записать @p after вместо @p before.
int runDiff(const std::filesystem::path& before, const std::filesystem::path& after);

/// Команда commands: ИК-команды всех устройств — протокол, адрес, команда, код.
int runCommands(const std::filesystem::path& path);

/// Команда bench-device: тестовое устройство с командой и кнопкой на каждый протокол encode.
int runBenchDevice(const std::filesystem::path& path, const std::filesystem::path& output,
                   const std::string& label);

/// Команда encode: длительности ИК-сигнала для протокола, адреса и команды (десятичные или 0x).
int runEncode(std::string_view protocol, std::string_view addressText, std::string_view commandText);

} // namespace harmony::cli
