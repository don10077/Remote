/**
 * @file main.cpp
 * @brief Точка входа консольной утилиты harmony-cli.
 *
 * Команды:
 *   info <путь>          — сводка по конфигурации (папка, UserConfiguration.xml или .EZHex);
 *   remote               — сведения о подключённом пульте;
 *   backup <файл.EZHex>  — прочитать конфигурацию с пульта в файл;
 *   write <путь> --yes [--force] — записать конфигурацию на пульт (сначала проверка и резервная копия);
 *   learn                — выучить ИК-сигнал с родного пульта и показать, что это;
 *   irdb build/search/show/identify — база ИК-кодов: сборка, поиск, команды, поиск модели по пульту.
 */

#include "cliCommon.h"
#include "fileCommands.h"
#include "hubCommands.h"
#include "irdbCommands.h"
#include "remoteCommands.h"
#include "transferCommands.h"
#include "harmony/core/version.h"
#include "harmony/transport/harmony900Link.h"
#include "harmony/transport/remoteLink.h"

#include <cstddef>
#include <filesystem>
#include <iostream>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#ifdef _WIN32
// GCC 16 для mingw-w64 определяет NOMINMAX в os_defines.h, поэтому проверяем
// перед определением: иначе повторное #define ловится как -Werror.
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
// shellapi.h требует windows.h, поэтому подключается после него.
#include <shellapi.h>
#endif

