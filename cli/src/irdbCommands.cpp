/**
 * @file irdbCommands.cpp
 * @brief Команды базы ИК-кодов: сборка, поиск, команды устройства, поиск модели по пульту.
 */

#include "irdbCommands.h"
#include "cliCommon.h"

#include "harmony/core/ir/irSignal.h"
#include "harmony/core/irdb/irDatabase.h"
#include "harmony/core/irdb/modelFinder.h"
#include "harmony/transport/usbNetLink.h"

#include <algorithm>
#include <charconv>
#include <chrono>
#include <cstdint>
#include <iostream>
#include <string_view>

namespace harmony::cli {
namespace {

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
    harmony::transport::UsbNetLink link;
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

} // namespace

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

} // namespace harmony::cli
