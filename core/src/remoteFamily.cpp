/**
 * @file remoteFamily.cpp
 * @brief Описание семейств архитектур: рамка контейнера, экран и особенности разбора.
 *
 * Данные особенностей разбора взяты из реальных веток `binaryHid.cpp`, а не придуманы
 * заранее. Каждый столбец здесь заменяет одну ветку по номеру архитектуры.
 */

#include "harmony/core/remoteFamily.h"

#include <array>

namespace harmony {

namespace {

// Точные слоты секций для arch 10: логический номер не совпадает с фактическим слотом.
constexpr std::array<SectionSlot, 15> kArch10SectionSlots = {{
    {1, 0}, {3, 4}, {4, 5}, {5, 6}, {6, 9}, {7, 10}, {9, 11}, {10, 12},
    {11, 14}, {12, 15}, {15, 18}, {16, 19}, {17, 20}, {18, 21}, {19, 22},
}};

// Имена взяты из комментариев `ContainerFamily`, экран arch 14 — из `softLabels()`
// и ref профиля 600. Для остальных архитектур данных экрана нет, поэтому нули.
const std::array<RemoteFamily, 6> kFamilies = {{
    {.architecture = 8, .title = "720,785,88x", .container = formats::ContainerFamily::Arch8,
     .wideTextCodes = false, .monoGlyphs = false, .nestedDrawingFollows0x16 = true,
     .commandIndexShift = 0, .sectionFirstSplit = 8, .sectionSecondSplit = -1,
     .sectionSlots = {}, .screen = {}},
    {.architecture = 9, .title = "510–555", .container = formats::ContainerFamily::Arch9,
     .wideTextCodes = true, .monoGlyphs = true, .nestedDrawingFollows0x16 = false,
     .commandIndexShift = 0, .sectionFirstSplit = -1, .sectionSecondSplit = -1,
     .sectionSlots = {}, .screen = ScreenGeometry{
         .width = 0, .height = 0, .headerBottom = 0, .footerTop = 0,
         .softQuarterX = 0, .softQuarterY = 0, .monochrome = true,
     }},
    {.architecture = 10, .title = "890,895", .container = formats::ContainerFamily::Arch10,
     .wideTextCodes = true, .monoGlyphs = false, .nestedDrawingFollows0x16 = true,
     .commandIndexShift = 0, .sectionFirstSplit = -1, .sectionSecondSplit = -1,
     .sectionSlots = kArch10SectionSlots, .screen = {}},
    {.architecture = 12, .title = "One", .container = formats::ContainerFamily::Arch12,
     .wideTextCodes = true, .monoGlyphs = false, .nestedDrawingFollows0x16 = true,
     .commandIndexShift = 1, .sectionFirstSplit = 8, .sectionSecondSplit = 17,
     .sectionSlots = {}, .screen = {}},
    {.architecture = 14, .title = "600/650/665/700", .container = formats::ContainerFamily::Arch14,
     .wideTextCodes = true, .monoGlyphs = false, .nestedDrawingFollows0x16 = true,
     .commandIndexShift = 0, .sectionFirstSplit = -1, .sectionSecondSplit = -1,
     .sectionSlots = {}, .screen = ScreenGeometry{
         .width = 172, .height = 182, .headerBottom = 16, .footerTop = 110,
         .softQuarterX = 64, .softQuarterY = 60, .monochrome = false,
     }},
    {.architecture = 16, .title = "200,300,350", .container = formats::ContainerFamily::Arch16,
     .wideTextCodes = true, .monoGlyphs = false, .nestedDrawingFollows0x16 = true,
     .commandIndexShift = 0, .sectionFirstSplit = -1, .sectionSecondSplit = -1,
     .sectionSlots = {}, .screen = {}},
}};

} // namespace

const RemoteFamily* remoteFamily(int architecture) {
    for (const auto& family : kFamilies) {
        if (family.architecture == architecture) {
            return &family;
        }
    }
    return nullptr;
}

} // namespace harmony
