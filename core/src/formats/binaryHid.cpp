/** @file binaryHid.cpp @brief Native import of indexed binary Harmony configurations. */
#include "harmony/core/formats/binaryHid.h"
#include "harmony/core/formats/container.h"
#include "harmony/core/formats/gspmFonts.h"
#include "harmony/core/keyScans.h"
#include "harmony/core/remoteFamily.h"
#include <algorithm>
#include <cctype>
#include <cstring>
#include <map>
#include <nlohmann/json.hpp>
#include <optional>
#include <set>
#include <stdexcept>

namespace harmony::formats {
namespace {
using Json = nlohmann::json;
struct Atom {
    unsigned value, opcode;
};
struct Key {
    unsigned tag;
    Atom action;
};
struct Text {
    unsigned x, y;
    std::string value;
};
struct Page {
    std::vector<Key> keys;
    std::vector<Text> texts;
};
struct Mode {
    std::vector<Key> keys;
    std::vector<Page> pages;
    std::string title;
};
struct GlyphName {
    std::uint64_t fingerprint;
    const char* character;
};
#include "hidGlyphNames.inc"

/// Legacy name tables use Latin-1; newer tables may contain UTF-8.
std::string nameText(std::string text) {
    try {
        (void)Json(text).dump();
        return text;
    } catch (const Json::type_error&) {
        std::string utf8;
        for (const unsigned char character : text) {
            if (character < 128)
                utf8.push_back(static_cast<char>(character));
            else {
                utf8.push_back(static_cast<char>(0xc0 | (character >> 6)));
                utf8.push_back(static_cast<char>(0x80 | (character & 63)));
            }
        }
        return utf8;
    }
}

/// Описание семейства по архитектуре контейнера. Нет описания — импорт не продолжается.
const RemoteFamily& familyFor(const Container& frame) {
    const auto* family = harmony::remoteFamily(frame.family->architecture);
    if (!family) {
        throw std::runtime_error("HID family has no description");
    }
    return *family;
}

/** All byte access and pointer resolution share one checked image boundary. */
class Reader {
public:
    Reader(std::span<const std::uint8_t> bytes, const Container& frame, std::string_view modelId)
        : bytes_(bytes), frame_(frame), family_(familyFor(frame)), modelId_(modelId) {}
    ImportedModel read();

private:
    std::span<const std::uint8_t> bytes_;
    const Container& frame_;
    const RemoteFamily& family_;
    std::string_view modelId_; ///< Id из заголовка EZHex; действует до завершения read().
    std::vector<std::vector<Atom>> actions_;
    std::map<unsigned, std::string> names_;
    std::optional<unsigned> activityVariable_;
    unsigned idle_ = 0;
    std::map<unsigned, std::string> alphabet_;
    std::set<unsigned> ambiguousIl_;                  ///< Коды, неотличимые между «I» и «l»
    std::map<unsigned, std::string> softLabels_;      ///< (устройство << 8 | команда) → подпись экранной кнопки
    std::vector<std::string> warnings_;                ///< предупреждения импорта; не свойства проекта

    /// Подписи экранных кнопок страницы: текст, сгруппированный по четвертям экрана (ЛВ, ПВ, ЛН, ПН).
    std::vector<std::string> softLabels(const ScreenGeometry& geometry, const std::vector<Text>& texts) const {
        // Без геометрии семейства подписи по четвертям не группируются: решение шага — пустой список.
        if (geometry.headerBottom <= 0 || geometry.footerTop <= 0 || geometry.softQuarterX <= 0 || geometry.softQuarterY <= 0)
            return {};
        const unsigned headerBottom = static_cast<unsigned>(geometry.headerBottom);
        const unsigned footerTop = static_cast<unsigned>(geometry.footerTop);
        const unsigned quarterX = static_cast<unsigned>(geometry.softQuarterX);
        const unsigned quarterY = static_cast<unsigned>(geometry.softQuarterY);
        std::string quadrant[4];
        for (const auto& item : texts) {
            if (item.y <= headerBottom || item.y >= footerTop || item.value.empty())
                continue; // заголовок со счётчиком страниц и нижняя строка подсказки
            auto& cell = quadrant[(item.y >= quarterY ? 2 : 0) + (item.x >= quarterX ? 1 : 0)];
            cell += (cell.empty() ? "" : " ") + item.value;
        }
        std::vector<std::string> out;
        for (auto& cell : quadrant)
            if (!cell.empty())
                out.push_back(std::move(cell));
        return out;
    }

