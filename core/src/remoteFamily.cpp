/**
 * @file remoteFamily.cpp
 * @brief Описание семейств архитектур: рамка контейнера, экран и особенности разбора.
 *
 * Данные особенностей разбора взяты из реальных веток `binaryHid.cpp`, а не придуманы
 * заранее. Каждый столбец здесь заменяет одну ветку по номеру архитектуры.
 */

#include "harmony/core/remoteFamily.h"

#include <algorithm>
#include <array>

namespace harmony {

namespace {

// Точные слоты секций для arch 10: логический номер не совпадает с фактическим слотом.
constexpr std::array<SectionSlot, 15> kArch10SectionSlots = {{
    {1, 0}, {3, 4}, {4, 5}, {5, 6}, {6, 9}, {7, 10}, {9, 11}, {10, 12},
    {11, 14}, {12, 15}, {15, 18}, {16, 19}, {17, 20}, {18, 21}, {19, 22},
}};

// Файлы, подтверждённые readings на моделях семейства 16 (Harmony 300 и 350). Оба отвечают на
// этих двух моделях: identity читалась с обеих, конфигурация читалась этим путём с обеих.
constexpr std::array<std::string_view, 2> kFileFamilyModels = {"300", "350"};

// Подтверждено на одном пульте поколения Touch — на Harmony Touch (skin 99). Для остальных
// моделей этого семейства ответа нет: путь не считается существующим, пока пульт на него не
// ответил. Модели хаба (Link, Home Hub, Ultimate Hub) имеют тот же номер семейства, но другим
// путём связи, и здесь они не подтверждены.
constexpr std::array<std::string_view, 1> kTouchOnly = {"touch"};

constexpr std::array<AnswerableFile, 2> kArch16Files = {{
    {"/sys/sysinfo", FileShape::TextLines, kFileFamilyModels},
    {"/cfg/usercfg", FileShape::BinaryContainer, kFileFamilyModels},
}};

constexpr std::array<AnswerableFile, 2> kArch17Files = {{
    {"/sys/sysinfo", FileShape::TextLines, kTouchOnly},
    {"/rf/deviceinfo", FileShape::JsonObject, kTouchOnly},
}};

// Имена взяты из комментариев `ContainerFamily`, экран arch 14 — из `softLabels()`
// и ref профиля 600. Для остальных архитектур данных экрана нет, поэтому нули.
const std::array<RemoteFamily, 7> kFamilies = {{
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
     .sectionSlots = {}, .screen = {}, .answerableFiles = kArch16Files},
    // arch 17 — это ещё и модели хаба (`link: hub`); читатель файлового протокола их не
    // обслуживает, и подтверждений для них нет.
    {.architecture = 17, .title = "Touch, Ultimate, 950", .container = std::nullopt,
     .wideTextCodes = true, .monoGlyphs = false, .nestedDrawingFollows0x16 = true,
     .commandIndexShift = 0, .sectionFirstSplit = -1, .sectionSecondSplit = -1,
     .sectionSlots = {}, .screen = {}, .answerableFiles = kArch17Files},
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

const AnswerableFile* answerableFile(int architecture, std::string_view path) {
    const auto* family = remoteFamily(architecture);
    if (!family) return nullptr;
    for (const auto& entry : family->answerableFiles) {
        if (entry.path == path) return &entry;
    }
    return nullptr;
}

bool confirmedOn(const AnswerableFile& entry, std::string_view modelId) {
    return std::ranges::find(entry.confirmedOn, modelId) != entry.confirmedOn.end();
}

} // namespace harmony
