/**
 * @file modelKeysQt.cpp
 * @brief A1: имена зон вида `Key` и поля `key` ячеек экранов профиля для ядра.
 *
 * Ядро (`harmony::modelKeys()`, K4) не читает содержимое оформлений, поэтому имена собирает
 * приложение из снимка ресурсов. Порядок — тот же, что у ответа ядра: зоны в
 * порядке файла, затем ячейки экранов (экраны по алфавиту id, ячейки по файлу).
 *
 * Проверка «размер фото совпал с `hardware.size`» здесь не делается: фото нужно только для
 * показа. Для пульта, который `reload()` принял, она выполняется сама собой — `checkModel()`
 * отклоняет оформление с несовпавшим размером, и такой пульт не попадает в снимок.
 *
 * Проверки геометрии зоны (`rect` внутри фото, `radius`) здесь тоже нет: они нужны, чтобы
 * нарисовать зону. Решение о списке клавиш от них не зависит.
 */

#include "modelKeysQt.h"
#include "remoteResources.h"

#include "harmony/core/keyVocabulary.h"

#include <QJsonArray>
#include <QJsonObject>

#include <map>
#include <string>
#include <vector>

namespace harmony::app {
namespace resources = remoteResources;

namespace {

/// Зоны вида `Key` в порядке файла. Пустой список — оформлению modelId нет или оно не принято.
std::vector<std::string> zoneNames(const QString& modelId) {
    const auto remote = resources::remote(modelId);
    const auto hardware = remote.value("hardware").toObject();
    std::vector<std::string> names;
    for (const auto& entry : hardware.value("zones").toArray()) {
        const auto zone = entry.toObject();
        const QString key = zone.value("id").toString();
        if (zone.value("kind").toString() == "Key" && !key.isEmpty())
            names.push_back(key.toStdString());
    }
    return names;
}

/// Поле `key` ячеек экранов: экраны по алфавиту id, ячейки в порядке файла.
std::vector<std::string> cellKeys(const QString& modelId) {
    const auto remote = resources::remote(modelId);
    std::map<QString, QJsonArray> cellsByScreen;   // map сортирует по ключу — экраны по алфавиту
    for (const auto& entry : remote.value("screens").toArray()) {
        const auto screen = entry.toObject();
        cellsByScreen[screen.value("id").toString()] = screen.value("cells").toArray();
    }
    std::vector<std::string> keys;
    for (const auto& [screen, cells] : cellsByScreen)
        for (const auto& entry : cells) {
            const QString key = entry.toObject().value("key").toString();
            if (!key.isEmpty())
                keys.push_back(key.toStdString());
        }
    return keys;
}

} // namespace

harmony::ModelKeys modelKeysFor(const QString& modelId, const harmony::model::RemoteConfig* config) {
    const auto zones = zoneNames(modelId);
    const auto cells = cellKeys(modelId);
    // Пустой modelId — модель не выбрана: modelInfo() ничего не подставляет (Q11).
    return harmony::modelKeys(harmony::modelInfo(modelId.toStdString()), zones, cells, config);
}

QStringList modelKeyIds(const QString& modelId, const harmony::model::RemoteConfig* config) {
    QStringList names;
    for (const auto& key : modelKeysFor(modelId, config).keys)
        names.append(QString::fromStdString(key.name));
    return names;
}

QStringList dictionaryIdsForKind(harmony::PhysicalKind kind) {
    QStringList names;
    for (const auto& name : harmony::keyNamesForKind(kind))
        names.append(QString::fromUtf8(name.data(), static_cast<int>(name.size())));
    return names;
}

QStringList uiKeyIds(const QString& modelId, const harmony::model::RemoteConfig* config) {
    const auto keys = modelKeysFor(modelId, config);
    QStringList names;
    for (const auto& key : keys.keys)
        names.append(QString::fromStdString(key.name));
    // К4: словарь добавляется только там, где модели ответить нечем. Если бы здесь решал
    // «список пуст», имена конфигурации пропадали бы из списка.
    if (keys.vocabularyFallback)
        names += dictionaryIdsForKind(harmony::PhysicalKind::Key);
    return names;
}

} // namespace harmony::app
