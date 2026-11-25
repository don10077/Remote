#include "harmony/core/modelInfo.h"
#include "harmony/core/configDocument.h"
#include "harmony/core/remoteProfile.h"

#include <gtest/gtest.h>

namespace {
/// Изолированный реестр оформлений, восстановленный после каждого теста.
class ModelInfoTest : public testing::Test {
protected:
    std::vector<harmony::RemoteProfile> saved = harmony::remoteProfiles;
    void SetUp() override { harmony::remoteProfiles.clear(); }
    void TearDown() override { harmony::remoteProfiles = saved; }
};

TEST_F(ModelInfoTest, CatalogModelNeedsNoProfile) {
    const auto info = harmony::modelInfo("525");
    EXPECT_EQ(info.id, "525");
    EXPECT_EQ(info.name, "Harmony 525");
    EXPECT_EQ(info.architecture, 9);
    EXPECT_EQ(info.link, harmony::LinkKind::UsbHid);
    EXPECT_TRUE(info.known);
    EXPECT_FALSE(info.custom);
    EXPECT_FALSE(info.hasProfile);
    EXPECT_FALSE(harmony::modelInfo("900").hasProfile);
    EXPECT_TRUE(harmony::modelInfo("900").known);
}

TEST_F(ModelInfoTest, EmptyAndUnknownIdsHaveNoCapabilities) {
    for (const auto* id : {"", "no-such"}) {
        const auto info = harmony::modelInfo(id);
        EXPECT_EQ(info.id, id);
        EXPECT_EQ(info.name, id);
        EXPECT_EQ(info.architecture, 0);
        EXPECT_EQ(info.link, harmony::LinkKind::None);
        EXPECT_FALSE(info.known);
        EXPECT_FALSE(info.custom);
        EXPECT_FALSE(info.hasProfile);
    }
    EXPECT_TRUE(harmony::profileFor("").folder.empty());
}

TEST_F(ModelInfoTest, ProfileResolutionUsesExactIdAndCurrentRegistry) {
    harmony::remoteProfiles.push_back({"600", "Profile 600", "", ""});
    EXPECT_TRUE(harmony::profileFor("600").loaded);
    EXPECT_EQ(harmony::profileFor("600i").folder, "remotes/600i");
    EXPECT_FALSE(harmony::profileFor("600i").loaded);
    EXPECT_FALSE(harmony::modelInfo("600i").hasProfile);
    harmony::remoteProfiles.push_back({"600i", "Custom name", "", ""});
    const auto info = harmony::modelInfo("600i");
    EXPECT_TRUE(info.hasProfile);
    EXPECT_TRUE(info.known);
    EXPECT_FALSE(info.custom);
    EXPECT_EQ(info.name, "Harmony 600i");
    harmony::remoteProfiles.clear();
    EXPECT_FALSE(harmony::profileFor("600i").loaded);
}

TEST_F(ModelInfoTest, DocumentSummaryFollowsTargetChanges) {
    auto document = harmony::ConfigDocument::createNew("Model info");
    ASSERT_TRUE(document.setTargetRemote("525"));
    EXPECT_EQ(document.targetModel().id, "525");
    EXPECT_EQ(document.targetModel().architecture, 9);
    harmony::remoteProfiles.push_back({"my-remote", "My remote", "", "usb-net"});
    ASSERT_TRUE(document.setTargetRemote("my-remote"));
    const auto info = document.targetModel();
    EXPECT_TRUE(info.custom);
    EXPECT_TRUE(info.hasProfile);
    EXPECT_FALSE(info.known);
    EXPECT_EQ(info.name, "My remote");
    EXPECT_EQ(info.architecture, 0);
    EXPECT_EQ(info.link, harmony::LinkKind::UsbNet)
        << "модели нет в каталоге: способ подключения из profile.json";
}
} // namespace
