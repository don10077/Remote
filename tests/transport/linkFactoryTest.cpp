/**
 * @file linkFactoryTest.cpp
 * @brief U4b: фабрика связи решает по модели, а не по факту «это 900».
 */

#include "harmony/core/modelInfo.h"
#include "harmony/core/remoteProfile.h"
#include "harmony/transport/linkFactory.h"
#include "harmony/transport/usbNetLink.h"

#include <gtest/gtest.h>

using namespace harmony;
using namespace harmony::transport;

namespace {

/// Изолированный реестр оформлений: `connection.driver` файла важнее каталога (Q6).
class LinkFactoryTest : public testing::Test {
protected:
    std::vector<harmony::RemoteProfile> saved = harmony::remoteProfiles;
    void SetUp() override { harmony::remoteProfiles.clear(); }
    void TearDown() override { harmony::remoteProfiles = saved; }
};

/// Модели каталога с `link: usb-net` — связь по USB-сети. Строка «это 900» в решении нет.
TEST(LinkFactory, UsbNetModelsGetUsbNetLink) {
    for (const auto* id : {"900", "1000", "1000i", "1100", "1100i", "xbox360"}) {
        const auto choice = makeLink(harmony::modelInfo(id));
        ASSERT_TRUE(choice) << id << ": " << choice.error().message;
        EXPECT_EQ(choice->kind, LinkKind::UsbNet) << id;
        ASSERT_TRUE(choice->link) << id;
        EXPECT_TRUE(dynamic_cast<UsbNetLink*>(choice->link.get())) << id;
        EXPECT_FALSE(choice->transferPath) << id;
    }
}

/// usb-hid и mh остаются прежним путём ConcordTransfer: интерфейсы в этом шаге не объединяем.
TEST(LinkFactory, HidAndMhStayOnTheTransferPath) {
    const auto hid = makeLink(harmony::modelInfo("650"));
    ASSERT_TRUE(hid) << hid.error().message;
    EXPECT_EQ(hid->kind, LinkKind::UsbHid);
    EXPECT_TRUE(hid->transferPath);
    EXPECT_EQ(hid->link, nullptr);

    const auto mh = makeLink(harmony::modelInfo("touch"));
    ASSERT_TRUE(mh) << mh.error().message;
    EXPECT_EQ(mh->kind, LinkKind::Mh);
    EXPECT_TRUE(mh->transferPath);
    EXPECT_EQ(mh->link, nullptr);
}

/// Хаб читается из файла, связь через фабрику не открывается.
TEST(LinkFactory, HubIsRefusedWithItsName) {
    const auto hub = makeLink(harmony::modelInfo("ultimate-hub"));
    if (hub) FAIL() << "hub must be refused, got kind " << static_cast<int>(hub->kind);
    EXPECT_EQ(hub.error().code, ErrorCode::Unsupported);
    EXPECT_NE(hub.error().message.find("hub"), std::string::npos) << hub.error().message;
    EXPECT_NE(hub.error().message.find("Harmony Ultimate Hub"), std::string::npos) << hub.error().message;
}

/// Модель не выбрана или её нет в справочнике: фабрика не угадывает, связь выбирается по
/// подключённому железу — это путь `connect("auto") (решение владельца 23.11.2026).
TEST(LinkFactory, UnknownModelIsLeftToTheAutoPath) {
    for (const auto* id : {"", "no-such-model"}) {
        const auto choice = makeLink(harmony::modelInfo(id));
        ASSERT_FALSE(choice) << id;
        EXPECT_EQ(choice.error().code, ErrorCode::Unsupported) << id;
        EXPECT_NE(choice.error().message.find("auto"), std::string::npos)
            << id << ": " << choice.error().message;
    }
}

/// Модель, для которой файл задает `driver: none`, — тоже отказ: связи для неё нет.
TEST_F(LinkFactoryTest, ModelWithoutConnectionIsRefused) {
    harmony::remoteProfiles.push_back(harmony::RemoteProfile{
        .id = "525", .name = "Harmony 525", .image = "", .driver = "none"});
    const auto choice = makeLink(harmony::modelInfo("525"));
    if (choice) FAIL() << "model without connection must be refused, got kind "
                       << static_cast<int>(choice->kind);
    EXPECT_EQ(choice.error().code, ErrorCode::Unsupported);
    EXPECT_NE(choice.error().message.find("none"), std::string::npos) << choice.error().message;
}

/// Решает ModelInfo, а не справочник заново: driver файла важнее каталога (Q6).
TEST_F(LinkFactoryTest, ProfileDriverWinsOverCatalog) {
    harmony::remoteProfiles.push_back(harmony::RemoteProfile{
        .id = "525", .name = "Harmony 525", .image = "", .driver = "usb-net"});
    const auto choice = makeLink(harmony::modelInfo("525"));
    ASSERT_TRUE(choice) << choice.error().message;
    EXPECT_EQ(choice->kind, LinkKind::UsbNet);
    EXPECT_TRUE(dynamic_cast<UsbNetLink*>(choice->link.get()));
    EXPECT_FALSE(choice->transferPath);
}

} // namespace
