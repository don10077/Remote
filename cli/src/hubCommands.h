#pragma once
#include <span>

/**
 * @brief Команда hub-read: конфигурация Harmony Hub по сети в новый файл *.hub.json.
 *
 * Использование: harmony-cli hub-read <адрес Hub> <новый файл> [--port <порт>].
 * @param arguments argv в UTF-8 (arguments[1] == "hub-read").
 * @return 0 — сохранено, 1 — ошибка аргументов, 2 — ошибка чтения или записи файла.
 */
int runHubCommand(std::span<char*> arguments);