namespace {

/// Переключает консоль Windows в UTF-8, иначе русский текст выводится «кракозябрами».
void setupConsole() {
#ifdef _WIN32
    SetConsoleOutputCP(CP_UTF8);
#endif
}

/**
 * @brief Печатает справку по использованию.
 * @param out Поток вывода.
 */
void printUsage(std::ostream& out) {
    out << "harmony-cli " << harmony::kVersion << "\n"
        << "Tool for the Logitech Harmony 900 remote configuration.\n\n"
        << "Usage:\n"
        << "  harmony-cli models              all physical remote models and transfer capabilities\n"
        << "  harmony-cli transfer-info <model-id>\n"
        << "  harmony-cli transfer-read <model-id> <new-backup-file>\n"
        << "  harmony-cli transfer-write <model-id> <config-file> --backup <new-file> --yes\n"
        << "                                   write an existing EZHex/MH ZIP without format conversion;\n"
        << "                                   use --without-backup only for models without readback\n"
        << "  harmony-cli hub-read <hub-address> <new-file.hub.json> [--port 8088]\n"
        << "                                   read devices and activities of a stock Harmony Hub over\n"
        << "                                   the local network (Elite, Ultimate, 950, Companion…);\n"
        << "                                   no rooting, no IR codes, no account data in the file\n"
        << "  harmony-cli hub-info <file> [--commands] [--irdb <database>]\n"
        << "                                   devices, commands and activities of a saved Hub file:\n"
        << "                                   *.hub.json, the Hub's own DeviceList.json /\n"
        << "                                   ActivityList.json (these carry the IR codes), or a\n"
        << "                                   backup bundle; --irdb suggests database models for\n"
        << "                                   devices whose commands have no codes\n"
        << "  harmony-cli info <path>          configuration summary; path is a folder,\n"
        << "                                   UserConfiguration.xml or an .EZHex file\n"
        << "  harmony-cli check <path> [--fix] what will not work on the remote; --fix repairs\n"
        << "                                   what can be repaired automatically and saves\n"
        << "  harmony-cli diff <before> <after> what changes on the remote if <after> is written\n"
        << "  harmony-cli commands <path>      IR commands of the devices: protocol, address,\n"
        << "                                   command, code as in Arduino IRremote\n"
        << "  harmony-cli encode <protocol> <address> <command>\n"
        << "                                   generate IR durations for a protocol command\n"
        << "  harmony-cli bench-device <path> [<file.EZHex>] [--name <label>]\n"
        << "                                   add a test device (default TEST) with a command and\n"
        << "                                   a screen button for every protocol of encode; saves\n"
        << "                                   an EZHex archive, optionally at <file.EZHex>\n"
        << "  harmony-cli irdb build <devices> <protocols> <database>\n"
        << "                                   build the IR code database from the Logitech archive\n"
        << "                                   (device_features_raw.jsonl.gz, AllProtocols.json.gz)\n"
        << "  harmony-cli irdb search <database> <words…>  find devices in the database\n"
        << "  harmony-cli irdb show <database> <number>    commands of a database device\n"
        << "  harmony-cli irdb identify <database>         find the model by a few buttons of its\n"
        << "                                   original remote (the remote learns each button)\n"
        << "  harmony-cli remote               information about the connected remote\n"
        << "  harmony-cli backup <file.EZHex>  read the configuration from the remote into a file\n"
        << "                                   (an existing file is kept: a number is added to the name)\n"
        << "  harmony-cli write <path> --yes [--force]\n"
        << "                                   write the configuration to the remote; before that\n"
        << "                                   the remote's current configuration is saved to\n"
        << "                                   harmony-backup-<date>.EZHex in the current folder.\n"
        << "                                   Refused if the check finds errors or the remote's\n"
        << "                                   configuration cannot be read; --force writes anyway\n"
        << "                                   (Ctrl-C cancels while reading or writing)\n"
        << "  harmony-cli learn                learn a signal of the original remote: point it\n"
        << "                                   at the Harmony and press a button\n"
        << "  harmony-cli settings [<path>]    remote settings (beep, backlight, font, theme, clock,\n"
        << "                                   child lock): of the connected remote, or in a file\n"
        << "  harmony-cli set <key> <value> [<path>]\n"
        << "                                   change a setting on the connected remote (at once),\n"
        << "                                   or in a configuration file (applied when written)\n"
        << "  harmony-cli log                  the remote's own system log (why it rejected a\n"
        << "                                   configuration, what it sent)\n"
        << "  harmony-cli shell <command…>     run a command in the remote's console (telnet)\n"
        << "  harmony-cli --version            show the version\n"
        << "  harmony-cli --help               show this help\n\n"
        << "Options:\n"
        << "  --host <address>                 address of the remote for the commands that talk\n"
        << "                                   to it (default " << harmony::transport::kDefaultRemoteHost
        << ")\n\n"
        << "Exit codes: 0 success, 1 wrong arguments, 2 the command failed, 3 the configuration\n"
        << "has errors (check; write without --force).\n\n"
        << "The remote connects via USB and shows up as a network adapter, address "
        << harmony::transport::kDefaultRemoteHost << ".\n"
        << "On Windows the driver from Logitech Harmony Remote Software is required.\n";
}

/**
 * @brief Аргументы командной строки в UTF-8.
 *
 * На Windows argv приходит в кодировке ANSI и теряет кириллицу (пути, поиск,
 * команды консоли), поэтому там аргументы берутся из Unicode-версии командной строки.
 */
std::vector<std::string> utf8Arguments(int argc, char* argv[]) {
#ifdef _WIN32
    int count = 0;
    LPWSTR* wide = CommandLineToArgvW(GetCommandLineW(), &count);
    if (wide != nullptr) {
        std::vector<std::string> result;
        for (int i = 0; i < count; ++i) {
            const int size = WideCharToMultiByte(CP_UTF8, 0, wide[i], -1, nullptr, 0, nullptr, nullptr);
            std::string text;
            if (size > 1) {
                text.resize(static_cast<std::size_t>(size));
                WideCharToMultiByte(CP_UTF8, 0, wide[i], -1, text.data(), size, nullptr, nullptr);
                text.pop_back(); // завершающий ноль
            }
            result.push_back(std::move(text));
        }
        LocalFree(wide);
        return result;
    }
#endif
    return std::vector<std::string>(argv, argv + argc);
}

} // namespace

/**
 * @brief Разбирает аргументы командной строки и выполняет команду.
 * @return 0 — успех, 1 — ошибка в аргументах, 2 — ошибка выполнения команды,
 *         3 — в конфигурации ошибки (check; write без --force).
 */
