#pragma once

/**
 * @file irdbCommands.h
 * @brief Команды базы ИК-кодов: irdb build, search, show, identify.
 */

#include <span>
#include <string>

namespace harmony::cli {

/// Команда irdb: args[2] — подкоманда; @p host нужен для identify. 0 / 1 — ошибка аргументов / 2.
int runIrdb(std::span<char*> args, const std::string& host);

} // namespace harmony::cli
