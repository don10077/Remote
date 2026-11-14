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

#include "harmony/core/configDiff.h"
#include "harmony/core/formats/hubConfig.h"
#include "transferCommands.h"
#include "hubCommands.h"
#include "harmony/core/configDocument.h"
#include "harmony/core/irdb/irDatabase.h"
#include "harmony/core/irdb/modelFinder.h"
#include "harmony/core/ir/irEncode.h"
#include "harmony/core/remotePreferences.h"
#include "harmony/core/version.h"
#include "harmony/transport/harmony900Link.h"
#include "harmony/transport/remoteServices.h"

#include <atomic>
#include <charconv>
#include <chrono>
#include <csignal>
#include <ctime>
#include <fstream>
#include <iterator>

#include <filesystem>
#include <iostream>
#include <optional>
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

/// Нажат Ctrl-C: чтение или запись отменяются через printProgress, а не убийством процесса.
std::atomic<bool> cancelRequested{false};
static_assert(std::atomic<bool>::is_always_lock_free, "the flag is set from a signal handler");

#ifdef _WIN32
BOOL WINAPI onConsoleControl(DWORD event) {
    if (event == CTRL_C_EVENT || event == CTRL_BREAK_EVENT) {
        cancelRequested = true;
        return TRUE;
    }
    return FALSE;
}
#else
void onInterrupt(int /*signal*/) {
    cancelRequested = true;
}
#endif

/// Перехватывает Ctrl-C на время обмена с пультом.
void catchInterrupt() {
#ifdef _WIN32
    SetConsoleCtrlHandler(onConsoleControl, TRUE);
#else
    std::signal(SIGINT, onInterrupt);
#endif
}

/// Печатает устройства и активности общей модели — у любого формата она одна.
int printModel(const harmony::model::RemoteConfig& config);

/**
 * @brief Читает конфигурацию хаба, если это она.
 * @return Модель; nullopt — файл не является конфигурацией хаба (обычный путь для 900).
 */
