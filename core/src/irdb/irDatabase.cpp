/**
 * @file irDatabase.cpp
 * @brief Своя база ИК-кодов: сборка из архива Logitech и чтение (каталог, поиск, записи).
 *
 * Запись устройства в базе — компактный JSON только с нужными полями:
 * @code
 *   {"id":…,"m":"Производитель","n":"Модель","t":тип,
 *    "c":[[имя,тип,код,протокол],…],
 *    "p":[тип питания,задержка,[вкл],[выкл],[переключатель]],
 *    "i":[задержка,[[вход,[действия]],…],[следующий],[предыдущий]]}
 *   действие: ["P",команда] | ["D",мс] | ["S",состояние,значение]
 * @endcode
 */

#include "harmony/core/irdb/irDatabase.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <fstream>
#include <map>
#include <mutex>
#include <nlohmann/json.hpp>
#include <unordered_map>
#include <unordered_set>
#include <zlib.h>

namespace harmony::irdb {

using nlohmann::json;

namespace {

constexpr std::array<char, 12> kMagic = {'H', 'A', 'R', 'M', 'O', 'N',
                                         'Y', 'I', 'R', 'D', 'B', '\0'};
constexpr std::uint32_t kVersion = 3; ///< 3 — паспорт сборки после индекса сигналов
constexpr std::size_t kBlockSize =
    64; ///< Устройств в блоке: сжатие почти как у целого файла, чтение быстрое
constexpr std::size_t kHeaderSize = 12 + 4 * 3 + 8 * 7;

std::string pathText(const std::filesystem::path& path) {
    const auto text = path.u8string();
    return {reinterpret_cast<const char*>(text.data()), text.size()};
}

// --- Сжатие ---------------------------------------------------------------------

std::string deflateText(std::string_view text, int level = 9) {
    uLongf size = compressBound(static_cast<uLong>(text.size()));
    std::string out(size, '\0');
    compress2(reinterpret_cast<Bytef*>(out.data()), &size,
              reinterpret_cast<const Bytef*>(text.data()), static_cast<uLong>(text.size()), level);
    out.resize(size);
    out.shrink_to_fit(); // при сборке таких строк сотни тысяч: запас под compressBound не держим
    return out;
}

Result<std::string> inflateText(std::string_view data, std::size_t size) {
    std::string out(size, '\0');
    uLongf length = static_cast<uLongf>(size);
    if (uncompress(reinterpret_cast<Bytef*>(out.data()), &length,
                   reinterpret_cast<const Bytef*>(data.data()),
                   static_cast<uLong>(data.size())) != Z_OK ||
        length != size) {
        return makeError(ErrorCode::Format,
                         "the IR code database is corrupt: a block cannot be unpacked");
    }
    return out;
}

/// Распаковка без известного размера (каталог, протоколы): по частям.
Result<std::string> inflateAll(std::string_view data) {
    z_stream stream{};
    if (inflateInit(&stream) != Z_OK) {
        return makeError(ErrorCode::Format, "the IR code database is corrupt");
    }
    stream.next_in = reinterpret_cast<Bytef*>(const_cast<char*>(data.data()));
    stream.avail_in = static_cast<uInt>(data.size());
    std::string out;
    std::array<char, 1 << 16> buffer{};
    int status = Z_OK;
    while (status == Z_OK) {
        stream.next_out = reinterpret_cast<Bytef*>(buffer.data());
        stream.avail_out = static_cast<uInt>(buffer.size());
        status = inflate(&stream, Z_NO_FLUSH);
        out.append(buffer.data(), buffer.size() - stream.avail_out);
    }
    inflateEnd(&stream);
    if (status != Z_STREAM_END) {
        return makeError(ErrorCode::Format, "the IR code database is corrupt");
    }
    return out;
}

// --- Чтение исходных файлов (.gz или как есть) --------------------------------------

/// Построчное чтение файла; .gz распаковывается на лету (zlib различает сам).
class LineReader {
public:
    ~LineReader() {
        if (file_ != nullptr) {
            gzclose(file_);
        }
    }

    Result<void> open(const std::filesystem::path& path) {
        std::error_code error;
        total_ = std::filesystem::file_size(path, error);
        if (error) {
            return makeError(ErrorCode::NotFound, "file not found: %1", {pathText(path)});
        }
#ifdef _WIN32
        file_ = gzopen_w(path.c_str(), "rb");
#else
        file_ = gzopen(path.c_str(), "rb");
#endif
        if (file_ == nullptr) {
            return makeError(ErrorCode::Io, "could not open the file: %1", {pathText(path)});
        }
        gzbuffer(file_, 1 << 20);
        return {};
    }

