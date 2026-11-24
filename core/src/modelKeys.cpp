/**
 * @file modelKeys.cpp
 * @brief K4: список назначаемых клавиш модели без профиля — порядок, источники и полнота.
 *
 * Программа отвечает на два вопроса раздельно (шаг K4): какие клавиши у модели есть — только по
 * подтверждённым источникам, и что на это можно назначать — физические клавиши плюс имена ячеек
 * экрана профиля. `ModelKeys::complete` отвечает на первый вопрос, список — на второй.
 *
 * Порядок списка — тот же, что у нынешнего `RemoteView::hardKeyIds()`, потому что от него
 * зависит сортировка списков программы: зоны профиля по файлу, ячейки экранов (экраны по
 * алфавиту id, ячейки по файлу), затем таблица в порядке словаря, затем имена конфигурации.
 *
 * Здесь нет кеша и нет чтения файлов: содержимое профиля передаёт приложение (`modelKeysQt`,
 * шаг A1). Функция вызывается из обновления таблиц, гейтов и диалогов, а не из отрисовки.
 *
 * Здесь же решение C1 (`keyIsModelName` и `keysMissingOnModel`): его читают проверка
 * конфигурации и сводка смены модели (C4), поэтому оно одно, а не два.
 */

#include "harmony/core/modelKeys.h"

#include "harmony/core/keyScans.h"
#include "harmony/core/keyVocabulary.h"
#include "harmony/core/remoteCatalog.h"

#include <algorithm>
#include <array>
#include <span>

