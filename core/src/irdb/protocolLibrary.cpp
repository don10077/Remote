/**
 * @file protocolLibrary.cpp
 * @brief Протоколы базы Logitech (AllProtocols.json): код команды → ИК-сигнал.
 *
 * Устройство протокола (только нужное здесь):
 * - IRSegments — сегменты данных: Header (атомы), Payload (кодирование значения),
 *   Trailer (атомы), TotalLength — длина кадра в мкс (0 — как получится);
 * - CodeSegments — постоянные сегменты (повтор, начало, конец);
 * - KeyCode.Start / Repeat / Finish — из каких сегментов состоит нажатие.
 * Атом — импульс (Type 1) или пауза (Type 0) длительностью Value мкс.
 * Кодирование Payload: EncodingType 0/1 — бит на символ (1 — бифазное),
 * 2/3 — шестнадцатеричная цифра на символ.
 *
 * Код команды: «G:<имя протокола>:(<начало>)(<повтор>)(<конец>):<n>»; в каждой
 * части значения сегментов через «_»: число («0x2555», «1x0») — сегмент данных,
 * имя («Repeat») — постоянный сегмент.
 *
 * Разбор кода и сборка сигнала повторяют проект harmony900tools (Martin Wagner).
 */

#include "harmony/core/irdb/irDatabase.h"

#include "harmony/core/formats/irTables.h"
#include "harmony/core/ir/carrierVm.h"

#include <algorithm>
#include <array>
#include <cstdlib>
#include <cctype>
#include <map>
#include <nlohmann/json.hpp>
#include <unordered_map>

