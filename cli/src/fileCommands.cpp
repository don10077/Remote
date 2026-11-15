/**
 * @file fileCommands.cpp
 * @brief Команды над файлами конфигурации без связи с пультом.
 */

#include "fileCommands.h"
#include "cliCommon.h"

#include "harmony/core/configDiff.h"
#include "harmony/core/configDocument.h"
#include "harmony/core/formats/hubConfig.h"
#include "harmony/core/ir/irEncode.h"

#include <charconv>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <iterator>
#include <optional>

namespace harmony::cli {
namespace {

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

} // namespace

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

} // namespace harmony::cli