int main(int argc, char* argv[]) {
    using namespace harmony::cli;
    setupConsole();
    std::vector<std::string> arguments = utf8Arguments(argc, argv);
    std::string host = harmony::transport::kDefaultRemoteHost;
    for (std::size_t i = 1; i < arguments.size();) {
        if (arguments[i] != "--host") {
            ++i;
            continue;
        }
        if (i + 1 >= arguments.size()) {
            std::cerr << "Specify an address: --host <address>\n";
            return 1;
        }
        host = arguments[i + 1];
        arguments.erase(arguments.begin() + static_cast<std::ptrdiff_t>(i),
                        arguments.begin() + static_cast<std::ptrdiff_t>(i + 2));
    }
    std::vector<char*> pointers;
    for (auto& argument : arguments) {
        pointers.push_back(argument.data());
    }
    const std::span<char*> args(pointers);

    if (args.size() < 2) {
        printUsage(std::cerr);
        return 1;
    }

    const std::string_view command = args[1];
    if (command == "models" || command == "transfer-info" || command == "transfer-read" || command == "transfer-write")
        return runTransferCommand(args, host);
    if (command == "hub-read" || command == "hub-info")
        return runHubCommand(args);
    if (command == "--version" || command == "-v") {
        std::cout << harmony::kVersion << "\n";
        return 0;
    }
    if (command == "--help" || command == "-h") {
        printUsage(std::cout);
        return 0;
    }

    if (command == "info") {
        if (args.size() < 3) {
            std::cerr << "Specify a path: harmony-cli info <folder or UserConfiguration.xml>\n";
            return 1;
        }
        return runInfo(argumentPath(args, 2));
    }

    if (command == "check") {
        if (args.size() < 3) {
            std::cerr << "Specify a path: harmony-cli check <path> [--fix]\n";
            return 1;
        }
        return runCheck(argumentPath(args, 2), args.size() > 3 && std::string_view(args[3]) == "--fix");
    }
    if (command == "diff") {
        if (args.size() < 4) {
            std::cerr << "Specify two paths: harmony-cli diff <on the remote> <to be written>\n";
            return 1;
        }
        return runDiff(argumentPath(args, 2), argumentPath(args, 3));
    }
    if (command == "commands") {
        if (args.size() < 3) {
            std::cerr << "Specify a path: harmony-cli commands <folder, UserConfiguration.xml or .EZHex>\n";
            return 1;
        }
        return runCommands(argumentPath(args, 2));
    }
    if (command == "encode") {
        if (args.size() != 5) {
            std::cerr << "Usage: harmony-cli encode <protocol> <address> <command>\n";
            return 1;
        }
        return runEncode(args[2], args[3], args[4]);
    }
    if (command == "bench-device") {
        std::filesystem::path folder;
        std::string label = "TEST";
        for (std::size_t i = 3; i < args.size(); ++i) {
            if (std::string_view(args[i]) == "--name" && i + 1 < args.size()) {
                label = args[++i];
            } else if (folder.empty() && !std::string_view(args[i]).starts_with("--")) {
                folder = argumentPath(args, i);
            } else {
                std::cerr << "Unknown option: " << args[i] << "\n";
                return 1;
            }
        }
        if (args.size() < 3) {
            std::cerr << "Usage: harmony-cli bench-device <path> [<file.EZHex>] [--name <label>]\n";
            return 1;
        }
        return runBenchDevice(argumentPath(args, 2), folder, label);
    }
    if (command == "irdb") {
        return runIrdb(args, host);
    }
    if (command == "learn") {
        return runLearn(host);
    }
    if (command == "remote") {
        harmony::transport::Harmony900Link link;
        return connectRemote(link, host) ? 0 : 2;
    }
    if (command == "backup") {
        if (args.size() < 3) {
            std::cerr << "Specify a file: harmony-cli backup <file.EZHex>\n";
            return 1;
        }
        return runBackup(argumentPath(args, 2), host);
    }
    if (command == "write") {
        bool yes = false;
        bool force = false;
        for (std::size_t i = 3; i < args.size(); ++i) {
            const std::string_view option(args[i]);
            if (option == "--yes") {
                yes = true;
            } else if (option == "--force") {
                force = true;
            } else {
                std::cerr << "Unknown option: " << option << "\n";
                return 1;
            }
        }
        if (args.size() < 3 || !yes) {
            std::cerr << "Writing changes the remote's configuration. Confirm: harmony-cli write <path> --yes\n";
            return 1;
        }
        return runWrite(argumentPath(args, 2), host, force);
    }

    if (command == "settings") {
        return runSettings(args.size() > 2 ? argumentPath(args, 2) : std::filesystem::path(), host);
    }
    if (command == "set") {
        if (args.size() < 4) {
            std::cerr << "Specify a setting and a value: harmony-cli set <key> <value> [<file.EZHex>]\n";
            return 1;
        }
        return runSet(args[2], args[3], args.size() > 4 ? argumentPath(args, 4) : std::filesystem::path(), host);
    }
    if (command == "log") {
        return runShell({}, host);
    }
    if (command == "shell") {
        if (args.size() < 3) {
            std::cerr << "Specify a command: harmony-cli shell <command…>\n";
            return 1;
        }
        std::string line;
        for (std::size_t i = 2; i < args.size(); ++i) {
            line += (line.empty() ? "" : " ") + std::string(args[i]);
        }
        return runShell(line, host);
    }

    std::cerr << "Unknown command: " << command << "\n\n";
    printUsage(std::cerr);
    return 1;
}