namespace harmony::irdb {

using nlohmann::json;

namespace {

constexpr int kMaxRepeats = 5; ///< Сколько раз передать кадр повтора, если протокол просит больше
constexpr int kTimingMax = 0x7FFF; ///< Длительность одного слова пульта, мкс

/// Член объекта или null.
const json& member(const json& object, const char* key) {
    static const json none;
    if (!object.is_object()) {
        return none;
    }
    const auto it = object.find(key);
    return it != object.end() ? *it : none;
}

/// Число или запасное значение (в базе много null).
std::int64_t number(const json& object, const char* key, std::int64_t fallback) {
    const json& value = member(object, key);
    return value.is_number() ? value.get<std::int64_t>() : fallback;
}

/// Строка или пусто.
std::string text(const json& object, const char* key) {
    const json& value = member(object, key);
    return value.is_string() ? value.get<std::string>() : std::string();
}

/// Длительность: импульс или пауза.
struct Pulse {
    bool mark;
    std::uint32_t us;
};

/// Добавляет атомы; соседние одного вида складываются, ведущая пауза отбрасывается.
bool appendAtoms(std::vector<Pulse>& out, const json& atoms, std::string& error) {
    if (!atoms.is_array()) {
        return true;
    }
    for (const json& atom : atoms) {
        const json* value = &member(atom, "Value");
        if (value->is_null()) {
            value = &member(atom, "MinValue");
        }
        if (!value->is_number()) {
            error = "timing without value";
            return false;
        }
        const auto us = value->get<std::int64_t>();
        if (us <= 0) {
            continue;
        }
        const bool mark = number(atom, "Type", 0) == 1;
        if (out.empty() && !mark) {
            continue;
        }
        if (!out.empty() && out.back().mark == mark) {
            out.back().us += static_cast<std::uint32_t>(us);
        } else {
            out.push_back({mark, static_cast<std::uint32_t>(us)});
        }
    }
    return true;
}

std::uint64_t lengthOf(const std::vector<Pulse>& out) {
    std::uint64_t sum = 0;
    for (const Pulse& pulse : out) {
        sum += pulse.us;
    }
    return sum;
}

int hexDigit(char c) {
    if (c >= '0' && c <= '9') {
        return c - '0';
    }
    if (c >= 'a' && c <= 'f') {
        return c - 'a' + 10;
    }
    if (c >= 'A' && c <= 'F') {
        return c - 'A' + 10;
    }
    return -1;
}

/**
 * @brief Значение сегмента данных → символы по порядку передачи (старшие первыми).
 *
 * EncodingType 0/1 — по символу на бит, 2/3 — на шестнадцатеричную цифру. Символов
 * ровно NumberOfBits: лишние старшие отбрасываются, недостающие — нули.
 */
bool symbolsOf(const json& payload, const std::string& digits, std::vector<int>& symbols,
               std::string& error) {
    const auto encoding = number(payload, "EncodingType", -1);
    const auto count = number(payload, "NumberOfBits", 0);
    if (encoding < 0 || encoding > 3 || count <= 0) {
        error = "unknown encoding " + std::to_string(encoding);
        return false;
    }
    symbols.clear();
    for (const char c : digits) {
        const int digit = hexDigit(c);
        if (digit < 0) {
            error = "invalid value";
            return false;
        }
        if (encoding <= 1) {
            for (int bit = 3; bit >= 0; --bit) {
                symbols.push_back((digit >> bit) & 1);
            }
        } else {
            symbols.push_back(digit);
        }
    }
    if (static_cast<std::int64_t>(symbols.size()) > count) {
        symbols.erase(symbols.begin(), symbols.end() - count);
    }
    while (static_cast<std::int64_t>(symbols.size()) < count) {
        symbols.insert(symbols.begin(), 0);
    }
    return true;
}

/// Сегмент данных: заголовок, значение, хвост и пауза до длины кадра.
bool appendDataSegment(std::vector<Pulse>& out, const json& segment, const std::string& digits,
                       std::string& error) {
    const std::uint64_t start = lengthOf(out);
    if (!appendAtoms(out, member(segment, "Header"), error)) {
        return false;
    }
    const json& payload = member(segment, "Payload");
    if (payload.is_object()) {
        std::map<int, const json*> symbolAtoms;
        for (const json& encodingItem : member(payload, "Encodings")) {
            symbolAtoms[static_cast<int>(number(encodingItem, "BitType", -1))] =
                &member(encodingItem, "Atoms");
        }
        std::vector<int> symbols;
        if (!symbolsOf(payload, digits, symbols, error)) {
            return false;
        }
        for (const int symbol : symbols) {
            const auto it = symbolAtoms.find(symbol);
            if (it == symbolAtoms.end()) {
                error = "value does not fit the protocol";
                return false;
            }
            if (!appendAtoms(out, *it->second, error)) {
                return false;
            }
        }
    }
    if (!appendAtoms(out, member(segment, "Trailer"), error)) {
        return false;
    }
    // Длина кадра: добиваем паузой.
    const std::int64_t total = number(segment, "TotalLength", 0);
    const std::uint64_t used = lengthOf(out) - start;
    if (total > 0 && static_cast<std::uint64_t>(total) > used && !out.empty()) {
        const auto gap = static_cast<std::uint32_t>(static_cast<std::uint64_t>(total) - used);
        if (out.back().mark) {
            out.push_back({false, gap});
        } else {
            out.back().us += gap;
        }
    }
    return true;
}

/// «0x2555» / «1x0» → шестнадцатеричные цифры; пусто — не значение.
std::string valueDigits(const std::string& token) {
    const auto x = token.find('x');
    if (x == std::string::npos || x == 0 || x + 1 >= token.size()) {
        return {};
    }
    for (std::size_t i = 0; i < x; ++i) {
        if (hexDigit(token[i]) < 0) {
            return {};
        }
    }
    return token.substr(x + 1);
}

std::vector<std::string> splitTokens(const std::string& group) {
    std::vector<std::string> tokens;
    if (group.empty()) {
        return tokens;
    }
    std::size_t begin = 0;
    for (;;) {
        const std::size_t end = group.find('_', begin);
        tokens.push_back(group.substr(begin, end - begin));
        if (end == std::string::npos) {
            break;
        }
        begin = end + 1;
    }
    return tokens;
}

const json* findSegment(const json& list, const std::string& name) {
    if (!list.is_array()) {
        return nullptr;
    }
    for (const json& segment : list) {
        if (text(segment, "Name") == name) {
            return &segment;
        }
    }
    return nullptr;
}

/// Разбор «G:<имя>:(<начало>)(<повтор>)(<конец>):<n>»; вручную — std::regex слишком медленный.
bool parseKeyCode(std::string code, std::string& name, std::string (&groups)[3]) {
    while (!code.empty() && std::isspace(static_cast<unsigned char>(code.back())) != 0) {
        code.pop_back();
    }
    std::size_t begin = 0;
    while (begin < code.size() && std::isspace(static_cast<unsigned char>(code[begin])) != 0) {
        ++begin;
    }
    const std::size_t colon = code.rfind(':');
    bool ok = code.compare(begin, 2, "G:") == 0 && colon != std::string::npos &&
              colon > begin + 2 && colon + 1 < code.size();
    for (std::size_t i = colon + 1; ok && i < code.size(); ++i) {
        ok = std::isdigit(static_cast<unsigned char>(code[i])) != 0;
    }
    std::size_t end = colon;
    for (int g = 2; ok && g >= 0; --g) {
        if (end == 0 || code[end - 1] != ')') {
            return false;
        }
        const std::size_t open = code.rfind('(', end - 1);
        if (open == std::string::npos || open < begin + 2) {
            return false;
        }
        groups[g] = code.substr(open + 1, end - 1 - open - 1);
        end = open;
    }
    if (!ok || end <= begin + 2 || code[end - 1] != ':') {
        return false;
    }
    name = code.substr(begin + 2, end - 1 - (begin + 2));
    return true;
}

/// Ошибка «код не преобразовать»: причина — шаблоном (переводится в интерфейсе).
std::unexpected<Error> unsupported(std::string pattern, std::vector<std::string> args = {}) {
    return wrapError(ErrorCode::Unsupported,
                     Error(ErrorCode::Unsupported, std::move(pattern), std::move(args)),
                     "the command code cannot be converted");
}

/// Сегмент в части кода.
struct PlanItem {
    const json* segment = nullptr;
    bool data = false;  ///< Сегмент данных (IRSegments), иначе постоянный (CodeSegments)
    std::string digits; ///< Значение сегмента данных, шестнадцатеричные цифры
};

/// Разобранный код команды: протокол и сегменты частей Start, Repeat, Finish.
struct Plan {
    const json* protocol = nullptr;
    std::array<std::vector<PlanItem>, 3> parts;
    int repeats = 1; ///< Сколько раз передать часть Repeat при коротком нажатии
};

/// Длительности атомов по порядку, без сложения: импульс положительный.
bool atomDurations(const json& atoms, std::vector<int>& out, std::string& error) {
    if (!atoms.is_array()) {
        return true;
    }
    for (const json& atom : atoms) {
        const json* value = &member(atom, "Value");
        if (value->is_null()) {
            value = &member(atom, "MinValue");
        }
        if (!value->is_number()) {
            error = "timing without value";
            return false;
        }
        const auto us = value->get<std::int64_t>();
        if (us <= 0) {
            continue;
        }
        if (us > 10'000'000) {
            error = "timing is too long";
            return false;
        }
        out.push_back(number(atom, "Type", 0) == 1 ? static_cast<int>(us) : -static_cast<int>(us));
    }
    return true;
}

/// Сколько бит на символ берёт пульт для алфавита из @p size символов.
unsigned bitsFor(std::size_t size) {
    unsigned bits = 0;
    while ((std::size_t{1} << bits) < size) {
        ++bits;
    }
    return bits;
}

/// Для сравнения: без пауз по краям, длительности — не длиннее слова пульта.
std::vector<int> comparable(const std::vector<int>& durations) {
    std::vector<int> out;
    for (const int d : durations) {
        if (d == 0) {
            continue;
        }
        if (!out.empty() && (out.back() > 0) == (d > 0)) {
            out.back() += d;
        } else {
            out.push_back(d);
        }
    }
    while (!out.empty() && out.front() < 0) {
        out.erase(out.begin());
    }
    while (!out.empty() && out.back() < 0) {
        out.pop_back();
    }
    for (int& d : out) {
        d = std::clamp(d, -kTimingMax, kTimingMax);
    }
    return out;
}

bool sameDurations(const std::vector<int>& a, const std::vector<int>& b) {
    if (a.size() != b.size()) {
        return false;
    }
    for (std::size_t i = 0; i < a.size(); ++i) {
        if ((a[i] > 0) != (b[i] > 0) || std::abs(a[i] - b[i]) > std::max(2, std::abs(b[i]) / 100)) {
            return false;
        }
    }
    return true;
}

} // namespace

struct ProtocolLibrary::Impl {
    std::unordered_map<int, json> protocols;
    std::unordered_map<std::string, int> idByName;