    /// Decodes the monochrome glyph representation used by the 5xx family.
    GspmGlyph monoGlyph(std::size_t at) const {
        GspmGlyph glyph;
        glyph.width = static_cast<int>(number(at++, 1));
        for (unsigned row = 0; row < 128; ++row) {
            const unsigned leader = number(at++, 1);
            if (!leader) {
                glyph.height = static_cast<int>(glyph.rows.size());
                return glyph;
            }
            const auto end = at + (leader & 31);
            std::vector<std::uint8_t> pixels;
            while (at < end) {
                const unsigned command = number(at++, 1), count = (command & 15) + 1, kind = command >> 4;
                if (kind == 6 || kind == 10)
                    pixels.insert(pixels.end(), count, kind == 10 ? 1 : 0);
                else if (kind == 5) {
                    for (unsigned i = 0; i < count; ++i)
                        pixels.push_back(((number(at + i / 4, 1) >> (6 - 2 * (i % 4))) & 3) == 1 ? 1 : 0);
                    at += (count + 3) / 4;
                } else
                    throw std::runtime_error("Invalid monochrome HID glyph");
                if (pixels.size() > 256)
                    throw std::runtime_error("HID glyph too wide");
            }
            if (at != end || pixels.size() != static_cast<std::size_t>(glyph.width))
                throw std::runtime_error("HID glyph row width mismatch");
            glyph.rows.push_back(std::move(pixels));
        }
        throw std::runtime_error("HID glyph too tall");
    }
    /// Characters are recognised by shapes annotated from our fixtures, across all font faces.
    void readAlphabet() {
        const auto table = section(7);
        if (!table)
            return;
        std::map<std::uint64_t, std::set<std::string>> known;
        for (const auto& item : kGlyphNames)
            known[item.fingerprint].insert(item.character);
        std::map<unsigned, std::set<std::string>> candidates;
        for (const auto font : index(*table, 2)) {
            unsigned first = number(font + 1, 1), count = number(font + 2, 1);
            if (!count) {
                count = number(font + 1, 2);
                first = 1;
            }
            if (count > 1024)
                continue;
            need(font + 3, count * 3);
            for (unsigned slot = 0; slot < count; ++slot) {
                if (!number(font + 3 + 3 * slot, 3))
                    continue;
                const auto address = pointer(font + 3 + 3 * slot);
                std::optional<GspmGlyph> glyph;
                if (family_.monoGlyphs)
                    glyph = monoGlyph(address);
                else {
                    auto decoded = decodeGspmGlyph(bytes_, address);
                    if (decoded)
                        glyph = std::move(*decoded);
                }
                if (!glyph)
                    continue;
                const auto found = known.find(glyph->fingerprint());
                if (found == known.end())
                    continue;
                const unsigned code = first + slot;
                if (!candidates.contains(code))
                    candidates[code] = found->second;
                else {
                    std::set<std::string> common;
                    std::set_intersection(candidates[code].begin(), candidates[code].end(),
                                          found->second.begin(), found->second.end(),
                                          std::inserter(common, common.end()));
                    candidates[code] = std::move(common);
                }
            }
        }
        for (const auto& [code, letters] : candidates) {
            if (letters.size() == 1)
                alphabet_[code] = *letters.begin();
            else if (letters == std::set<std::string>{"I", "l"})
                ambiguousIl_.insert(code); // заглавная «I» и строчная «l» рисуются одинаково
        }
    }
    std::string text(std::size_t& at) const {
        std::string result;
        for (unsigned i = 0; i < 512; ++i) {
            unsigned code = number(at++, 1);
            if (!code)
                return result;
            if ((code & 128) && family_.wideTextCodes)
                code = ((code & 127) << 8) | number(at++, 1);
            const auto letter = alphabet_.find(code);
            if (letter != alphabet_.end())
                result += letter->second;
            else if (ambiguousIl_.contains(code)) {
                // «I» и «l» неразличимы по форме: после буквы — строчная «l» («Blu»), иначе заглавная «I» («Input»).
                const bool afterLetter = !result.empty() && std::isalpha(static_cast<unsigned char>(result.back()));
                result += afterLetter ? "l" : "I";
            } else
                result += "?";
        }
        throw std::runtime_error("Unterminated HID text");
    }
    /// Reads text instructions; bounded calls and jumps follow drawing programs only.
    std::vector<Text> drawing(std::size_t at, std::set<std::size_t>& visited, unsigned depth = 0) const {
        std::vector<Text> out;
        if (depth > 16 || !visited.insert(at).second)
            return out;
        for (unsigned steps = 0; steps < 512; ++steps) {
            const auto opcode = number(at++, 1);
            if (opcode == 0 || opcode == 0x17)
                break;
            if (opcode == 4 || opcode == 5) {
                const auto x = number(at, 1), y = number(at + 1, 1);
                at += 2;
                std::string label;
                if (opcode == 4) {
                    auto string = pointer(at);
                    at += 3;
                    label = text(string);
                } else
                    label = text(at);
                out.push_back({x, y, std::move(label)});
            } else if (opcode == 0x14 || (opcode == 0x16 && family_.nestedDrawingFollows0x16)) {
                const auto target = pointer(at);
                at += 3;
                auto nested = drawing(target, visited, depth + 1);
                out.insert(out.end(), nested.begin(), nested.end());
                if (opcode == 0x14)
                    break;
            } else {
                unsigned skip = 0;
                switch (opcode) {
                case 1:
                    skip = 6;
                    break;
                case 2:
                    skip = 5;
                    break;
                case 3:
                    skip = 9;
                    break;
                case 0x10:
                    skip = 1;
                    break;
                case 0x11:
                    skip = 3;
                    break;
                case 0x15:
                    skip = 4;
                    break;
                case 0x16:
                    skip = 1;
                    break;
                default:
                    return out;
                }
                need(at, skip);
                at += skip;
            }
        }
        return out;
    }
    std::vector<Text> drawing(std::size_t at) const {
        std::set<std::size_t> visited;
        return drawing(at, visited);
    }
    void need(std::size_t at, std::size_t count) const {
        if (at > frame_.checksumOffset || count > frame_.checksumOffset - at)
            throw std::runtime_error("HID structure exceeds image at " + std::to_string(at));
    }
    unsigned number(std::size_t at, unsigned width) const {
        need(at, width);
        unsigned value = 0;
        for (unsigned i = 0; i < width; ++i)
            value |= unsigned(bytes_[at + i]) << (8 * i);
        return value;
    }
    std::size_t pointer(std::size_t at) const {
        const auto value = number(at, 3);
        if (value < frame_.base)
            throw std::runtime_error("HID pointer precedes image");
        const std::size_t offset = value - frame_.base;
        need(offset, 1);
        return offset;
    }
    std::optional<std::size_t> section(unsigned base) const {
        const auto raw = family_.sectionSlot(base);
        if (!raw) {
            return {};
        }
        return *raw < frame_.sections.size() ? frame_.sections[*raw] : std::nullopt;
    }
    std::vector<std::size_t> index(std::size_t at, unsigned width) const {
        const auto count = number(at, width);
        if (count > 65535)
            throw std::runtime_error("HID index too large");
        need(at + width, 3 * count);
        std::vector<std::size_t> out;
        for (unsigned i = 0; i < count; ++i)
            out.push_back(pointer(at + width + 3 * i));
        return out;
    }
    Atom atom(std::size_t at) const { return {number(at, 2), number(at + 2, 1)}; }
    std::vector<Key> keys(std::size_t at) const {
        unsigned count = number(at, 1), width = 4, lead = 1;
        if (count == 0) {
            count = number(at + 1, 1);
            width = 5;
            lead = 2;
        }
        need(at + lead, width * count);
        std::vector<Key> out;
        for (unsigned i = 0; i < count; ++i) {
            const auto row = at + lead + width * i + (width == 5 ? 1 : 0);
            out.push_back({number(row, 1), atom(row + 1)});
        }
        return out;
    }
    void expand(Atom item, std::vector<Atom>& out, std::set<unsigned>& seen, unsigned depth) const {
        if (depth > 32 || out.size() > 8192)
            return;
        if (item.opcode != 0x7f) {
            out.push_back(item);
            return;
        }
        if (item.value >= actions_.size() || !seen.insert(item.value).second)
            return;
        bool guarded = false;
        for (const auto next : actions_[item.value]) {
            if (guarded) {
                guarded = false;
                continue;
            }
            if (next.opcode == 0x70 || next.opcode == 0x71) {
                guarded = true;
                continue;
            }
            expand(next, out, seen, depth + 1);
        }
        seen.erase(item.value);
    }
    std::vector<Atom> expanded(Atom item) const {
        std::vector<Atom> out;
        std::set<unsigned> seen;
        expand(item, out, seen, 0);
        return out;
    }
    std::vector<unsigned> commands(Atom item) const {
        std::vector<unsigned> out;
        for (const auto step : expanded(item))
            if (step.opcode == 0x7d)
                out.push_back(step.value);
        return out;
    }
    void readActions() {
        const auto table = section(10);
        if (!table)
            return;
        for (const auto at : index(*table, 2)) {
            const auto count = number(at, 1);
            need(at + 1, count * 3);
            auto& list = actions_.emplace_back();
            for (unsigned i = 0; i < count; ++i)
                list.push_back(atom(at + 1 + 3 * i));
        }
    }
    void readNames() {
        const auto table = section(0);
        if (!table)
            return;
        if (number(*table, 2) != 0xfeed)
            throw std::runtime_error("Invalid HID name table");
        const std::size_t end = *table + number(*table + 2, 3);
        if (number(end, 2) != 0xbeef)
            throw std::runtime_error("Missing HID name terminator");
        for (std::size_t at = *table + 5; at < end;) {
            const auto length = number(at + 1, 2);
            if (number(at, 1) != 0xa7 || length < 4 || at + 3 + length > end)
                throw std::runtime_error("Invalid HID name record");
            if (number(at + 3, 2) == 1) {
                const auto id = number(at + 5, 2);
                names_[id] =
                    nameText(std::string(reinterpret_cast<const char*>(bytes_.data() + at + 7), length - 4));
                if (names_[id].starts_with("CurrentActivityState_"))
                    activityVariable_ = id;
            }
            at += 3 + length;
        }
    }
    std::vector<unsigned> block(std::size_t at, unsigned encoding) const {
        std::vector<unsigned> out;
        if (encoding == 1) {
            for (unsigned i = 0; i < 8192; ++i) {
                const auto word = number(at + 2 * i, 2);
                if (!word)
                    return out;
                out.push_back(word);
            }
            throw std::runtime_error("Unterminated HID waveform");
        }
        if (encoding == 5) {
            const auto symbols = pointer(at);
            const auto count = number(at + 3, 2), available = number(symbols, 1);
            need(at + 5, count);
            need(symbols + 1, 3 * available);
            for (unsigned i = 0; i < count; ++i) {
                const auto symbol = number(at + 5 + i, 1);
                if (symbol >= available)
                    throw std::runtime_error("Invalid HID waveform symbol");
                const auto body = pointer(symbols + 1 + 3 * symbol);
                const auto words = number(body, 2);
                if (words > 8192 || out.size() + words > 65536)
                    throw std::runtime_error("HID waveform too large");
                need(body + 2, words * 2);
                for (unsigned j = 0; j < words; ++j)
                    out.push_back(number(body + 2 + 2 * j, 2));
            }
        }
        return out;
    }
    Json signal(std::size_t classAt) const {
        if (classAt < 7)
            throw std::runtime_error("Invalid HID infrared header");
        const auto start = classAt - 7;
        if (pointer(start + 8) != start)
            throw std::runtime_error("HID infrared self-pointer mismatch");
        const auto encoding = number(classAt, 1), variants = number(start + 11, 1);
        if (!variants || variants > 2)
            throw std::runtime_error("Invalid HID waveform variants");
        need(start + 12, variants * 9);
        Json all = Json::array();
        for (unsigned i = 0; i < variants * 3; ++i) {
            const auto at = start + 12 + 3 * i;
            all.push_back(number(at, 3) ? Json(block(pointer(at), encoding)) : Json::array());
        }
        std::vector<int> durations;
        for (const unsigned word : all[0]) {
            const int length = static_cast<int>(word & 0x7fff);
            if (!length)
                continue;
            const int duration = (word & 0x8000) ? length : -length;
            if (durations.empty() && duration < 0)
                continue;
            if (!durations.empty() && (durations.back() > 0) == (duration > 0))
                durations.back() += duration;
            else
                durations.push_back(duration);
        }
        const auto period = number(start + 1, 3);
        return {{"format", "HarmonyBinaryIR"},
                {"frequency", period ? 1000000000.0 / period : 0.0},
                {"durations", durations},
                {"encoding", encoding},
                {"blocks", all},
                {"offset", start}};
    }
    /// Имя по таблице модели/семейства; без подтверждения — пометка ScanNN.
    std::string hardKeyName(unsigned scan) const {
        const auto name = harmony::keyNameForScan(family_.architecture, modelId_, scan);
        return name ? std::string(*name) : harmony::scanPlaceholder(scan);
    }
    void addButtons(std::vector<model::ControlGroup>& groups, const std::vector<Key>& bindings,
                    const model::RemoteConfig& config, bool screen) const {
        model::ControlGroup group;
        group.name = screen ? "Misc" : "HardButtons";
        for (const auto& key : bindings) {
            if ((key.tag >> 6) != 2)
                continue;
            const auto codes = commands(key.action);
            if (codes.size() != 1)
                continue;
            const unsigned device = codes[0] >> 8, command = codes[0] & 255;
            if (device >= config.devices.size() || command >= config.devices[device].commands.size())
                continue;
            const auto& owner = config.devices[device];
            const auto& name = owner.commands[command].name;
            model::Button button;
            if (!screen)
                button.name = hardKeyName(key.tag & 63);
            const auto shown = softLabels_.find(codes[0]);
            button.label = shown == softLabels_.end() ? name : shown->second;
            button.position = static_cast<int>(group.buttons.size());
            button.actionId = owner.id + "_" + name + "_Press";
            group.buttons.push_back(std::move(button));
        }
        if (!group.buttons.empty())
            groups.push_back(std::move(group));
    }
    /// Имена команд в файле не хранятся; настоящие подписи есть только у экранных кнопок.
    /// Команде с подписью даём имя из неё (без пробелов, как у Logitech: «PowerToggle»).
    void nameCommands(model::RemoteConfig& result, const std::vector<Mode>& modes) {
        for (const auto& mode : modes) {
            for (std::size_t page = 0; page < mode.pages.size(); ++page) {
                std::vector<const Key*> soft;
                for (const auto& key : mode.pages[page].keys)
                    if ((key.tag >> 6) == 2)
                        soft.push_back(&key);
                const auto labels = softLabels(family_.screen, mode.pages[page].texts);
                if (soft.size() != labels.size()) {
                    // Пустые ячейки: соответствие неоднозначно. Числа различаются — значит,
                    // часть мест пуста и её не восстановить (C2). Без программы рисования
                    // подписей нет вовсе, и сравнивать не с чем: предупреждения нет.
                    if (!labels.empty())
                        warnings_.push_back("Страница " + std::to_string(page + 1) + ": подписей " +
                                            std::to_string(labels.size()) + ", кнопок " +
                                            std::to_string(soft.size()) + ", пустые места не определены");
                    continue;
                }
                for (std::size_t i = 0; i < soft.size(); ++i) {
                    const auto codes = commands(soft[i]->action);
                    if (codes.size() == 1)
                        softLabels_.emplace(codes[0], labels[i]);
                }
            }
        }
        // Команда, которую вешают на физическую клавишу, обычно носит её имя (VolumeUp, Play).
        std::map<unsigned, std::string> hardNames;
        for (const auto& mode : modes)
            for (const auto& key : mode.keys) {
                if ((key.tag >> 6) != 2)
                    continue;
                const auto codes = commands(key.action);
                const auto key_name = harmony::keyNameForScan(family_.architecture, modelId_, key.tag & 63);
                if (codes.size() == 1 && key_name)
                    hardNames.emplace(codes[0], *key_name);
            }
        for (std::size_t d = 0; d < result.devices.size(); ++d) {
            std::set<std::string> used;
            for (std::size_t c = 0; c < result.devices[d].commands.size(); ++c)
                used.insert(result.devices[d].commands[c].name);
            for (std::size_t c = 0; c < result.devices[d].commands.size(); ++c) {
                const auto id = static_cast<unsigned>(d << 8 | c);
                const auto label = softLabels_.find(id);
                const auto hard = hardNames.find(id);
                if (label == softLabels_.end() && hard == hardNames.end())
                    continue;
                std::string name;
                if (label != softLabels_.end()) {
                    for (const char letter : label->second)
                        if (letter != ' ')
                            name += letter;
                } else {
                    name = hard->second;
                }
                if (name.empty() || name.find('?') != std::string::npos)
                    continue; // подпись не распознана до конца — оставляем номер
                std::string unique = name;
                for (int n = 2; used.contains(unique); ++n)
                    unique = name + "_" + std::to_string(n);
                used.erase(result.devices[d].commands[c].name);
                used.insert(unique);
                result.devices[d].commands[c].name = std::move(unique);
            }
        }
    }
    /// Команда с собственной подписью (например, «Return»), висящая на клавише с другим именем
    /// («Exit»), остаётся под своим именем. Расхождение — предупреждение: имена различаются, и
    /// сверить их в файле не с чем. Имя клавиши даёт таблица модели/семейства без профиля.
    void warnKeyMismatch(model::RemoteConfig& result, const std::vector<Mode>& modes) {
        std::set<std::string> reported;
        for (const auto& mode : modes)
            for (const auto& key : mode.keys) {
                if ((key.tag >> 6) != 2)
                    continue;
                const auto codes = commands(key.action);
                if (codes.size() != 1 || (codes[0] >> 8) >= result.devices.size())
                    continue;
                const auto& device = result.devices[codes[0] >> 8];
                if ((codes[0] & 255) >= device.commands.size())
                    continue;
                const auto& command = device.commands[codes[0] & 255].name;
                const unsigned scan = key.tag & 63;
                const auto key_name = hardKeyName(scan);
                const bool unknown = !harmony::keyNameForScan(family_.architecture, modelId_, scan);
                const bool generic = command.starts_with("Command");
                if (!unknown && (generic || command == key_name))
                    continue;
                const auto text = unknown ? "Клавиша " + key_name + " (скан-код " + std::to_string(scan) +
                                                ", архитектура " + std::to_string(family_.architecture) +
                                                ") без имени в таблице семейства"
                                          : "Команда " + command + " назначена на клавишу " + key_name +
                                                ": имя команды не совпадает с именем клавиши в таблице семейства";
                if (reported.insert(text).second)
                    warnings_.push_back(text);
            }
    }
    void readStates(model::RemoteConfig& result) {
        const auto table = section(13);
        if (!table)
            return;
        const auto count = number(*table, 2);
        need(*table + 8, 3 * count);
        for (unsigned id = 0; id < count; ++id) {
            const auto state = pointer(*table + 8 + 3 * id);
            if (activityVariable_ && id == *activityVariable_)
                idle_ = number(state, 2);
            const auto name = names_.find(id);
            if (name == names_.end())
                continue;
            auto split = name->second.rfind("_Power_");
            if (split == std::string::npos)
                split = name->second.rfind("_Input_");
            if (split == std::string::npos)
                continue;
            const auto countTransitions = number(state + 4, 2);
            need(state + 7, countTransitions * 8);
            std::set<unsigned> groups;
            for (unsigned i = 0; i < countTransitions; ++i)
                for (const auto command : commands(atom(state + 12 + 8 * i)))
                    groups.insert(command >> 8);
            if (groups.size() == 1 && *groups.begin() < result.devices.size())
                result.devices[*groups.begin()].label = name->second.substr(0, split);
        }
    }
    std::vector<Mode> readModes() const {
        std::vector<Mode> out;
        const auto table = section(6);
        if (!table)
            return out;
        for (const auto trailer : index(*table, 3)) {
            Mode mode;
            const auto keyTable = pointer(trailer + 1);
            mode.keys = keys(keyTable);
            const unsigned keyCount = number(keyTable, 1);
            const auto header = keyTable + (keyCount ? 1 + 4 * keyCount : 2 + 5 * number(keyTable + 1, 1));
            try {
                for (const auto& label : drawing(header))
                    if (label.y <= 16 && !label.value.empty() && label.value.find('?') == std::string::npos) {
                        mode.title = label.value;
                        break;
                    }
            } catch (const std::runtime_error&) { /* An unsupported drawing does not erase the inventory. */
            }
            const auto pages = number(trailer + 4, 2);
            need(trailer + 6, pages * 3);
            for (unsigned i = 0; i < pages; ++i) {
                const auto page = pointer(trailer + 6 + 3 * i);
                const unsigned shift = family_.commandIndexShift;
                Page content;
                content.keys = keys(pointer(page + shift));
                try {
                    content.texts = drawing(pointer(page + shift + 3));
                } catch (const std::runtime_error&) { /* Retain native page when text is not understood. */
                }
                mode.pages.push_back(std::move(content));
            }
            out.push_back(std::move(mode));
        }
        return out;
    }
    void readActivities(model::RemoteConfig& result, const std::vector<Mode>& modes) const {
        const auto table = section(9);
        if (!table || !activityVariable_) {
            result.properties.emplace_back("ActivitiesUndetermined", "true");
            return;
        }
        std::set<unsigned> seen;
        for (const auto at : index(*table, 1)) {
            const auto bindings = keys(at);
            for (const auto& key : bindings) {
                if (key.tag != 1)
                    continue;
                for (const auto step : expanded(key.action)) {
                    if (step.opcode != 0x80 + *activityVariable_ || step.value == idle_ ||
                        !seen.insert(step.value).second)
                        continue;
                    model::Activity activity;
                    activity.id = "activity" + std::to_string(step.value);
                    activity.label = "Activity " + std::to_string(step.value + 1);
                    for (const auto navigation : expanded(key.action))
                        if (navigation.opcode == 0x7e && navigation.value < modes.size() &&
                            !modes[navigation.value].title.empty())
                            activity.label = modes[navigation.value].title;
                    activity.type = "Generic";
                    std::map<unsigned, unsigned> devices; // устройство → число назначенных кнопок
                    for (const auto& binding : bindings)
                        for (const auto command : commands(binding.action))
                            ++devices[command >> 8];
                    // Устройства активности включаются вместе с ней; роль DEFAULT — одна, у устройства
                    // с наибольшим числом кнопок (остальные роли в файле не подписаны).
                    const std::pair<const unsigned, unsigned>* main = nullptr;
                    for (const auto& entry : devices) {
                        if (entry.first >= result.devices.size())
                            continue;
                        activity.powerOn.push_back(result.devices[entry.first].id);
                        if (!main || entry.second > main->second)
                            main = &entry;
                    }
                    if (main)
                        activity.roles.push_back({"DEFAULT", result.devices[main->first].id});
                    addButtons(activity.controlGroups, bindings, result, false);
                    result.activities.push_back(std::move(activity));
                }
            }
        }
    }
};

ImportedModel Reader::read() {
    model::RemoteConfig result;
    result.properties = {{"SourceFormat", "BinaryHid"},
                         {"ImportNotes",
                          "Native HID import. Original image retained. Unresolved names and scan codes use "
                          "numbered identifiers. Start/stop programs, conditional actions and multi-command "
                          "bindings have not been converted."}};
    readActions();
    readNames();
    readAlphabet();
    const auto table = section(5);
    if (!table)
        throw std::runtime_error("Missing HID device table");
    const auto devices = index(*table, 1);
    for (std::size_t i = 0; i < devices.size(); ++i) {
        model::Device device;
        device.id = "hid" + std::to_string(i);
        device.label = "Device " + std::to_string(i + 1);
        device.type = "Unknown";
        const auto list = index(devices[i] + 1, 2);
        for (std::size_t j = 0; j < list.size(); ++j) {
            model::Command command;
            command.name = "Command" + std::to_string(j + 1);
            command.protocolName = "HID";
            command.native = signal(list[j]).dump();
            device.commands.push_back(std::move(command));
        }
        result.devices.push_back(std::move(device));
    }
    readStates(result);
    const auto modes = readModes();
    nameCommands(result, modes);
    warnKeyMismatch(result, modes);
    for (std::size_t device = 0; device < result.devices.size(); ++device) {
        std::size_t best = 0;
        const Mode* owned = nullptr;
        for (const auto& mode : modes) {
            std::size_t score = 0;
            bool other = false;
            const auto check = [&](const auto& bindings) {
                for (const auto& key : bindings)
                    for (const auto command : commands(key.action)) {
                        if ((command >> 8) != device)
                            other = true;
                        else
                            ++score;
                    }
            };
            check(mode.keys);
            for (const auto& page : mode.pages)
                check(page.keys);
            if (!other && score > best) {
                best = score;
                owned = &mode;
            }
        }
        if (owned) {
            if (result.devices[device].label.starts_with("Device ") && !owned->title.empty())
                result.devices[device].label = owned->title;
            addButtons(result.devices[device].controlGroups, owned->keys, result, false);
            for (const auto& page : owned->pages)
                addButtons(result.devices[device].controlGroups, page.keys, result, true);
        }
    }
    readActivities(result, modes);
    return ImportedModel{std::move(result), harmony::ImportReport{warnings_}};
}
} // namespace
Result<ImportedModel> importBinaryHid(std::span<const std::uint8_t> payload, std::string_view modelId) {
    if (payload.size() > 10 && std::memcmp(payload.data(), "TPTP", 4) == 0 && payload[9] == 23)
        while (!payload.empty() && payload.back() == 0)
            payload = payload.first(payload.size() - 1);
    auto frame = parseContainer(payload);
    if (!frame)
        return std::unexpected(frame.error());
    try {
        return Reader(payload, *frame, modelId).read();
    } catch (const std::exception& error) {
        return makeError(ErrorCode::Format, "HID import: %1", {error.what()});
    }
}
} // namespace harmony::formats
