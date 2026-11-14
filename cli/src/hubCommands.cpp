#include "hubCommands.h"

#include "harmony/core/formats/hubConfig.h"
#include "harmony/core/irdb/irDatabase.h"
#include "harmony/transport/hubConfigReader.h"

#include <algorithm>
#include <charconv>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>
#include <string_view>

namespace {

/// Пути argv уже переведены в UTF-8 главным модулем CLI.
std::filesystem::path path(const char* text) { return std::filesystem::path(reinterpret_cast<const char8_t*>(text)); }

/// Подпись канала отправки команды.
const char* transportName(harmony::formats::HubTransport transport) {
    using harmony::formats::HubTransport;
    switch (transport) {
    case HubTransport::Ir: return "IR";
    case HubTransport::Bluetooth: return "Bluetooth";
    case HubTransport::HomeAssistant: return "Home Assistant";
    default: return "unknown";
    }
}

/// Команда hub-read: конфигурация Harmony Hub по сети в новый файл.
int runHubRead(std::span<char*> args) {
    if (args.size() < 4) {
        std::cerr << "Usage: harmony-cli hub-read <hub-address> <new-file.hub.json> [--port <port>]\n";
        return 1;
    }
    const std::string host = args[2];
    const auto output = path(args[3]);
    std::uint16_t port = harmony::transport::kHubPort;
    for (std::size_t i = 4; i < args.size(); ++i) {
        const std::string_view option(args[i]);
        if (option == "--port" && i + 1 < args.size()) {
            const std::string_view text(args[++i]);
            const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), port);
            if (error != std::errc() || end != text.data() + text.size() || port == 0) {
                std::cerr << "Invalid port: " << text << '\n';
                return 1;
            }
        } else {
            std::cerr << "Unknown option: " << option << '\n';
            return 1;
        }
    }
    if (std::filesystem::exists(output)) {
        std::cerr << "The output file already exists; existing files are not overwritten.\n";
        return 2;
    }
    std::cout << "Reading the configuration of the Harmony Hub at " << host << ':' << port << "...\n";
    const auto hub = harmony::transport::readHubConfig(host, port);
    if (!hub) {
        std::cerr << "Error: " << hub.error().message << '\n';
        return 2;
    }
    std::ofstream file(output, std::ios::binary | std::ios::noreplace);
    if (!file) {
        std::cerr << "Cannot create output (existing files are not overwritten).\n";
        return 2;
    }
    file.write(hub->backupJson.data(), static_cast<std::streamsize>(hub->backupJson.size()));
    file.close();
    if (!file) {
        std::cerr << "Could not finish output file.\n";
        return 2;
    }
    std::cout << "Hub: " << (hub->hubName.empty() ? "(no name)" : hub->hubName)
              << ", firmware " << (hub->firmware.empty() ? "?" : hub->firmware) << '\n'
              << "Devices: " << hub->devices.size() << ", activities: " << hub->activities.size() << '\n';
    for (const auto& device : hub->devices)
        std::cout << "  " << device.label << "  (" << device.manufacturer << ' ' << device.model << ", "
                  << device.type << ", commands: " << device.commands << ")\n";
    for (const auto& activity : hub->activities) std::cout << "  * " << activity << '\n';
    std::cout << "Saved: " << output.string() << " (" << hub->backupJson.size() << " bytes)\n"
              << "The file contains device names and models, but no IR codes, account or e-mail.\n";
    return 0;
}