    [[nodiscard]] Result<Plan> resolve(const DbCommand& command) const;
};

Result<Plan> ProtocolLibrary::Impl::resolve(const DbCommand& command) const {
    std::string name;
    std::string groups[3];
    if (!parseKeyCode(command.keyCode, name, groups)) {
        return unsupported("unknown code format");
    }
    auto it = protocols.find(command.protocolId);
    if (it == protocols.end()) {
        if (const auto byName = idByName.find(name); byName != idByName.end()) {
            it = protocols.find(byName->second);
        }
    }
    if (it == protocols.end()) {
        return unsupported("the protocol is not in the database");
    }
    const json& protocol = it->second;
    Plan plan;
    plan.protocol = &protocol;
    plan.repeats = static_cast<int>(
        std::clamp<std::int64_t>(number(protocol, "PressMinimumRepeats", 1), 1, kMaxRepeats));
    const json& irSegments = member(protocol, "IRSegments");
    const json& codeSegments = member(protocol, "CodeSegments");
    const std::string protocolName = irdb::text(protocol, "Name");

    const char* parts[] = {"Start", "Repeat", "Finish"};
    for (int part = 0; part < 3; ++part) {
        const std::vector<std::string> tokens = splitTokens(groups[part]);
        // Сегменты части: (имя, сегмент данных?).
        std::vector<std::pair<std::string, bool>> segments;
        const json& list = member(member(protocol, "KeyCode"), parts[part]);
        if (list.is_array()) {
            for (const json& segment : list) {
                segments.emplace_back(irdb::text(segment, "SegmentName"),
                                      number(segment, "SegmentType", 0) == 1);
            }
        }
        if (segments.size() != tokens.size()) {
            // Свободная запись («(0x8B7443BC_Repeat_Repeat)»): число — первый сегмент данных,
            // имя — постоянный сегмент «<протокол> KeyCode<имя>».
            std::string dataSegment;
            for (const auto& [segmentName, isData] : segments) {
                if (isData) {
                    dataSegment = segmentName;
                    break;
                }
            }
            if (dataSegment.empty() && irSegments.is_array() && !irSegments.empty()) {
                dataSegment = irdb::text(irSegments[0], "Name");
            }
            segments.clear();
            for (const std::string& token : tokens) {
                if (!valueDigits(token).empty()) {
                    segments.emplace_back(dataSegment, true);
                } else {
                    segments.emplace_back(protocolName + " KeyCode" + token, false);
                }
            }
        }
        for (std::size_t i = 0; i < tokens.size(); ++i) {
            PlanItem item;
            item.data = segments[i].second;
            item.segment = findSegment(item.data ? irSegments : codeSegments, segments[i].first);
            if (item.data) {
                item.digits = valueDigits(tokens[i]);
            }
            if (item.segment == nullptr || (item.data && item.digits.empty())) {
                return unsupported("segment “%1” not found", {segments[i].first});
            }
            plan.parts[part].push_back(std::move(item));
        }
    }
    return plan;
}

ProtocolLibrary::ProtocolLibrary() : impl_(std::make_unique<Impl>()) {}
ProtocolLibrary::ProtocolLibrary(ProtocolLibrary&&) noexcept = default;
ProtocolLibrary& ProtocolLibrary::operator=(ProtocolLibrary&&) noexcept = default;
ProtocolLibrary::~ProtocolLibrary() = default;

Result<ProtocolLibrary> ProtocolLibrary::parse(std::string_view text) {
    json document = json::parse(text.begin(), text.end(), nullptr, false);
    if (document.is_discarded() || !document.contains("Protocols") ||
        !document["Protocols"].is_array()) {
        return makeError(ErrorCode::Format,
                         "this is not a database protocol file (AllProtocols.json)");
    }
    ProtocolLibrary library;
    for (json& protocol : document["Protocols"]) {
        const auto id = static_cast<int>(number(protocol, "Id-", -1));
        if (id < 0) {
            continue;
        }
        library.impl_->idByName[irdb::text(protocol, "Name")] = id;
        library.impl_->protocols.emplace(id, std::move(protocol));
    }
    if (library.impl_->protocols.empty()) {
        return makeError(ErrorCode::Format, "the database protocol file contains no protocols");
    }
    return library;
}

std::size_t ProtocolLibrary::size() const {
    return impl_->protocols.size();
}

Result<ir::IrSignal> ProtocolLibrary::render(const DbCommand& command) const {
    auto plan = impl_->resolve(command);
    if (!plan) {
        return std::unexpected(std::move(plan.error()));
    }
    const json& protocol = *plan->protocol;

    ir::IrSignal signal;
    signal.frequency = static_cast<std::uint32_t>(
        std::max<std::int64_t>(0, number(protocol, "CarrierFrequency", 0)));
    if (signal.frequency < kCarrierMin || signal.frequency > kCarrierMax) {
        return unsupported("the remote cannot send a %1 kHz carrier",
                           {std::to_string(signal.frequency / 1000)});
    }
    std::vector<Pulse> out;
    std::string error;
    for (int part = 0; part < 3; ++part) {
        const int times = part == 1 ? plan->repeats : 1;
        for (int n = 0; n < times; ++n) {
            for (const PlanItem& item : plan->parts[part]) {
                const bool ok = item.data ? appendDataSegment(out, *item.segment, item.digits, error)
                                          : appendAtoms(out, member(*item.segment, "Atoms"), error);
                if (!ok) {
                    return unsupported(error);
                }
            }
        }
    }
    if (out.empty()) {
        return unsupported("empty code");
    }
    if (out.back().mark) {
        out.push_back({false, 20000});
    }
    // Длинные паузы между кадрами укорачиваются до предела слова пульта: приёмникам это не мешает.
    for (const Pulse& pulse : out) {
        const int us = static_cast<int>(std::min<std::uint32_t>(pulse.us, kTimingMax));
        signal.durations.push_back(pulse.mark ? us : -us);
    }
    return signal;
}

Result<ir::NativeCommand> ProtocolLibrary::compile(const DbCommand& command) const {
    auto plan = impl_->resolve(command);
    if (!plan) {
        return std::unexpected(std::move(plan.error()));
    }
    // Сигнал, который должен получиться: им проверяется программа.
    auto expected = render(command);
    if (!expected) {
        return std::unexpected(std::move(expected.error()));
    }
    const json& protocol = *plan->protocol;
    std::string error;

    // Элементы: сегменты данных, затем постоянные — все, чтобы запись была одна на протокол.
    std::vector<ir::ProgramElement> elements;
    std::unordered_map<const json*, std::size_t> elementOf;
    const json& irSegments = member(protocol, "IRSegments");
    if (irSegments.is_array()) {
        for (const json& segment : irSegments) {
            ir::ProgramElement element;
            if (!atomDurations(member(segment, "Header"), element.before, error) ||
                !atomDurations(member(segment, "Trailer"), element.after, error)) {
                return unsupported(error);
            }
            const std::int64_t total = number(segment, "TotalLength", 0);
            if (total > 0) {
                element.periodUs = static_cast<std::uint32_t>(std::min<std::int64_t>(total, 0xFFFFFFFE));
            }
            const json& payload = member(segment, "Payload");
            if (payload.is_object()) {
                const auto encoding = number(payload, "EncodingType", -1);
                const auto count = number(payload, "NumberOfBits", 0);
                if (encoding < 0 || encoding > 3 || count <= 0 || count > 0xFFFF) {
                    return unsupported("unknown encoding %1", {std::to_string(encoding)});
                }
                std::map<std::int64_t, std::vector<int>> symbols;
                for (const json& item : member(payload, "Encodings")) {
                    std::vector<int> durations;
                    if (!atomDurations(member(item, "Atoms"), durations, error)) {
                        return unsupported(error);
                    }
                    symbols[number(item, "BitType", -1)] = std::move(durations);
                }
                // Номер символа в <Code> — его BitType: нужны все с 0 подряд.
                const bool contiguous = !symbols.empty() && symbols.begin()->first == 0 &&
                                        symbols.rbegin()->first == static_cast<std::int64_t>(symbols.size()) - 1;
                if (!contiguous || symbols.size() > 16 || (encoding <= 1 && symbols.size() != 2)) {
                    return unsupported("the protocol symbols cannot be stored on the remote");
                }
                for (auto& symbol : symbols) {
                    element.alphabet.push_back(std::move(symbol.second));
                }
                element.symbols = static_cast<unsigned>(count);
                const json& toggle = member(payload, "ToggleBit");
                if (toggle.is_number_integer()) {
                    const auto position = toggle.get<std::int64_t>();
                    if (encoding > 1 || position < 0 || position >= count || position > 0xFE) {
                        return unsupported("the protocol toggle bit cannot be stored on the remote");
                    }
                    element.toggle = static_cast<std::uint8_t>(position);
                }
            }
            if (element.before.empty() && element.after.empty() && element.symbols == 0) {
                continue; // нечего передать; если код на него сошлётся — ниже отказ
            }
            elementOf[&segment] = elements.size();
            elements.push_back(std::move(element));
        }
    }
    const json& codeSegments = member(protocol, "CodeSegments");
    if (codeSegments.is_array()) {
        for (const json& segment : codeSegments) {
            ir::ProgramElement element;
            if (!atomDurations(member(segment, "Atoms"), element.before, error)) {
                return unsupported(error);
            }
            if (element.before.empty()) {
                continue;
            }
            elementOf[&segment] = elements.size();
            elements.push_back(std::move(element));
        }
    }
    const auto carrier = static_cast<std::uint32_t>(number(protocol, "CarrierFrequency", 0));
    auto program = ir::buildProgram(carrier, elements);
    if (!program) {
        return wrapError(ErrorCode::Unsupported, program.error(), "the command code cannot be converted");
    }

    // Код: пауза перед нажатием 500 мс (как у Logitech), минимум повторов, этапы 2, 3 и 5.
    std::vector<std::uint8_t> code = {0x00, 0x00, 0xF4, 0x01, static_cast<std::uint8_t>(plan->repeats)};
    unsigned toggleState = 0;
    bool toggleKnown = false;
    for (int part = 0; part < 3; ++part) {
        const auto& items = plan->parts[part];
        if (part == 2 && !items.empty()) {
            code.push_back(0x00); // этап 4 пуст
        }
        if (items.empty()) {
            if (part < 2) {
                code.push_back(0x00);
            }
            continue;
        }
        if (items.size() > 0xFF) {
            return unsupported("the code has too many segments");
        }
        code.push_back(static_cast<std::uint8_t>(items.size()));
        for (const PlanItem& item : items) {
            const auto found = elementOf.find(item.segment);
            if (found == elementOf.end()) {
                return unsupported("segment “%1” has nothing to transmit",
                                   {irdb::text(*item.segment, "Name")});
            }
            const ir::ProgramElement& element = elements[found->second];
            code.push_back(static_cast<std::uint8_t>(found->second));
            if (!item.data || element.symbols == 0) {
                continue;
            }
            std::vector<int> symbols;
            if (!symbolsOf(member(*item.segment, "Payload"), item.digits, symbols, error)) {
                return unsupported(error);
            }
            const unsigned width = bitsFor(element.alphabet.size());
            std::vector<bool> bits;
            for (const int symbol : symbols) {
                if (symbol < 0 || static_cast<std::size_t>(symbol) >= element.alphabet.size()) {
                    return unsupported("value does not fit the protocol");
                }
                for (unsigned b = width; b > 0; --b) {
                    bits.push_back(((symbol >> (b - 1)) & 1) != 0);
                }
            }
            if (element.toggle != ir::ProgramElement::kNoToggle && !toggleKnown) {
                toggleState = bits[element.toggle] ? 1U : 0U;
                toggleKnown = true;
            }
            // Биты старшим вперёд; без бит на символ пульт всё равно читает один байт.
            std::vector<std::uint8_t> bytes(std::max<std::size_t>(1, (bits.size() + 7) / 8), 0);
            for (std::size_t i = 0; i < bits.size(); ++i) {
                if (bits[i]) {
                    bytes[i / 8] = static_cast<std::uint8_t>(bytes[i / 8] | (0x80U >> (i % 8)));
                }
            }
            code.insert(code.end(), bytes.begin(), bytes.end());
        }
    }
    code.push_back(0x00);
    ir::NativeCommand result{std::move(*program), std::move(code)};

    // Запись должна пережить запись в файл и чтение обратно (указатели перемещаются).
    formats::IrTable table;
    table.entries.push_back(result.protocol);
    auto reread = formats::parseIrProto(formats::writeIrProto(table));
    if (!reread || reread->entries.size() != 1 || reread->entries.front() != result.protocol) {
        return makeError(ErrorCode::Format, "the generated protocol does not survive IrProto.bin");
    }
    // Исполняем, как пульт: нажатие должно дать тот же сигнал, что и render().
    auto pressed = ir::runCarrier(result.protocol, result.code, toggleState);
    if (!pressed) {
        return std::unexpected(std::move(pressed.error()));
    }
    const auto drift = std::abs(static_cast<long>(pressed->signal.frequency) - static_cast<long>(expected->frequency));
    if (drift > 400 || !sameDurations(comparable(pressed->signal.durations), comparable(expected->durations))) {
        return makeError(ErrorCode::Format, "the generated protocol command does not match the database code");
    }
    // Удержание добавляет этап повтора, если он есть у кода.
    if (!plan->parts[1].empty()) {
        const auto repeats = static_cast<unsigned>(std::ranges::count(pressed->stages, 3));
        auto held = ir::runCarrier(result.protocol, result.code, toggleState, std::max(1U, repeats));
        if (!held || held->stages.size() != pressed->stages.size() + 1) {
            return makeError(ErrorCode::Format, "the generated protocol command does not repeat while held");
        }
    }
    return result;
}

} // namespace harmony::irdb
