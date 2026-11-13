#include "hubCommands.h"

#include "harmony/transport/hubConfigReader.h"

#include <charconv>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <string_view>

namespace {
/// Пути argv уже переведены в UTF-8 главным модулем CLI.
std::filesystem::path path(const char* text) { return std::filesystem::path(reinterpret_cast<const char8_t*>(text)); }
} // namespace

int runHubCommand(std::span<char*> args) {
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
