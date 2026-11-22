/**
 * @file remoteCatalogTest.cpp
 * @brief Каталог моделей: поиск по skin, семейство прошивки, уникальность и связь с профилями.
 */

#include "harmony/core/remoteCatalog.h"
#include "harmony/core/remoteProfile.h"

#include <gtest/gtest.h>

#include <set>
#include <string>
#include <string_view>

using namespace harmony;

TEST(RemoteCatalog, FindsModelBySkin) {
    const auto* model = catalogModelBySkin(61);
    ASSERT_NE(model, nullptr);
    EXPECT_EQ(model->id, "900");

    ASSERT_NE(catalogModelBySkin(72), nullptr);
    EXPECT_EQ(catalogModelBySkin(72)->id, "650");

    for (const int skin : {62, 63, 64}) {
        ASSERT_NE(catalogModelBySkin(skin), nullptr) << skin;
        EXPECT_EQ(catalogModelBySkin(skin)->id, "1100") << skin;
    }

    EXPECT_EQ(catalogModelBySkin(9999), nullptr);
}

TEST(RemoteCatalog, ReadableModelsHaveArchitecture) {
    for (const auto& model : remoteCatalog()) {
        if (!model.readable) {
            continue;
        }
        EXPECT_NE(model.architecture, 0) << model.id;
    }
}

TEST(RemoteCatalog, SkinsAndIdsAreUnique) {
    std::set<std::string_view> ids;
    std::set<int> skins;
    for (const auto& model : remoteCatalog()) {
        EXPECT_TRUE(ids.insert(model.id).second) << model.id;
        for (const int skin : model.skins) {
            EXPECT_TRUE(skins.insert(skin).second) << model.id << ": " << skin;
        }
    }
}

TEST(RemoteCatalog, NormalizesIdCase) {
    EXPECT_EQ(normalizedModelId("1100I"), "1100i");
    EXPECT_EQ(normalizedModelId("900"), "900");
    EXPECT_EQ(normalizedModelId("Harmony-ONE"), "harmony-one");
}

TEST(RemoteCatalog, BuiltinProfilesHaveCatalogModels) {
    for (const auto& profile : bundledRemoteProfiles) {
        EXPECT_NE(catalogModel(profile.id), nullptr) << profile.id;
    }
}

TEST(RemoteCatalog, MissingArchitectureMeansUnknown) {
    const RemoteModel unknown{};
    EXPECT_EQ(unknown.architecture, 0);
    EXPECT_EQ(unknown.link, LinkKind::None);
    EXPECT_FALSE(unknown.readable);
    EXPECT_FALSE(unknown.rawRestore);
    EXPECT_FALSE(unknown.configWriteTested);
}

TEST(RemoteCatalog, DisplayNameComesFromCatalogThenProfile) {
    EXPECT_EQ(modelDisplayName("525"), "Harmony 525");
    EXPECT_EQ(modelDisplayName("900"), "Harmony 900");
    EXPECT_EQ(modelDisplayName(""), "");
    EXPECT_EQ(modelDisplayName("no-such"), "no-such");

    const auto saved = remoteProfiles;
    remoteProfiles.clear();
    EXPECT_EQ(modelDisplayName("900"), "Harmony 900");
    remoteProfiles.push_back({"900", "Profile name must not replace catalog name", "", ""});
    EXPECT_EQ(modelDisplayName("900"), "Harmony 900");
    remoteProfiles.push_back({"my-remote", "My remote", "", ""});
    EXPECT_EQ(modelDisplayName("my-remote"), "My remote");
    remoteProfiles = saved;
}

// Решено владельцем 22.11: способ подключения берётся из remote.json модели, если поле задано;
// если поля нет — из каталога. Право записи при этом решает каталог (OD9, W2).
TEST(RemoteCatalog, ConnectionFollowsFileWhenTheFieldIsSet) {
    // Без поля в файле — каталог.
    EXPECT_EQ(modelLink("900"), LinkKind::UsbNet);
    EXPECT_EQ(modelLink("1000"), LinkKind::UsbNet);
    EXPECT_EQ(modelLink("xbox360"), LinkKind::UsbNet);
    EXPECT_EQ(modelLink("650"), LinkKind::UsbHid);
    EXPECT_EQ(modelLink("525"), LinkKind::UsbHid);
    EXPECT_EQ(modelLink("touch"), LinkKind::Mh);
    EXPECT_EQ(modelLink("home-hub"), LinkKind::Hub);
    EXPECT_EQ(modelLink("no-such"), LinkKind::None);

    EXPECT_TRUE(usesUsbNet("1000")) << "каталог знает 1000 без профиля";
    EXPECT_FALSE(usesUsbNet("650"));
    EXPECT_FALSE(usesUsbNet("touch"));
    EXPECT_FALSE(usesUsbNet("no-such"));

    const auto saved = remoteProfiles;
    // Значение профиля того же id: remoteProfile() берёт первую запись, поэтому менять нужно её.
    const auto setDriver = [](const std::string& id, const std::string& value) {
        for (auto& profile : remoteProfiles)
            if (profile.id == id) profile.driver = value;
    };

    // Своя модель вне каталога: каталог её не знает, работает значение из remote.json.
    remoteProfiles.push_back({"my-remote", "My remote", "", "usb-net"});
    EXPECT_EQ(modelLink("my-remote"), LinkKind::UsbNet);
    EXPECT_TRUE(usesUsbNet("my-remote"));

    // Для модели каталога заданное значение важнее каталога.
    setDriver("650", "usb-net");
    EXPECT_EQ(modelLink("650"), LinkKind::UsbNet);
    EXPECT_TRUE(usesUsbNet("650"));
    // Неизвестное имя значения не даёт: остаётся каталог.
    setDriver("650", "unknown-driver");
    EXPECT_EQ(modelLink("650"), LinkKind::UsbHid);
    // `none` в файле — тоже решение пользователя: связи нет.
    setDriver("650", "none");
    EXPECT_EQ(modelLink("650"), LinkKind::None);

    // Значение «hub» из файла для модели, которой в каталоге нет.
    remoteProfiles.push_back({"my-hub", "My hub", "", "hub"});
    EXPECT_EQ(modelLink("my-hub"), LinkKind::Hub);
    remoteProfiles = saved;
}