namespace harmony {

namespace {

constexpr const char* kHardGroup = "HardButtons";

/**
 * Имена по модели там, где скан-кодов нет вовсе: надписи на корпусе.
 *
 * Источник: harmony-explorations `reference/remotes/harmony-touch/keys.md` (MIT) — 27 клавиш
 * клавиатуры Touch, каждая названа по надписи. `Home` и `Favorites` в этот список не входят:
 * это две зоны сенсорной панели над экраном, а не клавиши клавиатуры. Имя `Dvr` записано
 * каноном словаря (`DVR`). Надпись — слабый источник, поэтому такой список не исчерпывающий.
 */
constexpr std::array<std::string_view, 27> kTouchPrintedNames = {{
    "Blue",          "ChannelDown",    "ChannelUp",      "DirectionDown",
    "DirectionLeft", "DirectionRight", "DirectionUp",    "DVR",
    "Exit",          "FastForward",    "Green",          "Guide",
    "Info",          "Menu",           "Off",            "Pause",
    "Play",          "PrevChannel",    "Record",         "Red",
    "Rewind",        "Select",         "Stop",           "VolumeDown",
    "VolumeMute",    "VolumeUp",       "Yellow",
}};

struct PrintedNamesTable {
    std::string_view model;                       ///< Id модели; пусто — имён по модели нет
    std::span<const std::string_view> names;      ///< Имена как в источнике; порядок задаёт словарь
};

/// Таблицы надписей по модели: там, где скан-кодов нет, имена — единственный источник.
constexpr std::array<PrintedNamesTable, 1> kPrintedNamesTables = {{
    {"touch", std::span<const std::string_view>(kTouchPrintedNames)},
}};

/// Таблица надписей по модели; nullptr — модели отвечать нечем.
const PrintedNamesTable* printedNamesTable(std::string_view modelId) {
    const std::string id = normalizedModelId(modelId);
    for (const auto& table : kPrintedNamesTables)
        if (normalizedModelId(table.model) == id)
            return &table;
    return nullptr;
}

/// Порядок словаря — то, чем программа сортирует списки клавиш. -1 — имени в словаре нет.
int dictionaryRank(std::string_view name) {
    const auto& vocabulary = keyVocabulary();
    for (std::size_t i = 0; i < vocabulary.size(); ++i)
        if (vocabulary[i].name == name)
            return static_cast<int>(i);
    return -1;
}

} // namespace

ModelKeys modelKeys(const ModelInfo& model, std::span<const std::string> profileKeys,
                    std::span<const std::string> profileScreenKeys,
                    const model::RemoteConfig* config) {
    ModelKeys result;

    // Имя называется один раз: источник тот, кто ответил о нём раньше.
    auto add = [&result](const std::string& name, KeySource source) {
        for (const auto& existing : result.keys)
            if (existing.name == name)
                return;
        const auto* info = keyInfo(name);
        result.keys.push_back({name, info ? std::optional<KeyCategory>(info->category) : std::nullopt, source});
    };

    // 1) и 2) — профиль: зоны вида `Key` в порядке файла, затем `key` ячеек экранов.
    const bool profileGiven = !profileKeys.empty() || !profileScreenKeys.empty();
    for (const auto& name : profileKeys)
        add(name, KeySource::Profile);
    for (const auto& name : profileScreenKeys)
        add(name, KeySource::ProfileScreen);

    // 3) без профиля — таблица семейства или модели в порядке словаря; если таблиц кодов нет,
    //    но есть таблица надписей по модели, список строится по ней.
    const auto* table = keyScanTable(model.architecture, model.id);
    const auto* printed = printedNamesTable(model.id);
    if (!profileGiven) {
        if (table != nullptr) {
            std::vector<KeyScan> ordered(table->entries.begin(), table->entries.end());
            std::stable_sort(ordered.begin(), ordered.end(), [](const KeyScan& a, const KeyScan& b) {
                return dictionaryRank(a.name) < dictionaryRank(b.name);
            });
            for (const auto& entry : ordered)
                add(std::string(entry.name), KeySource::ScanTable);
        } else if (printed != nullptr) {
            std::vector<std::string_view> ordered(printed->names.begin(), printed->names.end());
            std::stable_sort(ordered.begin(), ordered.end(), [](std::string_view a, std::string_view b) {
                return dictionaryRank(a) < dictionaryRank(b);
            });
            for (const auto& name : ordered)
                add(std::string(name), KeySource::Printed);
        }
    }

    // 4) физические кнопки конфигурации, которых нет выше, — в конец списка.
    if (config != nullptr) {
        for (const auto& device : config->devices)
            for (const auto& group : device.controlGroups)
                for (const auto& button : group.buttons)
                    if (!button.name.empty())
                        add(button.name, KeySource::Config);
        for (const auto& activity : config->activities)
            for (const auto& group : activity.controlGroups)
                for (const auto& button : group.buttons)
                    if (!button.name.empty())
                        add(button.name, KeySource::Config);
    }

    // Полнота — решение о том, можно ли по отсутствию имени решать, что клавиши нет: только
    // подтверждённый источник (профиль или полная таблица) даёт такой ответ.
    result.complete = profileGiven || (table != nullptr && table->complete);
    result.vocabularyFallback = !profileGiven && table == nullptr && printed == nullptr;
    return result;
}

bool keyIsModelName(const ModelKeys& keys, std::string_view name) {
    const auto canonical = canonicalKeyName(name);
    for (const auto& key : keys.keys) {
        if (key.source == KeySource::Config || key.source == KeySource::ProfileScreen) {
            continue;
        }
        if (key.name == name || canonical == key.name) {
            return true;
        }
    }
    return false;
}

std::vector<std::string> keysMissingOnModel(const ModelKeys& keys, const model::RemoteConfig& config) {
    if (!keys.complete) return {};  // неполный список ничего не утверждает
    std::vector<std::string> out;
    const auto look = [&out, &keys](const std::vector<model::ControlGroup>& groups) {
        for (const auto& group : groups) {
            if (group.name != kHardGroup) continue;  // экранная кнопка: имени клавиши нет
            for (const auto& button : group.buttons) {
                if (button.name.empty() || isTemporaryZoneName(button.name)) continue;
                if (keyIsModelName(keys, button.name)) continue;
                if (std::ranges::find(out, button.name) == out.end()) out.push_back(button.name);
            }
        }
    };
    for (const auto& device : config.devices) look(device.controlGroups);
    for (const auto& activity : config.activities) look(activity.controlGroups);
    return out;
}

} // namespace harmony
