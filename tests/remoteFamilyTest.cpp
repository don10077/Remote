/**
 * @file remoteFamilyTest.cpp
 * @brief S1: описание семейства — рамка, экран 600-серии, неизвестная архитектура.
 */

#include "harmony/core/formats/container.h"
#include "harmony/core/remoteFamily.h"

#include <gtest/gtest.h>

TEST(RemoteFamilyTest, EveryContainerFamilyHasRemoteFamily) {
    for (const auto& info : harmony::formats::containerFamilies()) {
        const auto* family = harmony::remoteFamily(info.architecture);
        ASSERT_TRUE(family) << "arch " << info.architecture;
        EXPECT_EQ(family->architecture, info.architecture);
        EXPECT_EQ(family->container, info.family);
        EXPECT_FALSE(family->title.empty()) << "arch " << info.architecture;
    }
}

TEST(RemoteFamilyTest, Arch14ScreenDataIsMovedFromSoftLabels) {
    const auto* family = harmony::remoteFamily(14);
    ASSERT_TRUE(family);
    EXPECT_EQ(family->container, harmony::formats::ContainerFamily::Arch14);
    EXPECT_EQ(family->title, "600/650/665/700");

    // Источник значений — `softLabels()` в `core/src/formats/binaryHid.cpp` и ref экрана
    // профиля 600. Числа перенесены как есть; решение программы — что экран имеет эти границы.
    EXPECT_EQ(family->screen.width, 172);
    EXPECT_EQ(family->screen.height, 182);
    EXPECT_EQ(family->screen.headerBottom, 16);
    EXPECT_EQ(family->screen.footerTop, 110);
    EXPECT_EQ(family->screen.softQuarterX, 64);
    EXPECT_EQ(family->screen.softQuarterY, 60);
    EXPECT_FALSE(family->screen.monochrome);
}

TEST(RemoteFamilyTest, UnknownArchitectureHasNoFamily) {
    EXPECT_EQ(harmony::remoteFamily(0), nullptr);
    EXPECT_EQ(harmony::remoteFamily(99), nullptr);
}
