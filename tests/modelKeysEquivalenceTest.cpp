/**
 * @file modelKeysEquivalenceTest.cpp
 * @brief K4: для каждой встроенной модели список клавиш совпадает с нынешним
 *        `RemoteView::hardKeyIds()` — это условие шага A1.
 *
 * Тест сравнивает два решения программы, а не содержимое файлов: порядок и состав списка
 * клавиш модели. Профили читаются как данные (они в git), и имена зон и ячеек экранов
 * извлекаются по тем же правилам, что `hardKeyIds()` — размер фото, вид зоны `Key`, экраны по
 * алфавиту id и ячейки в порядке файла. Если эти правила разойдутся, A1 перестанет быть
 * чистым переносом, и тест это покажет.
 */

#include "../app/src/remoteResources.h"
#include "../app/src/modern/remoteView.h"
#include "../app/src/modern/remoteViewShared.h"

#include "harmony/core/modelKeys.h"
#include "harmony/core/modelInfo.h"
#include "harmony/core/remoteProfile.h"

#include <QJsonArray>
#include <QJsonObject>
#include <QPixmap>
#include <QStringList>
#include <QTemporaryDir>
#include <gtest/gtest.h>

#include <algorithm>
#include <map>
#include <string>
#include <vector>

using namespace harmony;

namespace {
namespace resources = harmony::app::remoteResources;

/// Каждый тест работает со своим снимком ресурсов: профили как данные, в git.
class ModelKeysEquivalenceTest : public testing::Test {
protected:
    QTemporaryDir root;
    void SetUp() override {
        ASSERT_TRUE(root.isValid());
        ASSERT_TRUE(resources::reload(root.path()).isEmpty());
    }
    void TearDown() override { resources::reload(root.path()); }
};

const ModelKey* keyNamed(const ModelKeys& keys, const std::string& name) {
    for (const auto& key : keys.keys)
        if (key.name == name)
            return &key;
    return nullptr;
}

/// Имена зон вида `Key` профиля в порядке файла — так же, как их берёт `hardKeyIds()`.
std::vector<std::string> profileKeysOf(const QString& id) {
    const auto remote = resources::remote(id);
    const auto hardware = remote.value("hardware").toObject();
    const auto size = hardware.value("size").toArray();
    const auto* profile = remoteProfile(id.toStdString());
    if (profile == nullptr)
        return {};
    QPixmap image;
    image.loadFromData(resources::data(QString::fromUtf8(profile->image.data())));
    if (image.isNull() || size.size() != 2)
        return {};
    const QSize declared(size[0].toInt(), size[1].toInt());
    if (declared != image.size())
        return {};
    const QRectF bounds(QPointF(0, 0), QSizeF(image.size()));
    std::vector<std::string> keys;
    for (const auto& entry : hardware.value("zones").toArray()) {
        const auto zone = entry.toObject();
        const QString key = zone.value("id").toString();
        const QRectF rect = harmony::app::remoteViewDetail::readRect(zone.value("rect"));
        if (zone.value("kind").toString() == "Key" && !key.isEmpty() && rect.isValid() &&
            bounds.contains(rect) && zone.value("radius").isDouble())
            keys.push_back(key.toStdString());
    }
    return keys;
}

/// Поле `key` ячеек экранов профиля: экраны по алфавиту id, ячейки в порядке файла.
std::vector<std::string> screenKeysOf(const QString& id) {
    const auto remote = resources::remote(id);
    std::map<QString, QJsonArray> cellsByScreen;   // map сортирует по ключу — экраны по алфавиту
    for (const auto& entry : remote.value("screens").toArray())
        cellsByScreen[entry.toObject().value("id").toString()] = entry.toObject().value("cells").toArray();
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

TEST_F(ModelKeysEquivalenceTest, ForEveryBuiltInModelTheListMatchesHardKeyIds) {
    ASSERT_FALSE(bundledRemoteProfiles.empty()) << "встроенных профилей нет — тест ничего не проверяет";

    int withKeys = 0;
    for (const auto& profile : bundledRemoteProfiles) {
        const QString id = QString::fromStdString(profile.id);
        const auto profileKeys = profileKeysOf(id);
        const auto screenKeys = screenKeysOf(id);
        if (profileKeys.empty() && screenKeys.empty())
            continue;   // профиля с такими именами нет: сравнивать нечего
        ++withKeys;

        const auto keys = modelKeys(modelInfo(profile.id), profileKeys, screenKeys, nullptr);
        QStringList names;
        for (const auto& key : keys.keys)
            names.append(QString::fromStdString(key.name));
        EXPECT_EQ(names, harmony::app::RemoteView::hardKeyIds(id)) << profile.id;
        EXPECT_TRUE(keys.complete) << profile.id;
    }

    // Проверка не вакуумная: хотя бы один встроенный профиль даёт список клавиш.
    EXPECT_GT(withKeys, 0);
}

TEST_F(ModelKeysEquivalenceTest, The1100ProfileListsScreenCellsAsAssignableKeys) {
    const QString id = "1100";
    const auto profileKeys = profileKeysOf(id);
    const auto screenKeys = screenKeysOf(id);
    ASSERT_FALSE(profileKeys.empty());
    ASSERT_FALSE(screenKeys.empty());

    const auto keys = modelKeys(modelInfo(id.toStdString()), profileKeys, screenKeys, nullptr);

    // Транспорт, цифры и Menu/Guide/Info/Exit — назначаемые кнопки модели.
    for (const auto* name : {"Play", "Pause", "Slow", "Number5", "NumberStar", "Menu", "Exit"})
        ASSERT_TRUE(keyNamed(keys, std::string(name))) << name;

    // Ячейки без поля `key` (`CornerTL`, `Help`) — не клавиши: их в списке нет.
    EXPECT_FALSE(keyNamed(keys, "CornerTL"));
    EXPECT_FALSE(keyNamed(keys, "Help"));

    // `Play` у 1100 — ячейка экрана, а не физическая клавиша; `Slow` — имя вне словаря.
    EXPECT_EQ(keyNamed(keys, "Play")->source, KeySource::ProfileScreen);
    EXPECT_EQ(std::find(profileKeys.begin(), profileKeys.end(), std::string("Play")), profileKeys.end());
    EXPECT_FALSE(keyNamed(keys, "Slow")->category.has_value());
}
