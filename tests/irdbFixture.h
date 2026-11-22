#pragma once

/**
 * @file irdbFixture.h
 * @brief Синтетическая база ИК-кодов для тестов: три протокола и три устройства,
 *        собранные в коде. Ожидания тестов следуют из этой конструкции, а не из
 *        содержимого публичного архива (правило 19).
 *
 * Протоколы повторяют нужные поля формата AllProtocols.json: сегменты данных,
 * постоянные сегменты и код команды «G:<имя>:(…)(…)(…):n». Устройства — строки
 * device_features_raw.jsonl с командами, питанием и входами.
 */

#include <nlohmann/json.hpp>
#include <zlib.h>

#include <cstdio>
#include <filesystem>
#include <string>
#include <utility>

namespace harmony::test {

/// Исходники базы: содержимое двух файлов архива.
struct IrDatabaseSource {
    std::string devices;   ///< device_features_raw.jsonl
    std::string protocols; ///< AllProtocols.json
};

namespace irdbFixture {

inline nlohmann::json atom(int type, int value) {
    return nlohmann::json{{"Type", type}, {"Value", value}};
}

inline nlohmann::json atoms(std::initializer_list<std::pair<int, int>> list) {
    nlohmann::json result = nlohmann::json::array();
    for (const auto& [type, value] : list) {
        result.push_back(atom(type, value));
    }
    return result;
}

inline nlohmann::json encoding(int bitType, nlohmann::json timing) {
    return nlohmann::json{{"BitType", bitType}, {"Atoms", std::move(timing)}};
}

inline nlohmann::json dataSegment(std::string name, nlohmann::json header, int bits,
                                  nlohmann::json encodings, nlohmann::json trailer,
                                  int totalLength) {
    return nlohmann::json{
        {"Name", std::move(name)},
        {"Header", std::move(header)},
        {"Payload",
         {{"EncodingType", 0},
          {"NumberOfBits", bits},
          {"Encodings", std::move(encodings)}}},
        {"Trailer", std::move(trailer)},
        {"TotalLength", totalLength}};
}

/// Код команды Logitech для протокола с одним сегментом данных.
inline std::string keyCode(const char* protocol, unsigned value) {
    char buffer[96];
    std::snprintf(buffer, sizeof buffer, "G:%s:()(0x%08X)():3", protocol, value);
    return buffer;
}

inline nlohmann::json command(std::string name, std::string type, std::string code, int protocol) {
    return nlohmann::json{{"Name", std::move(name)},
                          {"CommandTypeId", std::move(type)},
                          {"KeyCode", std::move(code)},
                          {"ProtocolId", protocol}};
}

inline nlohmann::json press(const char* name) {
    return nlohmann::json{{"__type", "IRPressAction"}, {"IRCommandName", name}, {"Order", 1}};
}

inline nlohmann::json segmentRef(const char* name, int type) {
    return nlohmann::json{{"SegmentName", name}, {"SegmentType", type}};
}

inline nlohmann::json protocol(int id, const char* name, int carrier, int repeats,
                               nlohmann::json irSegments, nlohmann::json codeSegments,
                               nlohmann::json keyCode) {
    return nlohmann::json{{"Id-", id},
                          {"Name", name},
                          {"CarrierFrequency", carrier},
                          {"PressMinimumRepeats", repeats},
                          {"IRSegments", std::move(irSegments)},
                          {"CodeSegments", std::move(codeSegments)},
                          {"KeyCode", std::move(keyCode)}};
}

inline std::string protocols() {
    using nlohmann::json;

    // Sony 12 Bit: 40 кГц, три повтора кадра, кадр 12 бит, пауза кадра 45 мс.
    const json sony = protocol(
        1, "Sony 12 Bit", 40000, 3,
        json::array({dataSegment("Sony 12 Bit", json::array({atom(1, 2400)}), 12,
                                 json::array({encoding(0, atoms({{0, 600}, {1, 600}})),
                                              encoding(1, atoms({{0, 600}, {1, 1200}}))}),
                                 json::array(), 45000)}),
        json::array(),
        json{{"Start", nullptr},
             {"Repeat", json::array({segmentRef("Sony 12 Bit", 1)})},
             {"Finish", nullptr}});

    // Toshiba 32 Bit: кадр данных и короткий код повтора (удержание кнопки).
    const json repeat = json{{"Name", "Toshiba 32 Bit KeyCodeRepeat"},
                             {"Atoms", atoms({{1, 8990}, {0, 2230}, {1, 568}, {0, 96077}})},
                             {"Header", json::array()},
                             {"Trailer", json::array()},
                             {"TotalLength", 0}};
    const json toshiba = protocol(
        2, "Toshiba 32 Bit", 38000, 1,
        json::array({dataSegment("Toshiba 32 Bit", json::array({atom(1, 8990), atom(0, 4490)}),
                                 32,
                                 json::array({encoding(0, atoms({{1, 568}, {0, 552}})),
                                              encoding(1, atoms({{1, 568}, {0, 1662}}))}),
                                 json::array({atom(1, 568)}), 107870)}),
        json::array({repeat}),
        json{{"Start", json::array({segmentRef("Toshiba 32 Bit", 1)})},
             {"Repeat", json::array({segmentRef("Toshiba 32 Bit KeyCodeRepeat", 0)})},
             {"Finish", nullptr}});

    // GoVideo 32 Bit: коды Samsung с тем же рисунком сигнала.
    const json goVideo = protocol(
        72, "GoVideo 32 Bit", 38000, 1,
        json::array({dataSegment("GoVideo 32 Bit", json::array({atom(1, 4500), atom(0, 4500)}),
                                 32,
                                 json::array({encoding(0, atoms({{1, 600}, {0, 1650}})),
                                              encoding(1, atoms({{1, 600}, {0, 500}}))}),
                                 json::array({atom(1, 600), atom(0, 49000)}), 0)}),
        json::array(),
        json{{"Start", nullptr},
             {"Repeat", json::array({segmentRef("GoVideo 32 Bit", 1)})},
             {"Finish", nullptr}});

    return json{{"Protocols", json::array({sony, toshiba, goVideo})}}.dump();
}

inline std::string devices() {
    using nlohmann::json;

    // --- Samsung: 24 команды цифр (на экран), аппаратные кнопки, питание и пять входов.
    json samsungCommands = json::array();
    samsungCommands.push_back(command("0", "Unknown", keyCode("GoVideo 32 Bit", 0x1F1F7788U), 72));
    for (unsigned i = 1; i < 24; ++i) {
        samsungCommands.push_back(
            command(std::to_string(i), "Unknown", keyCode("GoVideo 32 Bit", 0x1F1F0100U + i), 72));
    }
    const std::pair<const char*, const char*> hardKeys[] = {
        {"Menu", "Menu"},           {"Exit", "Exit"},
        {"Info", "Info"},           {"Guide", "Guide"},
        {"UpArrow", "PageUp"},      {"DownArrow", "PageDown"},
        {"Red", "Red"},             {"Green", "Green"},
        {"Yellow", "Yellow"},       {"Blue", "Blue"},
        {"VolumeUp", "VolumeUp"},   {"VolumeDown", "VolumeDown"},
        {"DirectionLeft", "DirectionLeft"},
        {"DirectionUp", "DirectionUp"},
        {"DirectionRight", "DirectionRight"},
        {"DirectionDown", "DirectionDown"},
        {"Select", "Select"},       {"ChannelUp", "ChannelUp"},
        {"ChannelDown", "ChannelDown"},
        {"Mute", "Mute"},           {"PrevChannel", "PrevChannel"},
        {"Rewind", "Rewind"},       {"SkipBack", "SkipBackward"},
        {"Record", "Record"},       {"Play", "Play"},
        {"Pause", "Pause"},         {"FastForward", "FastForward"},
        {"SkipForward", "SkipForward"},
        {"Stop", "Stop"},           {"Star", "Star"},
        {"NumberEnter", "NumberEnter"}};
    unsigned value = 0x1F1F1000U;
    for (const auto& [name, type] : hardKeys) {
        samsungCommands.push_back(command(name, type, keyCode("GoVideo 32 Bit", value++), 72));
    }
    samsungCommands.push_back(
        command("PowerOn", "PowerOn", keyCode("GoVideo 32 Bit", 0x1F1F2000U), 72));
    samsungCommands.push_back(
        command("PowerOff", "PowerOff", keyCode("GoVideo 32 Bit", 0x1F1F2001U), 72));
    json samsungInputs = json::array();
    for (int i = 1; i <= 5; ++i) {
        const std::string name = "Input" + std::to_string(i);
        samsungCommands.push_back(command(name, "Input", keyCode("GoVideo 32 Bit",
                                                                 0x1F1F3000U + i), 72));
        samsungInputs.push_back(json{{"InputName", "HDMI" + std::to_string(i)},
                                     {"InputOrder", i},
                                     {"Actions", json::array({press(name.c_str())})}});
    }
    const json samsung = {
        {"id", 1005},
        {"device",
         {{"Manufacturer", "Samsung"}, {"Model", "HLN501W"}, {"DeviceType", 1}}},
        {"commands", std::move(samsungCommands)},
        {"features",
         json::array(
             {{{"__type", "PowerFeature"},
               {"PowerTypeId", 1},
               {"PowerOnDelay", 1000},
               {"PowerOnActions", json::array({press("PowerOn")})},
               {"PowerOffActions", json::array({press("PowerOff")})}},
              {{"__type", "InputFeature"},
               {"InputDelay", 500},
               {"Inputs", std::move(samsungInputs)},
               {"NextActions", json::array()},
               {"PreviousActions", json::array()}}})}};

    // --- Sony: три команды и отдельные включение/выключение.
    const json sony = {
        {"id", 150},
        {"device",
         {{"Manufacturer", "Sony"}, {"Model", "STR-DE1015G"}, {"DeviceType", 5}}},
        {"commands",
         json::array({command("0", "Number0", "G:Sony 12 Bit:()(0x916)():3", 1),
                      command("1", "Number1", "G:Sony 12 Bit:()(0x016)():3", 1),
                      command("PowerOn", "PowerOn", "G:Sony 12 Bit:()(0x516)():3", 1),
                      command("PowerOff", "PowerOff", "G:Sony 12 Bit:()(0x416)():3", 1)})},
        {"features",
         json::array({{{"__type", "PowerFeature"},
                       {"PowerTypeId", 1},
                       {"PowerOnActions", json::array({press("PowerOn")})},
                       {"PowerOffActions", json::array({press("PowerOff")})}}})}};

    // --- Yamaha: переключатель питания и перебор входов (RelativeActions).
    const json yamaha = {
        {"id", 28603},
        {"device",
         {{"Manufacturer", "Yamaha"}, {"Model", "HTR-7650"}, {"DeviceType", 5}}},
        {"commands",
         json::array({command("0", "Unknown", "G:Toshiba 32 Bit:(0x5EA1A857)(Repeat)():3", 2),
                      command("1", "Unknown", "G:Toshiba 32 Bit:(0x5EA16897)(Repeat)():3", 2)})},
        {"features",
         json::array(
             {{{"__type", "PowerFeature"},
               {"PowerTypeId", 2},
               {"PowerToggleActions", json::array({press("0")})}},
              {{"__type", "InputFeature"},
               {"InputDelay", 1000},
               {"Inputs",
                json::array({{{"InputName", "TV"}, {"InputOrder", 1}},
                             {{"InputName", "DVD"}, {"InputOrder", 2}}})},
               {"NextActions", json::array({press("1")})},
               {"PreviousActions", json::array()}}})}};

    return (samsung.dump() + "\n") + (sony.dump() + "\n") + (yamaha.dump() + "\n");
}

/// Записывает файл в gzip: усечённый .gz распознаётся как повреждённый источник.
inline void writeGzip(const std::filesystem::path& path, const std::string& text) {
    gzFile file = gzopen(path.string().c_str(), "wb9");
    if (file == nullptr) {
        return;
    }
    gzwrite(file, text.data(), static_cast<unsigned>(text.size()));
    gzclose(file);
}

} // namespace irdbFixture

/// Исходники синтетической базы.
inline IrDatabaseSource irDatabaseSource() {
    return {irdbFixture::devices(), irdbFixture::protocols()};
}

/// Пути к записанным файлам базы.
struct IrDatabaseFiles {
    std::filesystem::path devices;
    std::filesystem::path protocols;
};

/// Пишет оба файла (.gz) в папку @p dir.
inline IrDatabaseFiles writeIrDatabaseSource(const std::filesystem::path& dir) {
    const IrDatabaseSource source = irDatabaseSource();
    IrDatabaseFiles files{dir / "device_features_raw.jsonl.gz", dir / "AllProtocols.json.gz"};
    irdbFixture::writeGzip(files.devices, source.devices);
    irdbFixture::writeGzip(files.protocols, source.protocols);
    return files;
}

} // namespace harmony::test