    /// Следующая строка без перевода строки; false — файл кончился.
    bool next(std::string& line) {
        line.clear();
        for (;;) {
            if (position_ < buffer_.size()) {
                const auto end = buffer_.find('\n', position_);
                if (end != std::string::npos) {
                    line.append(buffer_, position_, end - position_);
                    position_ = end + 1;
                    return true;
                }
                line.append(buffer_, position_, std::string::npos);
            }
            buffer_.resize(1 << 20);
            const int read = gzread(file_, buffer_.data(), static_cast<unsigned>(buffer_.size()));
            position_ = 0;
            if (read <= 0) {
                buffer_.clear();
                return !line.empty();
            }
            buffer_.resize(static_cast<std::size_t>(read));
        }
    }

    /// Прочитано байт исходного (сжатого) файла.
    [[nodiscard]] std::uint64_t done() const {
        const auto offset = gzoffset(file_);
        return offset > 0 ? static_cast<std::uint64_t>(offset) : 0;
    }

    [[nodiscard]] std::uint64_t total() const { return total_; }

    /// Ошибка распаковки или усечённый gzip не должны считаться концом файла.
    [[nodiscard]] bool failed() const {
        int code = Z_OK;
        gzerror(file_, &code);
        return code != Z_OK && code != Z_STREAM_END;
    }

private:
    gzFile file_ = nullptr;
    std::string buffer_;
    std::size_t position_ = 0;
    std::uint64_t total_ = 0;
};

Result<std::string> readWhole(const std::filesystem::path& path) {
    LineReader reader;
    if (auto opened = reader.open(path); !opened) {
        return std::unexpected(std::move(opened.error()));
    }
    std::string text;
    std::string line;
    while (reader.next(line)) {
        text += line;
        text += '\n';
    }
    if (reader.failed()) {
        return makeError(ErrorCode::Format, "the source archive is truncated or corrupt: %1", {pathText(path)});
    }
    return text;
}

// --- Запись из архива → компактная запись -------------------------------------------

std::int64_t number(const json& object, const char* key, std::int64_t fallback) {
    if (!object.is_object()) {
        return fallback;
    }
    const auto it = object.find(key);
    return it != object.end() && it->is_number() ? it->get<std::int64_t>() : fallback;
}

std::string text(const json& object, const char* key) {
    if (!object.is_object()) {
        return {};
    }
    const auto it = object.find(key);
    return it != object.end() && it->is_string() ? it->get<std::string>() : std::string();
}

/**
 * @brief Имя производителя или модели для каталога: без пробелов по краям,
 * табуляции и переводы строк — пробелы (каталог разделяется ими).
 */
std::string cleanName(std::string name) {
    for (char& c : name) {
        if (c == '\t' || c == '\n' || c == '\r') {
            c = ' ';
        }
    }
    const auto first = name.find_first_not_of(' ');
    if (first == std::string::npos) {
        return {};
    }
    return name.substr(first, name.find_last_not_of(' ') - first + 1);
}

/// Действия Logitech по полю Order → компактный вид.
json compactActions(const json& list) {
    std::vector<std::pair<std::int64_t, json>> ordered;
    if (list.is_array()) {
        for (const json& action : list) {
            const std::string type = text(action, "__type");
            json item;
            if (type == "IRPressAction") {
                const std::string command = text(action, "IRCommandName");
                if (command.empty()) {
                    continue;
                }
                item = json::array({"P", command});
            } else if (type == "IRDelayAction") {
                item = json::array({"D", number(action, "Delay", 0)});
            } else if (type == "IRDevAction") {
                const std::string state = text(action, "StateName");
                if (state.empty()) {
                    continue;
                }
                item = json::array({"S", state, text(action, "StateValue")});
            } else {
                continue;
            }
            ordered.emplace_back(number(action, "Order", 0), std::move(item));
        }
    }
    std::ranges::stable_sort(ordered, {}, &std::pair<std::int64_t, json>::first);
    json out = json::array();
    for (auto& [order, item] : ordered) {
        out.push_back(std::move(item));
    }
    return out;
}

/// Строка device_features_raw → компактная запись; пусто — пропустить.
json compactRecord(const json& source) {
    const json& device = source.contains("device") ? source["device"] : json();
    const std::string manufacturer = cleanName(text(device, "Manufacturer"));
    const std::string model = cleanName(text(device, "Model"));
    const auto id = number(source, "id", 0);
    if (manufacturer.empty() || model.empty() || id <= 0) {
        return {};
    }
    json record = {
        {"id", id}, {"m", manufacturer}, {"n", model}, {"t", number(device, "DeviceType", 0)}};
    json commands = json::array();
    if (source.contains("commands") && source["commands"].is_array()) {
        for (const json& command : source["commands"]) {
            const std::string name = text(command, "Name");
            const std::string keyCode = text(command, "KeyCode");
            if (name.empty() || keyCode.empty()) {
                continue;
            }
            commands.push_back(json::array({name, text(command, "CommandTypeId"), keyCode,
                                            number(command, "ProtocolId", -1)}));
        }
    }
    record["c"] = std::move(commands);
    if (source.contains("features") && source["features"].is_array()) {
        for (const json& feature : source["features"]) {
            const std::string type = text(feature, "__type");
            if (type == "PowerFeature") {
                record["p"] = json::array(
                    {number(feature, "PowerTypeId", 0),
                     number(feature, "PowerOnDelay", number(feature, "DefaultPowerOnDelay", 0)),
                     compactActions(feature.value("PowerOnActions", json())),
                     compactActions(feature.value("PowerOffActions", json())),
                     compactActions(feature.value("PowerToggleActions", json()))});
            } else if (type == "InputFeature") {
                std::vector<std::pair<std::int64_t, json>> inputs;
                if (feature.contains("Inputs") && feature["Inputs"].is_array()) {
                    for (const json& input : feature["Inputs"]) {
                        const std::string name = text(input, "InputName");
                        if (!name.empty()) {
                            inputs.emplace_back(number(input, "InputOrder", 0),
                                                json::array({name, compactActions(input.value(
                                                                       "Actions", json()))}));
                        }
                    }
                }
                std::ranges::stable_sort(inputs, {}, &std::pair<std::int64_t, json>::first);
                json list = json::array();
                for (auto& [order, input] : inputs) {
                    list.push_back(std::move(input));
                }
                record["i"] = json::array(
                    {number(feature, "InputDelay", number(feature, "DefaultInputDelay", 0)),
                     std::move(list), compactActions(feature.value("NextActions", json())),
                     compactActions(feature.value("PreviousActions", json()))});
            }
        }
    }
    return record;
}

// --- Компактная запись → DbDevice ----------------------------------------------------

std::vector<DbAction> readActions(const json& list) {
    std::vector<DbAction> out;
    if (!list.is_array()) {
        return out;
    }
    for (const json& item : list) {
        if (!item.is_array() || item.empty() || !item[0].is_string()) {
            continue;
        }
        const std::string kind = item[0].get<std::string>();
        DbAction action;
        if (kind == "P" && item.size() >= 2 && item[1].is_string()) {
            action.kind = DbAction::Kind::Press;
            action.command = item[1].get<std::string>();
        } else if (kind == "D" && item.size() >= 2 && item[1].is_number()) {
            action.kind = DbAction::Kind::Delay;
            action.delayMs = item[1].get<int>();
        } else if (kind == "S" && item.size() >= 3 && item[1].is_string() && item[2].is_string()) {
            action.kind = DbAction::Kind::State;
            action.stateName = item[1].get<std::string>();
            action.stateValue = item[2].get<std::string>();
        } else {
            continue;
        }
        out.push_back(std::move(action));
    }
    return out;
}

Result<DbDevice> readRecord(std::string_view line) {
    const json record = json::parse(line.begin(), line.end(), nullptr, false);
    if (record.is_discarded() || !record.is_object()) {
        return makeError(ErrorCode::Format,
                         "the IR code database is corrupt: a record cannot be read");
    }
    DbDevice device;
    device.id = static_cast<std::uint64_t>(std::max<std::int64_t>(0, number(record, "id", 0)));
    device.manufacturer = text(record, "m");
    device.model = text(record, "n");
    device.deviceType = static_cast<int>(number(record, "t", 0));
    if (record.contains("c") && record["c"].is_array()) {
        for (const json& item : record["c"]) {
            if (item.is_array() && item.size() >= 4 && item[0].is_string() && item[2].is_string()) {
                device.commands.push_back(
                    {item[0].get<std::string>(),
                     item[1].is_string() ? item[1].get<std::string>() : std::string(),
                     item[2].get<std::string>(), item[3].is_number() ? item[3].get<int>() : -1});
            }
        }
    }
    if (record.contains("p") && record["p"].is_array() && record["p"].size() >= 5) {
        const json& power = record["p"];
        device.powerType = power[0].is_number() ? power[0].get<int>() : 0;
        device.powerOnDelayMs = power[1].is_number() ? power[1].get<int>() : 0;
        device.powerOn = readActions(power[2]);
        device.powerOff = readActions(power[3]);
        device.powerToggle = readActions(power[4]);
    }
    if (record.contains("i") && record["i"].is_array() && record["i"].size() >= 4) {
        const json& inputs = record["i"];
        device.inputDelayMs = inputs[0].is_number() ? inputs[0].get<int>() : 0;
        if (inputs[1].is_array()) {
            for (const json& input : inputs[1]) {
                if (input.is_array() && input.size() >= 2 && input[0].is_string()) {
                    device.inputs.push_back({input[0].get<std::string>(), readActions(input[1])});
                }
            }
        }
        device.inputNext = readActions(inputs[2]);
        device.inputPrev = readActions(inputs[3]);
    }
    return device;
}

// --- Двоичная запись чисел ---------------------------------------------------------------

void put32(std::string& out, std::uint32_t value) {
    for (int i = 0; i < 4; ++i) {
        out.push_back(static_cast<char>((value >> (8 * i)) & 0xFF));
    }
}

void put64(std::string& out, std::uint64_t value) {
    for (int i = 0; i < 8; ++i) {
        out.push_back(static_cast<char>((value >> (8 * i)) & 0xFF));
    }
}

std::uint64_t get(std::string_view data, std::size_t at, int bytes) {
    std::uint64_t value = 0;
    for (int i = 0; i < bytes; ++i) {
        value |= static_cast<std::uint64_t>(
                     static_cast<unsigned char>(data[at + static_cast<std::size_t>(i)]))
                 << (8 * i);
    }
    return value;
}

/// Строка в нижнем регистре для поиска (латиница; остальное как есть).
std::string lower(std::string_view text) {
    std::string out(text);
    for (char& c : out) {
        if (c >= 'A' && c <= 'Z') {
            c = static_cast<char>(c - 'A' + 'a');
        }
    }
    return out;
}

/// Слова запроса в нижнем регистре.
std::vector<std::string> splitWords(std::string_view text) {
    std::vector<std::string> words;
    const std::string query = lower(text);
    std::size_t start = 0;
    for (std::size_t i = 0; i <= query.size(); ++i) {
        if (i == query.size() || query[i] == ' ' || query[i] == '\t') {
            if (i > start) {
                words.push_back(query.substr(start, i - start));
            }
            start = i + 1;
        }
    }
    return words;
}

bool containsAll(const std::string& key, const std::vector<std::string>& words) {
    return std::ranges::all_of(
        words, [&key](const std::string& word) { return key.find(word) != std::string::npos; });
}

} // namespace

std::string deviceTypeName(int code) {
    static const std::map<int, const char*> types = {
        {1, "Television"},
        {2, "Vcr"},
        {3, "Cd"},
        {4, "DvdCd"},
        {5, "Receiver"},
        {6, "Cassette"},
        {7, "Projector"},
        {8, "Computer"},
        {9, "GameConsole"},
        {10, "Laptop"},
        {11, "CdJukebox"},
        {12, "SetTopBox"},
        {13, "Monitor"},
        {14, "CableBox"},
        {15, "TvVcr"},
        {16, "Satellite"},
        {18, "Pvr"},
        {19, "Amplifier"},
        {20, "Dat"},
        {21, "CdRadioCassette"},
        {22, "DvdCdRadio"},
        {23, "DvdCdGame"},
        {24, "Lighting"},
        {25, "DvdVcr"},
        {28, "Laserdisc"},
        {29, "Minidisc"},
        {30, "AudioVideoSwitch"},
        {32, "DvdVcrReceiver"},
        {33, "MusicServer"},
        {34, "DvdRecorder"},
        {35, "MediaCenter"},
        {36, "RadioTuner"},
        {37, "TvDvd"},
        {38, "TvDvdVcr"},
        {44, "HomeAppliance"},
        {45, "ClimateControl"},
        {47, "DvdrVcr"},
        {48, "TvHdd"},
        {51, "SetTopBox"},
        {52, "SetTopBox"},
        {53, "HomeAppliance"},
        {59, "ClimateControl"},
        {63, "SetTopBox"},
    };
    const auto it = types.find(code);
    return it != types.end() ? it->second : "Unknown";
}

// --- Сборка -------------------------------------------------------------------------------

Result<BuildStats> buildDatabase(const std::filesystem::path& devicesFile,
                                 const std::filesystem::path& protocolsFile,
                                 const std::filesystem::path& output, const std::string& source,
                                 const BuildProgress& progress) {
    // Протоколы — сразу: без них база бесполезна, лучше узнать до долгого прохода.
    auto protocols = readWhole(protocolsFile);
    if (!protocols) {
        return std::unexpected(std::move(protocols.error()));
    }
    auto library = ProtocolLibrary::parse(*protocols);
    if (!library) {
        return std::unexpected(std::move(library.error()));
    }

    LineReader reader;
    if (auto opened = reader.open(devicesFile); !opened) {
        return std::unexpected(std::move(opened.error()));
    }

    std::filesystem::path temporary = output;
    temporary += ".tmp";
    std::ofstream out(temporary, std::ios::binary | std::ios::trunc);
    if (!out) {
        return makeError(ErrorCode::Io, "could not create the file: %1", {pathText(temporary)});
    }
    out.write(std::string(kHeaderSize + 2 + source.size(), '\0').data(),
              static_cast<std::streamsize>(kHeaderSize + 2 + source.size()));

    // Записи держатся в памяти сжатыми по одной (около 250 МБ на весь архив), затем
    // сортируются по алфавиту и пишутся блоками: соседние модели одного
    // производителя похожи, и база получается в 2,5 раза меньше, чем в порядке файла.
    struct Item {
        CatalogueEntry entry;
        std::string packed;  ///< Запись, сжатая отдельно
        std::uint32_t size;  ///< Размер записи без сжатия
        std::uint32_t order; ///< Номер в порядке файла (для индекса сигналов)
    };
    std::vector<Item> items;
    // Сигналы всех команд — чтобы искать модель по кнопкам родного пульта.
    // Одинаковые коды у разных моделей встречаются постоянно: кадр считается один раз.
    SignalIndexBuilder signalIndex;
    DbCommand command;
    std::unordered_set<std::uint64_t> seen;
    BuildStats stats;
    std::string line;
    std::size_t lines = 0;
    auto cancelled = [&] {
        out.close();
        std::error_code ignored;
        std::filesystem::remove(temporary, ignored);
        return makeError(ErrorCode::Cancelled, "building the database was cancelled");
    };
    while (reader.next(line)) {
        if (++lines % 256 == 0 && progress && !progress(reader.done(), reader.total())) {
            return cancelled();
        }
        if (line.empty()) {
            continue;
        }
        const json original = json::parse(line, nullptr, false);
        if (original.is_discarded()) {
            ++stats.skippedLines;
            continue;
        }
        json record = compactRecord(original);
        if (record.is_null()) {
            ++stats.skippedLines;
            continue;
        }
        const auto id = record["id"].get<std::uint64_t>();
        if (!seen.insert(id).second) {
            ++stats.skippedLines;
            continue; // повтор записи: берётся первая
        }
        stats.commands += record["c"].size();
        const auto order = static_cast<std::uint32_t>(items.size());
        for (const json& item : record["c"]) {
            command.keyCode = item[2].get<std::string>();
            command.protocolId = item[3].get<int>();
            if (auto signal = library->render(command)) {
                signalIndex.add(order, *signal);
            }
        }
        const std::string text = record.dump(-1, ' ', false, json::error_handler_t::replace);
        items.push_back({{id, record["m"].get<std::string>(), record["n"].get<std::string>(),
                          record["t"].get<int>()},
                         deflateText(text, 1),
                         static_cast<std::uint32_t>(text.size()),
                         order});
    }
    if (reader.failed()) {
        out.close();
        std::error_code ignored;
        std::filesystem::remove(temporary, ignored);
        return makeError(ErrorCode::Format, "the source archive is truncated or corrupt: %1", {pathText(devicesFile)});
    }
    if (items.empty()) {
        out.close();
        std::error_code ignored;
        std::filesystem::remove(temporary, ignored);
        return makeError(ErrorCode::Format, "the file contains no devices: %1",
                         {pathText(devicesFile)});
    }
    std::ranges::sort(items, [](const Item& a, const Item& b) {
        const std::string left = lower(a.entry.manufacturer);
        const std::string right = lower(b.entry.manufacturer);
        if (left != right) {
            return left < right;
        }
        return lower(a.entry.model) < lower(b.entry.model);
    });
    stats.devices = items.size();
    std::vector<std::uint32_t> catalogueOf(items.size()); // номер в порядке файла → в каталоге
    for (std::size_t i = 0; i < items.size(); ++i) {
        catalogueOf[items[i].order] = static_cast<std::uint32_t>(i);
    }

    std::string table;
    std::uint32_t blocks = 0;
    for (std::size_t begin = 0; begin < items.size(); begin += kBlockSize) {
        if (progress && !progress(reader.total(), reader.total())) {
            return cancelled();
        }
        std::string block;
        for (std::size_t i = begin; i < std::min(items.size(), begin + kBlockSize); ++i) {
            auto text = inflateText(items[i].packed, items[i].size);
            if (!text) {
                return std::unexpected(std::move(text.error()));
            }
            block += *text;
            block += '\n';
            std::string().swap(items[i].packed); // память — сразу
        }
        const std::string packed = deflateText(block);
        put64(table, static_cast<std::uint64_t>(out.tellp()));
        put32(table, static_cast<std::uint32_t>(packed.size()));
        put32(table, static_cast<std::uint32_t>(block.size()));
        out.write(packed.data(), static_cast<std::streamsize>(packed.size()));
        ++blocks;
    }
    std::string catalogue;
    for (std::size_t i = 0; i < items.size(); ++i) {
        const CatalogueEntry& entry = items[i].entry;
        catalogue += std::to_string(entry.id) + '\t' + entry.manufacturer + '\t' + entry.model +
                     '\t' + std::to_string(entry.deviceType) + '\t' + std::to_string(i) + '\n';
    }
    const auto tableOffset = static_cast<std::uint64_t>(out.tellp());
    out.write(table.data(), static_cast<std::streamsize>(table.size()));
    const std::string packedCatalogue = deflateText(catalogue);
    const auto catalogueOffset = static_cast<std::uint64_t>(out.tellp());
    out.write(packedCatalogue.data(), static_cast<std::streamsize>(packedCatalogue.size()));
    const std::string packedProtocols = deflateText(*protocols);
    const auto protocolsOffset = static_cast<std::uint64_t>(out.tellp());
    out.write(packedProtocols.data(), static_cast<std::streamsize>(packedProtocols.size()));
    stats.signalFrames = signalIndex.frameCount();
    const std::string packedSignals = deflateText(signalIndex.write(catalogueOf), 6);
    signalIndex = {}; // память — сразу
    const auto signalsOffset = static_cast<std::uint64_t>(out.tellp());
    out.write(packedSignals.data(), static_cast<std::streamsize>(packedSignals.size()));
    const auto builtAt = static_cast<std::int64_t>(std::time(nullptr));
    const auto metadata = json{{"builtAt", builtAt}, {"source", source},
                               {"devices", stats.devices}, {"commands", stats.commands},
                               {"skippedLines", stats.skippedLines}, {"signalFrames", stats.signalFrames}}.dump();
    out.write(metadata.data(), static_cast<std::streamsize>(metadata.size()));

    std::string header(kMagic.data(), kMagic.size()); // в начале файла было оставлено место
    put32(header, kVersion);
    put32(header, static_cast<std::uint32_t>(items.size()));
    put32(header, blocks);
    put64(header, catalogueOffset);
    put64(header, packedCatalogue.size());
    put64(header, protocolsOffset);
    put64(header, packedProtocols.size());
    put64(header, tableOffset);
    put64(header, signalsOffset);
    put64(header, packedSignals.size());
    header.push_back(static_cast<char>(source.size() & 0xFF));
    header.push_back(static_cast<char>((source.size() >> 8) & 0xFF));
    header += source;
    out.seekp(0);
    out.write(header.data(), static_cast<std::streamsize>(header.size()));
    out.close();
    if (!out) {
        return makeError(ErrorCode::Io, "error writing the file: %1", {pathText(temporary)});
    }
    std::error_code error;
    std::filesystem::rename(temporary, output, error);
    if (error) {
        return makeError(ErrorCode::Io, "could not replace the file: %1", {pathText(output)});
    }
    if (progress) {
        progress(reader.total(), reader.total());
    }
    return stats;
}

// --- Чтение -------------------------------------------------------------------------------

struct IrDatabase::Impl {
    std::filesystem::path file;
    std::string source;
    std::uint32_t version = 2;
    std::string metadata;
    std::vector<CatalogueEntry> entries;
    std::vector<std::string> keys; ///< «производитель модель» в нижнем регистре
    std::vector<std::uint32_t> positions; ///< Номер записи каждого устройства каталога
    struct Block {
        std::uint64_t offset;
        std::uint32_t packed;
        std::uint32_t size;
    };
    std::vector<Block> blocks;
    ProtocolLibrary protocols;
    std::uint64_t signalsOffset = 0;
    std::uint64_t signalsSize = 0;

