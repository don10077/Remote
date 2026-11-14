#pragma once
#include <span>

/**
 * @brief Команды Harmony Hub: чтение по сети и разбор сохранённого файла.
 *
 * Использование:
 *   harmony-cli hub-read <адрес Hub> <новый файл> [--port <порт>]
 *   harmony-cli hub-info <файл> [--irdb <база>] [--commands]
 * @param arguments argv в UTF-8 (arguments[1] — имя команды).
 * @return 0 — успех, 1 — ошибка аргументов, 2 — ошибка выполнения.
 */
int runHubCommand(std::span<char*> arguments);
