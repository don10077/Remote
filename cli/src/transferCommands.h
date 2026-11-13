#pragma once
#include <span>
#include <string>

/// Обрабатывает каталог моделей и передачу готовых конфигураций без редактора.
int runTransferCommand(std::span<char*> arguments, const std::string& host);