    // Индекс сигналов — при первом обращении.
    mutable std::mutex signalsLock;
    mutable std::shared_ptr<const SignalIndex> signals;

    // Последний распакованный блок: соседние устройства обычно смотрят подряд.
    mutable std::mutex lock;
    mutable std::size_t cachedBlock = static_cast<std::size_t>(-1);
    mutable std::vector<std::string> cachedRecords;
};

IrDatabase::IrDatabase() : impl_(std::make_unique<Impl>()) {}
IrDatabase::IrDatabase(IrDatabase&&) noexcept = default;
IrDatabase& IrDatabase::operator=(IrDatabase&&) noexcept = default;
IrDatabase::~IrDatabase() = default;

Result<IrDatabase> IrDatabase::open(const std::filesystem::path& file) {
    std::ifstream in(file, std::ios::binary);
    if (!in) {
        return makeError(ErrorCode::NotFound, "file not found: %1", {pathText(file)});
    }
    auto readAt = [&in](std::uint64_t offset, std::size_t size) {
        std::string data(size, '\0');
        in.seekg(static_cast<std::streamoff>(offset));
        in.read(data.data(), static_cast<std::streamsize>(size));
        return in.gcount() == static_cast<std::streamsize>(size) ? data : std::string();
    };
    const std::string header = readAt(0, kHeaderSize + 2);
    if (header.size() != kHeaderSize + 2 ||
        std::memcmp(header.data(), kMagic.data(), kMagic.size()) != 0) {
        return makeError(ErrorCode::Format, "this is not a Harmony IR code database: %1",
                         {pathText(file)});
    }
    const auto version = get(header, 12, 4);
    if (version != 2 && version != kVersion) {
        return makeError(ErrorCode::Format,
                         "the IR code database has a different version — build it again");
    }
    IrDatabase database;
    Impl& impl = *database.impl_;
    impl.file = file;
    impl.version = static_cast<std::uint32_t>(version);
    const auto count = static_cast<std::size_t>(get(header, 16, 4));
    const auto blockCount = static_cast<std::size_t>(get(header, 20, 4));
    const std::uint64_t catalogueOffset = get(header, 24, 8);
    const std::uint64_t catalogueSize = get(header, 32, 8);
    const std::uint64_t protocolsOffset = get(header, 40, 8);
    const std::uint64_t protocolsSize = get(header, 48, 8);
    const std::uint64_t tableOffset = get(header, 56, 8);
    impl.signalsOffset = get(header, 64, 8);
    impl.signalsSize = get(header, 72, 8);
    const auto sourceSize = static_cast<std::size_t>(get(header, kHeaderSize, 2));
    impl.source = readAt(kHeaderSize + 2, sourceSize);
    if (version >= 3) {
        const auto offset = impl.signalsOffset + impl.signalsSize;
        std::error_code error;
        const auto size = std::filesystem::file_size(file, error);
        if (error || offset > size || size - offset > 65536) {
            return makeError(ErrorCode::Format, "the IR code database has invalid build metadata");
        }
        impl.metadata = readAt(offset, static_cast<std::size_t>(size - offset));
        if (json::parse(impl.metadata, nullptr, false).is_discarded()) {
            return makeError(ErrorCode::Format, "the IR code database has invalid build metadata");
        }
    }

    const std::string table = readAt(tableOffset, blockCount * 16);
    if (table.size() != blockCount * 16) {
        return makeError(ErrorCode::Format, "the IR code database is corrupt: no block table");
    }
    for (std::size_t i = 0; i < blockCount; ++i) {
        impl.blocks.push_back({get(table, i * 16, 8),
                               static_cast<std::uint32_t>(get(table, i * 16 + 8, 4)),
                               static_cast<std::uint32_t>(get(table, i * 16 + 12, 4))});
    }

    auto catalogue = inflateAll(readAt(catalogueOffset, static_cast<std::size_t>(catalogueSize)));
    if (!catalogue) {
        return std::unexpected(std::move(catalogue.error()));
    }
    std::size_t at = 0;
    while (at < catalogue->size()) {
        const auto end = catalogue->find('\n', at);
        const std::string_view row(catalogue->data() + at,
                                   (end == std::string::npos ? catalogue->size() : end) - at);
        at = end == std::string::npos ? catalogue->size() : end + 1;
        std::array<std::string_view, 5> fields;
        std::size_t field = 0;
        std::size_t start = 0;
        for (std::size_t i = 0; i <= row.size() && field < 5; ++i) {
            if (i == row.size() || row[i] == '\t') {
                fields[field++] = row.substr(start, i - start);
                start = i + 1;
            }
        }
        if (field != 5) {
            continue;
        }
        CatalogueEntry entry;
        entry.id = std::strtoull(std::string(fields[0]).c_str(), nullptr, 10);
        entry.manufacturer = fields[1];
        entry.model = fields[2];
        entry.deviceType = std::atoi(std::string(fields[3]).c_str());
        impl.keys.push_back(lower(entry.manufacturer + ' ' + entry.model));
        impl.positions.push_back(
            static_cast<std::uint32_t>(std::strtoul(std::string(fields[4]).c_str(), nullptr, 10)));
        impl.entries.push_back(std::move(entry));
    }
    if (impl.entries.size() != count || (count + kBlockSize - 1) / kBlockSize != blockCount) {
        return makeError(ErrorCode::Format,
                         "the IR code database is corrupt: the catalogue does not match the "
                         "records");
    }

    auto protocols = inflateAll(readAt(protocolsOffset, static_cast<std::size_t>(protocolsSize)));
    if (!protocols) {
        return std::unexpected(std::move(protocols.error()));
    }
    auto library = ProtocolLibrary::parse(*protocols);
    if (!library) {
        return std::unexpected(std::move(library.error()));
    }
    impl.protocols = std::move(*library);
    return database;
}

const std::vector<CatalogueEntry>& IrDatabase::entries() const {
    return impl_->entries;
}

std::vector<std::size_t> IrDatabase::search(std::string_view text, std::size_t max,
                                            std::size_t* found) const {
    const std::vector<std::string> words = splitWords(text);
    std::vector<std::size_t> result;
    std::size_t total = 0;
    for (std::size_t i = 0; i < impl_->keys.size(); ++i) {
        if (containsAll(impl_->keys[i], words)) {
            ++total;
            if (result.size() < max) {
                result.push_back(i);
            }
        }
    }
    if (found != nullptr) {
        *found = total;
    }
    return result;
}

Result<DbDevice> IrDatabase::device(std::size_t entry) const {
    if (entry >= impl_->entries.size()) {
        return makeError(ErrorCode::NotFound, "no device %1", {std::to_string(entry)});
    }
    const std::size_t position = impl_->positions[entry];
    const std::size_t blockIndex = position / kBlockSize;
    const std::size_t slot = position % kBlockSize;
    if (blockIndex >= impl_->blocks.size()) {
        return makeError(ErrorCode::Format, "the IR code database is corrupt: record missing");
    }
    std::lock_guard guard(impl_->lock);
    if (impl_->cachedBlock != blockIndex) {
        const Impl::Block& block = impl_->blocks[blockIndex];
        std::ifstream in(impl_->file, std::ios::binary);
        std::string packed(block.packed, '\0');
        in.seekg(static_cast<std::streamoff>(block.offset));
        in.read(packed.data(), static_cast<std::streamsize>(packed.size()));
        if (!in) {
            return makeError(ErrorCode::Io, "error reading the file: %1", {pathText(impl_->file)});
        }
        auto data = inflateText(packed, block.size);
        if (!data) {
            return std::unexpected(std::move(data.error()));
        }
        impl_->cachedRecords.clear();
        std::size_t at = 0;
        while (at < data->size()) {
            const auto end = data->find('\n', at);
            impl_->cachedRecords.emplace_back(*data, at,
                                              (end == std::string::npos ? data->size() : end) - at);
            at = end == std::string::npos ? data->size() : end + 1;
        }
        impl_->cachedBlock = blockIndex;
    }
    if (slot >= impl_->cachedRecords.size()) {
        return makeError(ErrorCode::Format, "the IR code database is corrupt: record missing");
    }
    return readRecord(impl_->cachedRecords[slot]);
}

bool IrDatabase::matches(std::size_t entry, std::string_view text) const {
    return entry < impl_->keys.size() && containsAll(impl_->keys[entry], splitWords(text));
}

Result<std::shared_ptr<const SignalIndex>> IrDatabase::signalIndex() const {
    std::lock_guard guard(impl_->signalsLock);
    if (impl_->signals) {
        return impl_->signals;
    }
    std::ifstream in(impl_->file, std::ios::binary);
    std::string packed(static_cast<std::size_t>(impl_->signalsSize), '\0');
    in.seekg(static_cast<std::streamoff>(impl_->signalsOffset));
    in.read(packed.data(), static_cast<std::streamsize>(packed.size()));
    if (!in) {
        return makeError(ErrorCode::Io, "error reading the file: %1", {pathText(impl_->file)});
    }
    auto data = inflateAll(packed);
    if (!data) {
        return std::unexpected(std::move(data.error()));
    }
    std::string().swap(packed);
    auto index = SignalIndex::parse(*data);
    if (!index) {
        return std::unexpected(std::move(index.error()));
    }
    impl_->signals = std::make_shared<const SignalIndex>(std::move(*index));
    return impl_->signals;
}

const ProtocolLibrary& IrDatabase::protocols() const {
    return impl_->protocols;
}

const std::string& IrDatabase::source() const {
    return impl_->source;
}

std::uint32_t IrDatabase::formatVersion() const {
    return impl_->version;
}

const std::string& IrDatabase::buildInfo() const {
    return impl_->metadata;
}

std::vector<CandidateScore> rankByCommandNames(const IrDatabase& database,
                                               const std::vector<std::size_t>& candidates,
                                               const std::vector<std::string>& names) {
    auto lower = [](std::string text) {
        std::ranges::transform(text, text.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        return text;
    };
    std::unordered_set<std::string> wanted;
    for (const std::string& name : names) {
        wanted.insert(lower(name));
    }
    std::vector<CandidateScore> scores;
    for (const std::size_t entry : candidates) {
        const auto record = database.device(entry);
        if (!record) {
            continue;
        }
        std::unordered_set<std::string> have;
        for (const DbCommand& command : record->commands) {
            have.insert(lower(command.name));
        }
        CandidateScore score;
        score.entry = entry;
        score.total = wanted.size();
        for (const std::string& name : wanted) {
            score.matched += have.count(name);
        }
        score.extra = have.size() - score.matched;
        scores.push_back(score);
    }
    std::ranges::stable_sort(scores, [](const CandidateScore& a, const CandidateScore& b) {
        return a.matched != b.matched ? a.matched > b.matched : a.extra < b.extra;
    });
    return scores;
}

} // namespace harmony::irdb