std::optional<harmony::model::RemoteConfig> openHubConfig(const std::filesystem::path& path) {
    std::error_code ec;
    if (!std::filesystem::is_regular_file(path, ec)) return std::nullopt;
    const auto size = std::filesystem::file_size(path, ec);
    if (ec || size == 0 || size > 64 * 1024 * 1024) return std::nullopt;
    std::ifstream input(path, std::ios::binary);
    const std::string text((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
    auto hub = harmony::formats::parseHubConfig(text);
    if (!hub) return std::nullopt;
    return harmony::formats::toRemoteConfig(*hub);
}

/**
 * @brief Команда info: читает конфигурацию и печатает устройства и активности.
 * @param path Папка, UserConfiguration.xml, .EZHex или файл конфигурации Harmony Hub.
 * @return 0 — успех, 2 — конфигурацию не удалось прочитать.
 */
int runInfo(const std::filesystem::path& path) {
    // Конфигурация хаба и конфигурация 900 показываются одинаково: модель общая.
    if (const auto hub = openHubConfig(path)) return printModel(*hub);
    const auto document = harmony::ConfigDocument::open(path);
    if (!document) {
        std::cerr << "Error: " << document.error().message << "\n";
        return 2;
    }
    return printModel(document->model());
}

int printModel(const harmony::model::RemoteConfig& model) {
    const harmony::model::RemoteConfig* config = &model;

    std::cout << "Updated: " << config->property("LastUpdated") << "\n"
              << "Devices: " << config->devices.size()
              << ", activities: " << config->userActivityCount()
              << ", IR commands: " << config->commandCount() << "\n\n";

    std::cout << "Devices:\n";
    for (const auto& device : config->devices) {
        std::cout << "  " << device.displayName() << "  (" << device.manufacturer << " "
                  << device.model << ", " << device.type << ", commands: " << device.commands.size()
                  << ")" << (config->isDeviceUsed(device.id) ? "" : "  — not used") << "\n";
    }

    std::cout << "\nActivities:\n";
    for (const auto& activity : config->activities) {
        std::cout << "  " << activity.label;
        if (!activity.powerOn.empty()) {
            std::cout << "  ←";
            for (const auto& deviceId : activity.powerOn) {
                const auto* device = config->findDevice(deviceId);
                std::cout << " [" << (device != nullptr ? device->displayName() : deviceId) << "]";
            }
        }
        std::cout << "\n";
    }
    return 0;
}

/// Печатает прогресс одной строкой: «Запись: 45%».
bool printProgress(harmony::transport::Stage stage, std::size_t done, std::size_t total) {
    using harmony::transport::Stage;
    const char* name = stage == Stage::Read      ? "Reading"
                       : stage == Stage::Write   ? "Writing"
                       : stage == Stage::Verify  ? "Verifying"
                       : stage == Stage::Restart ? "Restarting the remote"
                       : stage == Stage::SetTime ? "Clock"
                                                 : "Connecting";
    std::cout << "\r" << name << ": " << (total > 0 ? done * 100 / total : 0) << "%    " << std::flush;
    if (!cancelRequested) {
        return true;
    }
    if (stage == Stage::Read || stage == Stage::Write) {
        return false;
    }
    // Проверку, перезапуск и часы транспорт не прерывает: пульт уже применяет конфигурацию.
    static bool told = false;
    if (!told) {
        told = true;
        std::cout << "\nCannot cancel now: the remote is applying the configuration.\n";
    }
    return true;
}

/// Записывает байты в файл; false — не удалось, в том числе при сбросе буфера на диск.
bool writeFile(const std::filesystem::path& path, const std::vector<std::uint8_t>& bytes) {
    std::ofstream file(path, std::ios::binary);
    file.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    file.close();
    return !file.fail();
}

/// Подключается к пульту и печатает, что за пульт.
harmony::Result<harmony::transport::RemoteInfo> connectRemote(harmony::transport::Harmony900Link& link,
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

/**
 * @brief Проверка конфигурации: что на пульте не сработает; с --fix — исправить и сохранить.
 * @param path Папка, UserConfiguration.xml или файл .EZHex.
 * @param fix Исправить, что исправляется само, и сохранить.
 * @return 0 — ошибок нет, 3 — есть ошибки, 2 — не открылась.
 */
int runCheck(const std::filesystem::path& path, bool fix) {
    auto document = harmony::ConfigDocument::open(path);
    if (!document) {
        std::cerr << "Error: " << document.error().message << "\n";
        return 2;
    }
    if (fix) {
        auto fixed = document->fixAll();
        if (!fixed) {
            std::cerr << "Error: " << fixed.error().message << "\n";
            return 2;
        }
        if (*fixed > 0) {
            if (auto saved = saveArchive(*document); !saved) {
                std::cerr << "Error: " << saved.error().message << "\n";
                return 2;
            }
        }
        std::cout << "Fixed: " << *fixed << "\n";
    }
    return printProblems(document->check()) > 0 ? 3 : 0;
}

/// Что изменится, если записать @p after вместо @p before.
int runDiff(const std::filesystem::path& before, const std::filesystem::path& after) {
    auto from = harmony::ConfigDocument::open(before);
    auto to = harmony::ConfigDocument::open(after);
    if (!from || !to) {
        std::cerr << "Error: " << (!from ? from.error().message : to.error().message) << "\n";
        return 2;
    }
    printChanges(harmony::compareConfigurations(*from, *to));
    return 0;
}

/**
 * @brief Печатает ИК-команды всех устройств: протокол, адрес, команда, код.
 * @param path Папка, UserConfiguration.xml или файл .EZHex.
 * @return Код завершения.
 */
int runCommands(const std::filesystem::path& path) {
    const auto document = harmony::ConfigDocument::open(path);
    if (!document) {
        std::cerr << "Error: " << document.error().message << "\n";
        return 2;
    }
    for (const auto& device : document->model().devices) {
        std::cout << device.displayName() << "  (" << device.manufacturer << " " << device.model << ")\n";
        for (const auto& command : device.commands) {
            std::cout << "  " << command.name << ": ";
            const auto details = document->commandDetails(device.id, command.name);
            if (!details) {
                std::cout << details.error().message << "\n";
                continue;
            }
            std::cout << (details->decoded ? details->decoded->text() : harmony::ir::describe(details->signal));
            if (const std::string raw = details->rawData(); !raw.empty()) {
                std::cout << " · " << raw;
            }
            std::cout << (details->learned ? " · learned" : "") << "\n";
        }
    }
    return 0;
}

/**
 * @brief Команда bench-device: устройство с командой каждого протокола, который собирает
 * программа, и экранной кнопкой на каждую — для записи излучения пульта через Broadlink RM4
 * (tools/broadlinkLearn.py learn). Адреса и команды — как в tools/protocolBench.py.
 *
 * Если устройство с таким именем уже есть, команды и кнопки в нём заменяются.
 * @param path Конфигурация; архив сохраняется на месте, для старой папки создаётся соседний EZHex.
 * @param output Если не пусто — сохранить результат в этот файл EZHex.
 * @param label Имя устройства на пульте.
 * @return Код завершения.
 */
int runBenchDevice(const std::filesystem::path& path, const std::filesystem::path& output,
                   const std::string& label) {
    struct Case {
        const char* name;
        const char* protocol;
        std::uint32_t address;
        std::uint32_t command;
    };
    static constexpr Case kCases[] = {
        {"NEC", "NEC", 0x04, 0x08},
        {"NECext", "NEC (extended)", 0x1E7, 0x45},
        {"Samsung32", "Samsung32", 0x0707, 0x02},
        {"SIRC12", "Sony SIRC 12", 0x01, 0x15},
        {"SIRC15", "Sony SIRC 15", 0x97, 0x2A},
        {"SIRC20", "Sony SIRC 20", 0x1A3, 0x3C},
        {"RC5", "RC5", 0x05, 0x35},
        {"RC5X", "RC5", 0x10, 0x4C},
        {"RC6", "RC6", 0xA5, 0x5A},
        {"Panasonic", "Panasonic", 0x080, 0x3D},
        {"LG", "LG", 0x04, 0x10EF},
        {"JVC", "JVC", 0x03, 0x17},
        {"Onkyo", "Onkyo", 0xD2D2, 0x1234},
        {"Denon", "Denon", 0x02, 0xE1},
        {"Sharp", "Sharp", 0x01, 0x16},
        {"RCA", "RCA", 0x0F, 0x54},
    };

    auto document = harmony::ConfigDocument::open(path);
    if (!document) {
        std::cerr << "Error: " << document.error().message << "\n";
        return 2;
    }
    std::string deviceId;
    for (const auto& device : document->model().devices) {
        if (device.label == label) {
            deviceId = device.id;
        }
    }
    if (deviceId.empty()) {
        auto created = document->createDevice({.label = label, .type = "Television",
                                               .manufacturer = "Bench", .model = label});
        if (!created) {
            std::cerr << "Error: " << created.error().message << "\n";
            return 2;
        }
        deviceId = *created;
        std::cout << "Device " << label << " created\n";
    } else {
        std::cout << "Device " << label << " exists: its commands are replaced\n";
    }

    const auto owner = harmony::ButtonOwner::device(deviceId);
    int position = 0;
    for (const auto& item : kCases) {
        auto stored = document->setProtocolCommand(deviceId, item.name, item.protocol, item.address, item.command);
        if (!stored) {
            std::cerr << "Error: " << item.name << ": " << stored.error().message << "\n";
            return 2;
        }
        if (auto button = document->setSoftButton(owner, position, item.name, deviceId, item.name); !button) {
            std::cerr << "Error: button " << item.name << ": " << button.error().message << "\n";
            return 2;
        }
        std::cout << "  " << position + 1 << ". " << item.name << ": " << item.protocol << " " << std::hex
                  << std::showbase << item.address << " " << item.command << std::dec << std::noshowbase
                  << (*stored ? " · protocol (IrProto.bin)" : " · signal (SsIr.bin)") << "\n";
        ++position;
    }

    auto file = output;
    if (!file.empty() && file.extension() != ".EZHex" && file.extension() != ".ezhex") file += ".EZHex";
    const auto saved = file.empty() ? saveArchive(*document) : document->saveAsEzHex(file);
    if (!saved) {
        std::cerr << "Error: " << saved.error().message << "\n";
        return 2;
    }
    std::cout << "Saved: " << document->location().string() << "\n";
    return 0;
}

/**
 * @brief Поиск модели по кнопкам родного пульта: Harmony учит кнопку за кнопкой,
 * после каждой печатаются лучшие модели. Конец — 30 секунд без нажатий.
 * @param file Файл базы.
 * @return Код завершения.
 */
int runIdentify(const std::filesystem::path& file, const std::string& host) {
    auto database = harmony::irdb::IrDatabase::open(file);
    if (!database) {
        std::cerr << "Error: " << database.error().message << "\n";
        return 2;
    }
    auto index = database->signalIndex();
    if (!index) {
        std::cerr << "Error: " << index.error().message << "\n";
        return 2;
    }
    harmony::transport::Harmony900Link link;
    if (!connectRemote(link, host)) {
        return 2;
    }
    harmony::irdb::ModelFinder finder(*database, *index);
    std::cout << "Point the original remote at the Harmony and press different buttons one by one:\n"
                 "power, volume, a digit, a menu key… Stops after 30 seconds without a press.\n";
    for (;;) {
        auto signal = link.learnIr({}, std::chrono::seconds(30));
        if (!signal) {
            std::cout << (finder.presses() > 0 ? "Done.\n" : "Nothing pressed.\n");
            return finder.presses() > 0 ? 0 : 2;
        }
        switch (finder.add(*signal)) {
        case harmony::irdb::ModelFinder::Press::Repeated:
            std::cout << "That button was pressed already — press another one.\n";
            continue;
        case harmony::irdb::ModelFinder::Press::TooShort:
            std::cout << "The signal was too short — press the button once more.\n";
            continue;
        case harmony::irdb::ModelFinder::Press::Added:
            break;
        }
        const auto results = finder.results({}, 10);
        std::cout << "\nButtons: " << finder.presses() << " · " << harmony::ir::describe(*signal) << "\n";
        if (results.hits.empty()) {
            std::cout << "  no model sends these signals\n";
            continue;
        }
        std::cout << "  " << results.bestCount << " model(s) know " << results.best << " of "
                  << finder.presses() << ":";
        for (std::size_t i = 0; i < std::min<std::size_t>(5, results.makers.size()); ++i) {
            std::cout << (i == 0 ? " " : ", ") << results.makers[i].first << " " << results.makers[i].second;
        }
        std::cout << "\n";
        for (const auto& hit : results.hits) {
            const auto& entry = database->entries()[hit.entry];
            std::cout << "  " << hit.matched << "/" << finder.presses() << "\t" << hit.entry << "\t"
                      << entry.manufacturer << " " << entry.model << "\n";
        }
    }
}

/**
 * @brief Команды базы ИК-кодов: build, search, show, identify.
 * @param args Аргументы командной строки (args[2] — подкоманда).
 * @param host Адрес пульта (для identify).
 * @return Код завершения.
 */
int runIrdb(std::span<char*> args, const std::string& host) {
    const std::string_view action = args.size() > 2 ? std::string_view(args[2]) : std::string_view();
    if (action == "build" && args.size() >= 6) {
        int lastPercent = -1;
        auto stats = harmony::irdb::buildDatabase(
            argumentPath(args, 3), argumentPath(args, 4), argumentPath(args, 5), "logitech-harmony-ir-archive",
            [&lastPercent](std::uint64_t done, std::uint64_t total) {
                const int percent = total > 0 ? static_cast<int>(done * 100 / total) : 0;
                if (percent != lastPercent) {
                    lastPercent = percent;
                    std::cout << "\r" << percent << "%" << std::flush;
                }
                return true;
            });
        std::cout << "\n";
        if (!stats) {
            std::cerr << "Error: " << stats.error().message << "\n";
            return 2;
        }
        std::cout << "Devices: " << stats->devices << ", commands: " << stats->commands
                  << ", skipped lines: " << stats->skippedLines << ", signals: " << stats->signalFrames
                  << "\n";
        return 0;
    }
    if ((action == "search" || action == "show") && args.size() >= 5) {
        auto database = harmony::irdb::IrDatabase::open(argumentPath(args, 3));
        if (!database) {
            std::cerr << "Error: " << database.error().message << "\n";
            return 2;
        }
        if (action == "search") {
            std::string query;
            for (std::size_t i = 4; i < args.size(); ++i) {
                query += std::string(args[i]) + " ";
            }
            std::size_t found = 0;
            for (const std::size_t index : database->search(query, 50, &found)) {
                const auto& entry = database->entries()[index];
                std::cout << index << "\t" << entry.manufacturer << " " << entry.model << "\t"
                          << harmony::irdb::deviceTypeName(entry.deviceType) << "\n";
            }
            std::cout << "Found: " << found << "\n";
            return 0;
        }
        const std::string_view number(args[4]);
        std::size_t entry = 0;
        if (const auto [end, error] = std::from_chars(number.data(), number.data() + number.size(), entry);
            error != std::errc() || end != number.data() + number.size()) {
            std::cerr << "Error: not a device number: " << number << "\n";
            return 1;
        }
        auto device = database->device(entry);
        if (!device) {
            std::cerr << "Error: " << device.error().message << "\n";
            return 2;
        }
        std::cout << device->manufacturer << " " << device->model << " ("
                  << harmony::irdb::deviceTypeName(device->deviceType) << "), power: " << device->powerType
                  << ", inputs: " << device->inputs.size() << "\n";
        for (const auto& command : device->commands) {
            auto signal = database->protocols().render(command);
            std::cout << "  " << command.name << " [" << command.type << "]: ";
            if (!signal) {
                std::cout << signal.error().message << "\n";
                continue;
            }
            const auto decoded = harmony::ir::decode(*signal);
            std::cout << (decoded ? decoded->text() + " · " + decoded->rawData() : harmony::ir::describe(*signal))
                      << "\n";
        }
        return 0;
    }
    if (action == "identify" && args.size() >= 4) {
        return runIdentify(argumentPath(args, 3), host);
    }
    std::cerr << "Usage: harmony-cli irdb build|search|show|identify … (see harmony-cli --help)\n";
    return 1;
}

/// Команда backup: конфигурация с пульта → файл .EZHex.
int runBackup(const std::filesystem::path& file, const std::string& host) {
    harmony::transport::Harmony900Link link;
    if (!connectRemote(link, host)) {
        return 2;
    }
    catchInterrupt();
    auto ezhex = link.readConfig(printProgress);
    std::cout << "\n";
    if (!ezhex) {
        std::cerr << "Error: " << ezhex.error().message << "\n";
        return 2;
    }
    if (!writeFile(file, *ezhex)) {
        std::cerr << "Error: could not write the file\n";
        return 2;
    }
    std::cout << "Saved: " << ezhex->size() << " bytes\n";
    return 0;
}

/**
 * @brief Команда write: проверка, резервная копия с пульта, отличия, затем запись.
 * @param path Что записать.
 * @param host Адрес пульта.
 * @param force Писать и с ошибками проверки, и если копию с пульта не удалось разобрать.
 * @return 0 — записано, 2 — ошибка, 3 — в конфигурации ошибки (без --force).
 */
int runWrite(const std::filesystem::path& path, const std::string& host, bool force) {
    const auto document = harmony::ConfigDocument::open(path);
    if (!document) {
        std::cerr << "Error: " << document.error().message << "\n";
        return 2;
    }
    // Проверяем, что конфигурация собирается, ещё до связи с пультом.
    if (auto check = document->toEzHex(true); !check) {
        std::cerr << "Error: " << check.error().message << "\n";
        return 2;
    }
    // Как в приложении: с ошибками проверки часть кнопок на пульте молча не сработает.
    if (const auto problems = document->check();
        harmony::countProblems(problems, harmony::Problem::Severity::Error) > 0) {
        printProblems(problems);
        if (!force) {
            std::cerr << "Error: the configuration has errors; some buttons or steps will not work on the remote.\n"
                         "Fix them (harmony-cli check <path> --fix) or add --force to write anyway.\n";
            return 3;
        }
    }
    harmony::transport::Harmony900Link link;
    if (!connectRemote(link, host)) {
        return 2;
    }
    catchInterrupt();

    // Резервная копия обязательна: другого способа вернуть прежнюю конфигурацию нет.
    char stamp[32] = {};
    const std::time_t now = std::time(nullptr);
    std::strftime(stamp, sizeof stamp, "%Y%m%d-%H%M%S", std::localtime(&now));
    const std::filesystem::path backup = std::string("harmony-backup-") + stamp + ".EZHex";
    auto current = link.readConfig(printProgress);
    std::cout << "\n";
    if (!current) {
        std::cerr << "Error: " << current.error().message << " — writing cancelled\n";
        return 2;
    }
    if (!writeFile(backup, *current)) {
        std::cerr << "Error: could not save the backup " << backup.string() << " — writing cancelled\n";
        return 2;
    }
    std::cout << "Backup: " << backup.string() << "\n";

    // Настройки RF-бластеров — с пульта, как они сейчас (их меняют на самом пульте).
    const auto onRemote = harmony::ConfigDocument::fromEzHex(*current, "remote");
    if (onRemote) {
        std::cout << "Changes on the remote:\n";
        printChanges(harmony::compareConfigurations(*onRemote, *document));
    } else {
        std::cerr << "Warning: the remote's configuration could not be read (" << onRemote.error().message
                  << "): its RF blaster settings will be replaced by those of the file being written.\n";
        if (!force) {
            std::cerr << "Writing cancelled; add --force to write anyway.\n";
            return 2;
        }
    }
    const auto ezhex = onRemote ? document->toEzHexKeepingRemoteRf(*onRemote) : document->toEzHex(true);
    if (!ezhex) {
        std::cerr << "Error: " << ezhex.error().message << "\n";
        return 2;
    }
    if (cancelRequested) {
        std::cerr << "Writing cancelled. The backup is kept.\n";
        return 2;
    }
    auto written = link.writeConfig(*ezhex, printProgress);
    std::cout << "\n";
    if (!written) {
        std::cerr << "Error: " << written.error().message << "\n";
        return 2;
    }
    std::cout << "Done: configuration written, remote restarted.\n";
    return 0;
}

/// Команда learn: выучить сигнал и напечатать протокол и длительности.
int runLearn(const std::string& host) {
    harmony::transport::Harmony900Link link;
    if (!connectRemote(link, host)) {
        return 2;
    }
    std::cout << "Point the original remote at the Harmony and press a button (waiting 30 seconds)...\n";
    auto signal = link.learnIr({}, std::chrono::seconds(30));
    if (!signal) {
        std::cerr << "Error: " << signal.error().message << "\n";
        return 2;
    }
    std::cout << harmony::ir::describe(*signal) << "\nCarrier, Hz: " << signal->frequency
              << "\nDurations, µs (+pulse/−gap):";
    for (const int duration : signal->durations) {
        std::cout << " " << duration;
    }
    std::cout << "\n";
    return 0;
}

/// Собирает ИК-сигнал для стенда; числа принимаются в десятичном виде или с префиксом 0x.
int runEncode(std::string_view protocol, std::string_view addressText, std::string_view commandText) {
    const auto parse = [](std::string_view text) -> std::optional<std::uint32_t> {
        int base = 10;
        if (text.starts_with("0x") || text.starts_with("0X")) {
            text.remove_prefix(2);
            base = 16;
        }
        std::uint32_t value = 0;
        const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), value, base);
        if (error != std::errc{} || end != text.data() + text.size() || text.empty()) return std::nullopt;
        return value;
    };
    const auto address = parse(addressText);
    const auto command = parse(commandText);
    if (!address || !command) {
        std::cerr << "Address and command must be unsigned decimal or 0x hexadecimal numbers.\n";
        return 1;
    }
    auto signal = harmony::ir::encode(protocol, *address, *command);
    if (!signal) {
        std::cerr << "Error: " << signal.error().message << "\n";
        return 2;
    }
    std::cout << "Frequency, Hz: " << signal->frequency << "\nDurations, us (+pulse/-gap):";
    for (const int duration : signal->durations) std::cout << ' ' << duration;
    std::cout << '\n';
    return 0;
}

/// Подпись значения настройки: «On», «24-hour» или само число.
std::string preferenceText(const harmony::RemotePreference& preference, const std::string& value) {
    for (const auto& item : preference.choices) {
        if (item.value == value) {
            return std::string(item.label) + " (" + value + ")";
        }
    }
    return value;
}

/// Допустимые значения настройки для подсказки.
std::string preferenceRange(const harmony::RemotePreference& preference) {
    if (preference.choices.empty()) {
        return std::to_string(preference.min) + "…" + std::to_string(preference.max);
    }
    std::string out;
    for (const auto& item : preference.choices) {
        out += (out.empty() ? "" : ", ") + std::string(item.value) + " = " + std::string(item.label);
    }
    return out;
}

/**
 * @brief Команда settings: настройки пульта — подключённого или в файле конфигурации.
 * @param path Пусто — подключённый пульт.
 * @param host Адрес пульта.
 */
int runSettings(const std::filesystem::path& path, const std::string& host) {
    std::optional<harmony::ConfigDocument> document;
    if (!path.empty()) {
        auto opened = harmony::ConfigDocument::open(path);
        if (!opened) {
            std::cerr << "Error: " << opened.error().message << "\n";
            return 2;
        }
        document = std::move(*opened);
    }
    for (const auto& preference : harmony::remotePreferences()) {
        std::string value;
        if (document) {
            value = document->remotePreference(preference.key).value_or("— (the remote keeps its own)");
        } else {
            auto read = harmony::transport::readRemoteSetting(host, preference.endpoint());
            if (!read) {
                std::cerr << "Error: " << read.error().message << "\n";
                return 2;
            }
            value = *read;
        }
        std::cout << "  " << preference.key << "\t" << preference.label << ": "
                  << preferenceText(preference, value) << "\n";
    }
    return 0;
}

/**
 * @brief Команда set: меняет настройку на пульте или в файле конфигурации.
 * @param path Пусто — подключённый пульт.
 * @param host Адрес пульта.
 */
int runSet(std::string_view key, const std::string& value, const std::filesystem::path& path,
           const std::string& host) {
    const auto* preference = harmony::findRemotePreference(key);
    if (preference == nullptr) {
        std::cerr << "Unknown setting: " << key << ". Settings:";
        for (const auto& item : harmony::remotePreferences()) {
            std::cerr << " " << item.key;
        }
        std::cerr << "\n";
        return 1;
    }
    if (auto checked = harmony::checkPreferenceValue(*preference, value); !checked) {
        std::cerr << "Error: " << checked.error().message << " (" << preferenceRange(*preference) << ")\n";
        return 1;
    }
    if (path.empty()) {
        auto written = harmony::transport::writeRemoteSetting(host, preference->endpoint(), value);
        if (!written) {
            std::cerr << "Error: " << written.error().message << "\n";
            return 2;
        }
        std::cout << preference->label << ": " << preferenceText(*preference, value) << "\n";
        return 0;
    }
    auto document = harmony::ConfigDocument::open(path);
    if (!document) {
        std::cerr << "Error: " << document.error().message << "\n";
        return 2;
    }
    if (document->ezhexFile().empty()) {
        std::cerr << "Error: remote settings are stored only in an .EZHex file\n";
        return 2;
    }
    if (auto changed = document->setRemotePreference(key, value); !changed) {
        std::cerr << "Error: " << changed.error().message << "\n";
        return 2;
    }
    if (auto saved = saveArchive(*document); !saved) {
        std::cerr << "Error: " << saved.error().message << "\n";
        return 2;
    }
    std::cout << preference->label << ": " << preferenceText(*preference, value)
              << " — saved; it takes effect when the configuration is written\n";
    return 0;
}

/// Команда log или shell: вывод команды в консоли пульта.
int runShell(const std::string& command, const std::string& host) {
    auto output = command.empty() ? harmony::transport::readRemoteLog(host)
                                  : harmony::transport::runRemoteShell(host, command);
    if (!output) {
        std::cerr << "Error: " << output.error().message << "\n";
        return 2;
    }
    std::cout << *output << "\n";
    return 0;
}

} // namespace

/**
 * @brief Разбирает аргументы командной строки и выполняет команду.
 * @return 0 — успех, 1 — ошибка в аргументах, 2 — ошибка выполнения команды,
 *         3 — в конфигурации ошибки (check; write без --force).
 */
int main(int argc, char* argv[]) {
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
