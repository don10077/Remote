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

// Имена значков экранных кнопок (U6). Источник — выгрузка значков прошивки 900 из проекта
// afterglow (`icons/buttons`, 87 PNG, GPL-3.0); сами картинки в проект не копируются. По
// `icons/NOTICE.md` те же изображения стоят в прошивке 1100 (`app/assets/global/` в 63.hfw),
// и 5 имён (live, podcasts, teletext_off/on/onoff) 1100 называет иначе — в списке оставлены
// имена 900 (решение владельца 20.11, OD13). Для arch 12 эти имена применяются только на пути
// записи: при чтении бинарного образа значок — безымянный битмап, имя не восстанавливается.
// Регистр важен; порядок — по алфавиту без учёта регистра.
constexpr std::array<std::string_view, 87> kFirmwareIcons = {{
    "3musicNote", "55_sbNowPlaying", "A", "aspect", "B", "back", "Cart", "cbox_circle_default",
    "cbox_square_default", "cbox_triangle_default", "circle", "clock", "closedCaption", "eject", "fastforward",
    "filmstrip", "heart", "list", "live", "mail", "mediaCenter", "myDVR", "myMusic", "myPictures", "myRadio", "myTV",
    "myVideo", "nowplaying", "off", "on", "pause", "play", "playList", "podcasts", "powerOFF", "powerON",
    "powerOnOff", "PS-Circle", "PS-Cross", "PS-Square", "PS_Triangle", "purple", "record", "repeat", "replay",
    "replayTV", "rewind", "sbAdd", "sbFavorites", "sbNowPlaying", "sbPodcast", "sbRepeat", "sbSearch", "sbShuffle",
    "search", "shuffle", "skipBack", "skipForward", "sky", "sleep", "slowMotion", "slowPlay", "SoundEffect",
    "SoundMode", "square", "star", "start", "stop", "stop2", "Subtitle", "switchDisplay", "teletext_blue_default",
    "teletext_green_default", "teletext_off", "teletext_on", "teletext_onoff", "teletext_red_default",
    "teletext_yellow_default", "thumbsdown_default", "thumbsup_default", "tivo", "topMenu", "triangle", "VOD", "X",
    "xBox_Button", "Y",
}};

// Имена взяты из комментариев `ContainerFamily`, экран arch 14 — из `softLabels()`
// и ref профиля 600. Для остальных архитектур данных экрана нет, поэтому нули.
const std::array<RemoteFamily, 9> kFamilies = {{
    {.architecture = 8, .title = "720,785,88x", .container = formats::ContainerFamily::Arch8,
     .wideTextCodes = false, .monoGlyphs = false, .nestedDrawingFollows0x16 = true,
     .commandIndexShift = 0, .sectionFirstSplit = 8, .sectionSecondSplit = -1,
     .sectionSlots = {}, .screen = {}, .answerableFiles = {}, .buttonIcons = {}},
    {.architecture = 9, .title = "510–555", .container = formats::ContainerFamily::Arch9,
     .wideTextCodes = true, .monoGlyphs = true, .nestedDrawingFollows0x16 = false,
     .commandIndexShift = 0, .sectionFirstSplit = -1, .sectionSecondSplit = -1,
     .sectionSlots = {}, .screen = ScreenGeometry{
         .width = 0, .height = 0, .headerBottom = 0, .footerTop = 0,
         .softQuarterX = 0, .softQuarterY = 0, .monochrome = true,
     }, .answerableFiles = {}, .buttonIcons = {}},
    {.architecture = 10, .title = "890,895", .container = formats::ContainerFamily::Arch10,
     .wideTextCodes = true, .monoGlyphs = false, .nestedDrawingFollows0x16 = true,
     .commandIndexShift = 0, .sectionFirstSplit = -1, .sectionSecondSplit = -1,
     .sectionSlots = kArch10SectionSlots, .screen = {}, .answerableFiles = {}, .buttonIcons = {}},
    // arch 11 (1100, 1100i): конфигурация читается как XML или ZIP, бинарного контейнера нет,
    // поэтому флаги разбора — значения по умолчанию, ими никто не пользуется. Строка нужна
    // списку значков: те же изображения, что у 900 (путь записи и выгрузка прошивки 1100).
    {.architecture = 11, .title = "1100, 1100i", .container = std::nullopt,
     .wideTextCodes = true, .monoGlyphs = false, .nestedDrawingFollows0x16 = true,
     .commandIndexShift = 0, .sectionFirstSplit = -1, .sectionSecondSplit = -1,
     .sectionSlots = {}, .screen = {}, .answerableFiles = {}, .buttonIcons = kFirmwareIcons},
    // Имена применяются только на пути записи: при чтении образа One значок — безымянный
    // битмап picture bank, имя не восстанавливается.
    {.architecture = 12, .title = "One", .container = formats::ContainerFamily::Arch12,
     .wideTextCodes = true, .monoGlyphs = false, .nestedDrawingFollows0x16 = true,
     .commandIndexShift = 1, .sectionFirstSplit = 8, .sectionSecondSplit = 17,
     .sectionSlots = {}, .screen = {}, .answerableFiles = {}, .buttonIcons = kFirmwareIcons},
    // Значков нет: кнопки этого поколения текстовые, имён значков нет ни в одном источнике.
    {.architecture = 14, .title = "600/650/665/700", .container = formats::ContainerFamily::Arch14,
     .wideTextCodes = true, .monoGlyphs = false, .nestedDrawingFollows0x16 = true,
     .commandIndexShift = 0, .sectionFirstSplit = -1, .sectionSecondSplit = -1,
     .sectionSlots = {}, .screen = ScreenGeometry{
         .width = 172, .height = 182, .headerBottom = 16, .footerTop = 110,
         .softQuarterX = 64, .softQuarterY = 60, .monochrome = false,
     }, .answerableFiles = {}, .buttonIcons = {}},
    // arch 15 (900, 1000): конфигурация — XML, контейнера нет; строка нужна списку значков
    // прошивки 900. Одна архитектура у 900 и 1000, но разные размеры экрана (S1), поэтому
    // экран здесь не заполняется.
    {.architecture = 15, .title = "900, 1000", .container = std::nullopt,
     .wideTextCodes = true, .monoGlyphs = false, .nestedDrawingFollows0x16 = true,
     .commandIndexShift = 0, .sectionFirstSplit = -1, .sectionSecondSplit = -1,
     .sectionSlots = {}, .screen = {}, .answerableFiles = {}, .buttonIcons = kFirmwareIcons},
    {.architecture = 16, .title = "200,300,350", .container = formats::ContainerFamily::Arch16,
     .wideTextCodes = true, .monoGlyphs = false, .nestedDrawingFollows0x16 = true,
     .commandIndexShift = 0, .sectionFirstSplit = -1, .sectionSecondSplit = -1,
     .sectionSlots = {}, .screen = {}, .answerableFiles = kArch16Files, .buttonIcons = {}},
    // arch 17 — это ещё и модели хаба (`link: hub`); читатель файлового протокола их не
    // обслуживает, и подтверждений для них нет. Значков нет: кнопки текстовые.
    {.architecture = 17, .title = "Touch, Ultimate, 950", .container = std::nullopt,
     .wideTextCodes = true, .monoGlyphs = false, .nestedDrawingFollows0x16 = true,
     .commandIndexShift = 0, .sectionFirstSplit = -1, .sectionSecondSplit = -1,
     .sectionSlots = {}, .screen = {}, .answerableFiles = kArch17Files, .buttonIcons = {}},
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
