/**
 * @file keyVocabulary.cpp
 * @brief Данные словаря имён клавиш: таблица раздела 2 и алиасы раздела 2.1
 * [../key-names.md]. Источник каждого имени — правило 8 того же документа.
 *
 * Словарь — данные, а не вывод: здесь нет ни скан-кодов (они у семейств, `keyScans`, K2),
 * ни профиля модели. Порядок записей — порядок групп словаря, он же порядок групп в
 * интерфейсе; список зон какой-либо модели его не задаёт.
 *
 * Источник имени — сильнейший из четырёх подтверждённых (правило 8): измерение, фактическая
 * конфигурация, словарь Logitech по модели, надпись на корпусе. Если у имени подтверждений
 * нет, источник `Unknown` — имя придумано (правило 9), и проверка (K5/C1) пишет про него
 * предупреждение. Имя, подтверждённое только печатным алиасом, получает тот же источник, что
 * алиас: словарная форма — наша нормализация, отдельного подтверждения у неё нет.
 */

#include "harmony/core/keyVocabulary.h"

#include <algorithm>
#include <array>
#include <cctype>

namespace harmony {

namespace {

/// Словарь: группы в порядке интерфейса. Имена внутри группы — в порядке [../key-names.md].
constexpr std::array<KeyNameInfo, 87> kDictionary = {{
    // Служебные. Имена по надписям на корпусе (keys.md: «name from: printed»).
    // `Mouse` и `Numbers` — исключение: подтверждённого источника нет ни в keys.md, ни в
    // afterglow `hard_keys`, ни в конфигурации 900. Имя взято из таблицы
    // `afterglow/backends/harmony_800/keys.py`, где оно названо по базе Logitech, но отдельного
    // подтверждения нет, поэтому источник `Unknown` (правило 9, вопрос владельцу).
    {"Off", KeyCategory::Service, static_cast<unsigned>(PhysicalKind::System), NameSource::Printed},
    {"Help", KeyCategory::Service, static_cast<unsigned>(PhysicalKind::System), NameSource::Printed},
    {"Power", KeyCategory::Service, static_cast<unsigned>(PhysicalKind::System), NameSource::Printed},
    {"Glow", KeyCategory::Service, static_cast<unsigned>(PhysicalKind::System), NameSource::Printed},
    {"Mouse", KeyCategory::Service, static_cast<unsigned>(PhysicalKind::System), NameSource::Unknown},
    {"Numbers", KeyCategory::Service, static_cast<unsigned>(PhysicalKind::Key), NameSource::Unknown},

    // Активности. Подтверждение — только надпись на корпусе (правило 8).
    {"Activities", KeyCategory::Activity, static_cast<unsigned>(PhysicalKind::ScreenActs), NameSource::Printed},
    {"WatchMovie", KeyCategory::Activity, static_cast<unsigned>(PhysicalKind::ScreenActs), NameSource::Printed},
    {"WatchTV", KeyCategory::Activity, static_cast<unsigned>(PhysicalKind::ScreenActs), NameSource::Printed},
    {"ListenMusic", KeyCategory::Activity, static_cast<unsigned>(PhysicalKind::ScreenActs), NameSource::Printed},
    {"MoreActivities", KeyCategory::Activity, static_cast<unsigned>(PhysicalKind::ScreenActs), NameSource::Printed},

    // Устройство. Имена по надписям; `TV` и `DVR` — по карте кнопок Logitech (afterglow `hard_keys`),
    // `DVR` ещё и по измерению 350 («named by what it sends»).
    {"TV", KeyCategory::Device, static_cast<unsigned>(PhysicalKind::ScreenActs), NameSource::LogitechMap},
    {"DVR", KeyCategory::Device, static_cast<unsigned>(PhysicalKind::ScreenActs), NameSource::Measured},
    {"CablePc", KeyCategory::Device, static_cast<unsigned>(PhysicalKind::ScreenActs), NameSource::Printed},
    {"DvdMusic", KeyCategory::Device, static_cast<unsigned>(PhysicalKind::ScreenActs), NameSource::Printed},
    {"TvMedia", KeyCategory::Device, static_cast<unsigned>(PhysicalKind::ScreenActs), NameSource::Printed},
    {"AvrGame", KeyCategory::Device, static_cast<unsigned>(PhysicalKind::ScreenActs), NameSource::Printed},
    {"CableSat", KeyCategory::Device, static_cast<unsigned>(PhysicalKind::ScreenActs), NameSource::Printed},
    {"VcrAux", KeyCategory::Device, static_cast<unsigned>(PhysicalKind::ScreenActs), NameSource::Printed},
    {"Dvd", KeyCategory::Device, static_cast<unsigned>(PhysicalKind::ScreenActs), NameSource::Printed},

    // Избранное. Имена по надписям на корпусе (300, 350).
    {"Favorite1", KeyCategory::Favorite, static_cast<unsigned>(PhysicalKind::Key), NameSource::Printed},
    {"Favorite2", KeyCategory::Favorite, static_cast<unsigned>(PhysicalKind::Key), NameSource::Printed},
    {"Favorite3", KeyCategory::Favorite, static_cast<unsigned>(PhysicalKind::Key), NameSource::Printed},
    {"Favorite4", KeyCategory::Favorite, static_cast<unsigned>(PhysicalKind::Key), NameSource::Printed},
    {"Favorite5", KeyCategory::Favorite, static_cast<unsigned>(PhysicalKind::Key), NameSource::Printed},

    // Навигация. `Menu`, `Exit`, `Info`, `Guide`, `Input` — по тому, что клавиши отправляют;
    // `Back`, `Home`, `PIP` — по карте кнопок Logitech; `List`, `Teletext` — только по надписи.
    {"Menu", KeyCategory::Navigation, static_cast<unsigned>(PhysicalKind::Key), NameSource::Measured},
    {"Exit", KeyCategory::Navigation, static_cast<unsigned>(PhysicalKind::Key), NameSource::Measured},
    {"Info", KeyCategory::Navigation, static_cast<unsigned>(PhysicalKind::Key), NameSource::Measured},
    {"Guide", KeyCategory::Navigation, static_cast<unsigned>(PhysicalKind::Key), NameSource::Measured},
    {"Input", KeyCategory::Navigation, static_cast<unsigned>(PhysicalKind::Key), NameSource::Measured},
    {"List", KeyCategory::Navigation, static_cast<unsigned>(PhysicalKind::Key), NameSource::Printed},
    {"Teletext", KeyCategory::Navigation, static_cast<unsigned>(PhysicalKind::Key), NameSource::Printed},
    {"Back", KeyCategory::Navigation, static_cast<unsigned>(PhysicalKind::Key), NameSource::LogitechMap},
    {"Home", KeyCategory::Navigation, static_cast<unsigned>(PhysicalKind::Key), NameSource::LogitechMap},
    {"PIP", KeyCategory::Navigation, static_cast<unsigned>(PhysicalKind::Key), NameSource::LogitechMap},
    {"UpArrow", KeyCategory::Navigation, static_cast<unsigned>(PhysicalKind::Key), NameSource::Config},
    {"DownArrow", KeyCategory::Navigation, static_cast<unsigned>(PhysicalKind::Key), NameSource::Config},

    // D-pad, громкость, каналы, транспорт, цифры и цвета — имена фактической конфигурации 900
    // (группа `HardButtons` в `app/resources/demo/UserConfiguration.xml`).
    {"DirectionUp", KeyCategory::Dpad, static_cast<unsigned>(PhysicalKind::Key), NameSource::Config},
    {"DirectionDown", KeyCategory::Dpad, static_cast<unsigned>(PhysicalKind::Key), NameSource::Config},
    {"DirectionLeft", KeyCategory::Dpad, static_cast<unsigned>(PhysicalKind::Key), NameSource::Config},
    {"DirectionRight", KeyCategory::Dpad, static_cast<unsigned>(PhysicalKind::Key), NameSource::Config},
    {"Select", KeyCategory::Dpad, static_cast<unsigned>(PhysicalKind::Key), NameSource::Config},

    {"VolumeUp", KeyCategory::Volume, static_cast<unsigned>(PhysicalKind::Key), NameSource::Config},
    {"VolumeDown", KeyCategory::Volume, static_cast<unsigned>(PhysicalKind::Key), NameSource::Config},
    {"VolumeMute", KeyCategory::Volume, static_cast<unsigned>(PhysicalKind::Key), NameSource::Config},

    {"ChannelUp", KeyCategory::Channel, static_cast<unsigned>(PhysicalKind::Key), NameSource::Config},
    {"ChannelDown", KeyCategory::Channel, static_cast<unsigned>(PhysicalKind::Key), NameSource::Config},
    {"PrevChannel", KeyCategory::Channel, static_cast<unsigned>(PhysicalKind::Key), NameSource::Config},

    // Транспорт. `Rewind`, `Play`, `Pause`, `Record`, `Stop` названы по тому, что клавиши
    // отправляют (300/350), поэтому измерение важнее конфигурации; `Media` — только надпись.
    {"Rewind", KeyCategory::Transport, static_cast<unsigned>(PhysicalKind::Key), NameSource::Measured},
    {"Play", KeyCategory::Transport, static_cast<unsigned>(PhysicalKind::Key), NameSource::Measured},
    {"Pause", KeyCategory::Transport, static_cast<unsigned>(PhysicalKind::Key), NameSource::Measured},
    {"FastForward", KeyCategory::Transport, static_cast<unsigned>(PhysicalKind::Key), NameSource::Config},
    {"Record", KeyCategory::Transport, static_cast<unsigned>(PhysicalKind::Key), NameSource::Measured},
    {"Stop", KeyCategory::Transport, static_cast<unsigned>(PhysicalKind::Key), NameSource::Measured},
    {"SkipBack", KeyCategory::Transport, static_cast<unsigned>(PhysicalKind::Key), NameSource::Config},
    {"SkipForward", KeyCategory::Transport, static_cast<unsigned>(PhysicalKind::Key), NameSource::Config},
    {"Media", KeyCategory::Transport, static_cast<unsigned>(PhysicalKind::Key), NameSource::Printed},

    {"Number0", KeyCategory::Digit, static_cast<unsigned>(PhysicalKind::Key), NameSource::Config},
    {"Number1", KeyCategory::Digit, static_cast<unsigned>(PhysicalKind::Key), NameSource::Config},
    {"Number2", KeyCategory::Digit, static_cast<unsigned>(PhysicalKind::Key), NameSource::Config},
    {"Number3", KeyCategory::Digit, static_cast<unsigned>(PhysicalKind::Key), NameSource::Config},
    {"Number4", KeyCategory::Digit, static_cast<unsigned>(PhysicalKind::Key), NameSource::Config},
    {"Number5", KeyCategory::Digit, static_cast<unsigned>(PhysicalKind::Key), NameSource::Config},
    {"Number6", KeyCategory::Digit, static_cast<unsigned>(PhysicalKind::Key), NameSource::Config},
    {"Number7", KeyCategory::Digit, static_cast<unsigned>(PhysicalKind::Key), NameSource::Config},
    {"Number8", KeyCategory::Digit, static_cast<unsigned>(PhysicalKind::Key), NameSource::Config},
    {"Number9", KeyCategory::Digit, static_cast<unsigned>(PhysicalKind::Key), NameSource::Config},
    {"NumberPlus", KeyCategory::Digit, static_cast<unsigned>(PhysicalKind::Key), NameSource::Measured},
    {"NumberEnter", KeyCategory::Digit, static_cast<unsigned>(PhysicalKind::Key), NameSource::Measured},
    {"NumberStar", KeyCategory::Digit, static_cast<unsigned>(PhysicalKind::Key), NameSource::Printed},
    {"NumberHash", KeyCategory::Digit, static_cast<unsigned>(PhysicalKind::Key), NameSource::Printed},

    {"Red", KeyCategory::Color, static_cast<unsigned>(PhysicalKind::Key), NameSource::Config},
    {"Green", KeyCategory::Color, static_cast<unsigned>(PhysicalKind::Key), NameSource::Config},
    {"Yellow", KeyCategory::Color, static_cast<unsigned>(PhysicalKind::Key), NameSource::Config},
    {"Blue", KeyCategory::Color, static_cast<unsigned>(PhysicalKind::Key), NameSource::Config},

    // Экран. `Options` — имени с источником нет (правило 9): в keys.md и в afterglow это имя
    // экрана, а не клавиши. `Devices` и четыре клавиши по углам экрана подтверждены только
    // печатными алиасами (`Device`, `ScreenLeft1…4`, `ScreenRight1…4`).
    {"Options", KeyCategory::Screen, static_cast<unsigned>(PhysicalKind::ScreenSoft), NameSource::Unknown},
    {"Devices", KeyCategory::Screen, static_cast<unsigned>(PhysicalKind::ScreenSoft), NameSource::Printed},
    {"SoftLeft1", KeyCategory::Screen, static_cast<unsigned>(PhysicalKind::ScreenSoft), NameSource::Printed},
    {"SoftLeft2", KeyCategory::Screen, static_cast<unsigned>(PhysicalKind::ScreenSoft), NameSource::Printed},
    {"SoftLeft3", KeyCategory::Screen, static_cast<unsigned>(PhysicalKind::ScreenSoft), NameSource::Printed},
    {"SoftLeft4", KeyCategory::Screen, static_cast<unsigned>(PhysicalKind::ScreenSoft), NameSource::Printed},
    {"SoftRight1", KeyCategory::Screen, static_cast<unsigned>(PhysicalKind::ScreenSoft), NameSource::Printed},
    {"SoftRight2", KeyCategory::Screen, static_cast<unsigned>(PhysicalKind::ScreenSoft), NameSource::Printed},
    {"SoftRight3", KeyCategory::Screen, static_cast<unsigned>(PhysicalKind::ScreenSoft), NameSource::Printed},
    {"SoftRight4", KeyCategory::Screen, static_cast<unsigned>(PhysicalKind::ScreenSoft), NameSource::Printed},

    // Листание. Имена подтверждены печатными алиасами `ScreenPrev`/`ScreenNext` (One, 885).
    {"PageLeft", KeyCategory::Page, static_cast<unsigned>(PhysicalKind::PagePrev), NameSource::Printed},
    {"PageRight", KeyCategory::Page, static_cast<unsigned>(PhysicalKind::PageNext), NameSource::Printed},
}};

// Алиасы (раздел 2.1 [../key-names.md]): импорт и миграция принимают алиас, редактор
// показывает каноническое имя. Порядок — порядок таблицы документа.
constexpr std::array<KeyAlias, 20> kAliases = {{
    {"AllOff", "Off"},
    {"Enter", "NumberEnter"},
    {"Clear", "NumberPlus"},
    {"Star", "NumberStar"},
    {"Hash", "NumberHash"},
    {"Device", "Devices"},
    {"ListenToMusic", "ListenMusic"},
    {"WatchAMovie", "WatchMovie"},
    {"ScreenPrev", "PageLeft"},
    {"ScreenNext", "PageRight"},
    {"ScreenLeft1", "SoftLeft1"},
    {"ScreenLeft2", "SoftLeft2"},
    {"ScreenLeft3", "SoftLeft3"},
    {"ScreenLeft4", "SoftLeft4"},
    {"ScreenRight1", "SoftRight1"},
    {"ScreenRight2", "SoftRight2"},
    {"ScreenRight3", "SoftRight3"},
    {"ScreenRight4", "SoftRight4"},
    {"Tv", "TV"},
    {"Dvr", "DVR"},
}};

/// Имена видов — они же значения `hardware.zones[].kind`.
constexpr std::array<std::pair<std::string_view, PhysicalKind>, 6> kKinds = {{
    {"Key", PhysicalKind::Key},
    {"System", PhysicalKind::System},
    {"ScreenActs", PhysicalKind::ScreenActs},
    {"ScreenSoft", PhysicalKind::ScreenSoft},
    {"PagePrev", PhysicalKind::PagePrev},
    {"PageNext", PhysicalKind::PageNext},
}};

bool allDigits(std::string_view rest) {
    if (rest.empty())
        return false;
    return std::all_of(rest.begin(), rest.end(), [](char c) { return std::isdigit(static_cast<unsigned char>(c)) != 0; });
}

/// `ScanNN` — пометка неподтверждённого кода (K2: `Scan2C`), одна или две hex-цифры в верхнем регистре.
bool isScanMark(std::string_view rest) {
    if (rest.empty() || rest.size() > 2)
        return false;
    return std::all_of(rest.begin(), rest.end(), [](char c) {
        return std::isdigit(static_cast<unsigned char>(c)) != 0 || (c >= 'A' && c <= 'F');
    });
}

} // namespace

[[nodiscard]] std::span<const KeyNameInfo> keyVocabulary() { return kDictionary; }

[[nodiscard]] std::span<const KeyAlias> keyAliases() { return kAliases; }

[[nodiscard]] const KeyNameInfo* keyInfo(std::string_view name) {
    for (const auto& entry : kDictionary)
        if (entry.name == name)
            return &entry;
    return nullptr;
}

[[nodiscard]] std::optional<std::string_view> canonicalKeyName(std::string_view nameOrAlias) {
    if (keyInfo(nameOrAlias) != nullptr)
        return nameOrAlias;
    for (const auto& alias : kAliases)
        if (alias.alias == nameOrAlias)
            return alias.canonical;
    return std::nullopt;
}

[[nodiscard]] std::vector<std::string_view> keyNamesForKind(PhysicalKind kind) {
    const auto bit = static_cast<unsigned>(kind);
    std::vector<std::string_view> names;
    for (const auto& entry : kDictionary)
        if (entry.kinds & bit)
            names.push_back(entry.name);
    return names;
}

[[nodiscard]] std::optional<PhysicalKind> physicalKind(std::string_view kind) {
    for (const auto& [name, value] : kKinds)
        if (name == kind)
            return value;
    return std::nullopt;
}

[[nodiscard]] std::string_view physicalKindName(PhysicalKind kind) {
    for (const auto& [name, value] : kKinds)
        if (value == kind)
            return name;
    return {};
}

[[nodiscard]] bool isTemporaryZoneName(std::string_view name) {
    if (name.empty())
        return false;
    if (name.size() > 5 && name.substr(name.size() - 5) == "_copy")
        return true;
    if (name == "Auto" || (name.starts_with("Auto") && allDigits(name.substr(4))))
        return true;
    if (name == "Zone" || (name.starts_with("Zone") && allDigits(name.substr(4))))
        return true;
    if (name.starts_with("Scan"))
        return isScanMark(name.substr(4));
    return false;
}

} // namespace harmony