/// Печатает устройства, команды и активности разобранного файла.
void printConfiguration(const harmony::formats::HubConfiguration& hub, bool withCommands) {
    using harmony::formats::HubConfigSource;
    std::cout << "Source: "
              << (hub.source == HubConfigSource::Resources
                      ? "the Hub's own resource files (IR codes included)"
                      : "the stock Hub over the network (command names only)")
              << '\n';
    if (!hub.hubName.empty()) std::cout << "Hub: " << hub.hubName << '\n';
    if (!hub.firmware.empty()) std::cout << "Firmware: " << hub.firmware << '\n';
    if (!hub.createdUtc.empty()) std::cout << "Read: " << hub.createdUtc << '\n';
    std::cout << "Devices: " << hub.devices.size() << ", activities: " << hub.activities.size()
              << ", commands: " << hub.commandCount() << " (with IR or key codes: " << hub.commandsWithCodes()
              << ")\n";
    for (const auto& warning : hub.warnings) std::cout << "Note: " << warning << '\n';

    std::cout << "\nDevices:\n";
    for (const auto& device : hub.devices) {
        std::cout << "  " << device.displayName() << "  (" << device.manufacturer << ' ' << device.model << ", "
                  << (device.type.empty() ? "type " + std::to_string(device.typeCode) : device.type)
                  << ", commands: " << device.commands.size() << ", with codes: " << device.commandsWithCodes()
                  << ")\n";
        if (!withCommands) continue;
        for (const auto& command : device.commands) {
            std::cout << "    " << command.name << ": " << transportName(command.transport);
            if (command.hasCode()) std::cout << " · " << command.protocol << ' ' << command.code;
            if (command.repeat) std::cout << " · repeat";
            if (command.learned) std::cout << " · learned";
            std::cout << '\n';
        }
    }

    std::cout << "\nActivities:\n";
    for (const auto& activity : hub.activities) {
        std::cout << "  " << activity.label << (activity.isPowerOff() ? "  (power off)" : "") << '\n';
        for (const auto& role : activity.roles) {
            const auto device = std::ranges::find(hub.devices, role.deviceId, &harmony::formats::HubDevice::id);
            std::cout << "      "
                      << (device != hub.devices.end() ? device->displayName() : role.deviceId);
            if (!role.role.empty()) std::cout << " · " << role.role;
            if (!role.input.empty()) std::cout << " · input " << role.input;
            std::cout << '\n';
        }
    }
}

/// Ищет в базе ИК-кодов модели, похожие на устройства без кодов.
void suggestIrdb(const harmony::formats::HubConfiguration& hub, const std::filesystem::path& file) {
    auto database = harmony::irdb::IrDatabase::open(file);
    if (!database) {
        std::cerr << "Could not open the IR code database: " << database.error().message << '\n';
        return;
    }
    std::cout << "\nIR code candidates from the database (by manufacturer and model):\n";
    for (const auto& device : hub.devices) {
        if (device.commandsWithCodes() == device.commands.size() && !device.commands.empty()) continue;
        const std::string query = device.manufacturer + " " + device.model;
        std::cout << "  " << device.displayName() << " — " << query << ":\n";
        std::size_t found = 0;
        const auto hits = database->search(query, 5, &found);
        if (hits.empty()) {
            std::cout << "      nothing matched; try harmony-cli irdb search\n";
            continue;
        }
        for (const std::size_t index : hits) {
            const auto& entry = database->entries()[index];
            std::cout << "      " << index << '\t' << entry.manufacturer << ' ' << entry.model << '\t'
                      << harmony::irdb::deviceTypeName(entry.deviceType) << '\n';
        }
        if (found > hits.size()) std::cout << "      … " << found << " in total\n";
    }
    std::cout << "Matching is by name only; the commands themselves are not copied yet.\n";
}

/// Команда hub-info: разбор сохранённого файла конфигурации хаба.
int runHubInfo(std::span<char*> args) {
    if (args.size() < 3) {
        std::cerr << "Usage: harmony-cli hub-info <file> [--commands] [--irdb <database>]\n"
                     "Accepts a *.hub.json file, the Hub's DeviceList.json / ActivityList.json,\n"
                     "or a backup bundle that contains them.\n";
        return 1;
    }
    const auto file = path(args[2]);
    bool withCommands = false;
    std::filesystem::path database;
    for (std::size_t i = 3; i < args.size(); ++i) {
        const std::string_view option(args[i]);
        if (option == "--commands") {
            withCommands = true;
        } else if (option == "--irdb" && i + 1 < args.size()) {
            database = path(args[++i]);
        } else {
            std::cerr << "Unknown option: " << option << '\n';
            return 1;
        }
    }
    std::error_code ec;
    const auto size = std::filesystem::file_size(file, ec);
    if (ec || size > 64 * 1024 * 1024) {
        std::cerr << "Cannot read the file, or it is too large.\n";
        return 2;
    }
    std::ifstream input(file, std::ios::binary);
    const std::string text((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
    if (text.size() != size) {
        std::cerr << "Could not read the file completely.\n";
        return 2;
    }
    const auto hub = harmony::formats::parseHubConfig(text);
    if (!hub) {
        std::cerr << "Error: " << hub.error().message << '\n';
        return 2;
    }
    printConfiguration(*hub, withCommands);
    if (!database.empty()) suggestIrdb(*hub, database);
    return 0;
}

} // namespace

int runHubCommand(std::span<char*> args) {
    const std::string_view command(args[1]);
    if (command == "hub-info") return runHubInfo(args);
    return runHubRead(args);
}
