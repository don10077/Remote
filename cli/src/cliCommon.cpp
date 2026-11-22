/**
 * @file cliCommon.cpp
 * @brief Общее для команд консольной утилиты.
 */

#include "cliCommon.h"

#include <iostream>
#include <string_view>

namespace harmony::cli {

/**
 * @brief Возвращает аргумент командной строки (UTF-8, см. utf8Arguments) как путь.
 * @param args Аргументы.
 * @param index Номер аргумента.
 */
std::filesystem::path argumentPath(std::span<char*> args, std::size_t index) {
#ifdef _WIN32
    const std::string_view text(args[index]);
    return std::filesystem::path(std::u8string(text.begin(), text.end()));
#else
    return std::filesystem::path(args[index]);
#endif
}

/// Подключается к пульту и печатает, что за пульт.
harmony::Result<harmony::transport::RemoteInfo> connectRemote(harmony::transport::UsbNetLink& link,
                                                              const std::string& host) {
    auto info = link.connect({.host = host, .port = harmony::transport::Endpoint{}.port});
    if (info) {
        std::cout << "Remote: " << info->model << ", firmware " << info->firmwareVersion
                  << ", serial number " << info->serial << "\n";
    } else {
        std::cerr << "Error: " << info.error().message << "\n";
    }
    return info;
}

/**
 * @brief Печатает проблемы проверки и итог.
 * @return Сколько ошибок.
 */
std::size_t printProblems(const std::vector<harmony::Problem>& problems) {
    for (const auto& problem : problems) {
        std::cout << (problem.severity == harmony::Problem::Severity::Error ? "error   " : "warning ")
                  << problem.place.message << ": " << problem.what.message
                  << (problem.fixable() ? "  [fixable]" : "") << "\n";
    }
    const auto errors = harmony::countProblems(problems, harmony::Problem::Severity::Error);
    std::cout << "Errors: " << errors
              << ", warnings: " << harmony::countProblems(problems, harmony::Problem::Severity::Warning) << "\n";
    return errors;
}

/// Печатает отличия конфигураций: «+ device …», «* activity …» с подробностями.
void printChanges(const std::vector<harmony::ConfigChange>& changes) {
    for (const auto& change : changes) {
        const char* mark = change.kind == harmony::ConfigChange::Kind::Added     ? "+ "
                           : change.kind == harmony::ConfigChange::Kind::Removed ? "- "
                                                                                 : "* ";
        std::cout << mark << (change.area == harmony::ConfigChange::Area::Device ? "device " : "activity ")
                  << change.item << "\n";
        for (const auto& detail : change.details) {
            std::cout << "    " << detail.message << "\n";
        }
    }
    std::cout << (changes.empty() ? "No differences.\n" : "");
}

/**
 * @brief Сохраняет изменения в EZHex; для старой папки создаёт соседний архив.
 * @param document Изменённый документ.
 * @return Ошибка, если новый архив уже существует или не удалось записать его.
 */
harmony::Result<void> saveArchive(harmony::ConfigDocument& document) {
    if (!document.ezhexFile().empty()) return document.save();
    auto file = document.folder();
    file += ".EZHex";
    if (std::filesystem::exists(file))
        return harmony::makeError(harmony::ErrorCode::Io, "the output archive already exists: %1", {file.string()});
    auto saved = document.saveAsEzHex(file);
    if (saved) std::cout << "Saved: " << file.string() << "\n";
    return saved;
}

} // namespace harmony::cli
