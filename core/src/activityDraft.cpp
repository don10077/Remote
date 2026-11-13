/**
 * @file activityDraft.cpp
 * @brief Типы активностей, их свойства и автоназначение аппаратных кнопок.
 */

#include "harmony/core/activityDraft.h"

#include "activityTemplate.h"

#include <algorithm>
#include <map>
#include <cctype>
#include <string_view>

namespace harmony {

namespace {

/// Кнопка пульта и подходящие ей имена команд — в порядке предпочтения.
struct KeyCandidates {
    std::string_view key;
    std::vector<std::string_view> commands;
    bool volume = false; ///< Кнопка громкости: уходит устройству громкости
};

/**
 * Таблица собрана по конфигурациям с пульта: какими командами устройства
 * обычно назначают каждую кнопку. Порядок — как в файлах пульта.
 */
const std::vector<KeyCandidates>& keyTable() {
    static const std::vector<KeyCandidates> table = {
        {"Menu", {"Menu", "Setup"}},
        {"Exit", {"Exit", "Back", "Return"}},
        {"Info", {"Info", "Display"}},
        {"Guide", {"Guide", "Home", "TopMenu"}},
        {"Red", {"Red", "A"}},
        {"Green", {"Green", "B"}},
        {"Yellow", {"Yellow", "C"}},
        {"Blue", {"Blue", "D"}},
        {"VolumeUp", {"VolumeUp"}, true},
        {"VolumeDown", {"VolumeDown"}, true},
        {"VolumeMute", {"Mute", "VolumeMute"}, true},
        {"DirectionUp", {"DirectionUp"}},
        {"DirectionDown", {"DirectionDown"}},
        {"DirectionLeft", {"DirectionLeft"}},
        {"DirectionRight", {"DirectionRight"}},
        {"Select", {"Select", "OK", "Enter"}},
        {"ChannelUp", {"ChannelUp", "PageUp"}},
        {"ChannelDown", {"ChannelDown", "PageDown"}},
        {"PrevChannel", {"PrevChannel", "ChannelPrev", "Last", "Return", "Back"}},
        {"Play", {"Play", "Play/Pause", "PlayPause"}},
        {"Pause", {"Pause", "Play/Pause", "PlayPause"}},
        {"Rewind", {"Rewind"}},
        {"FastForward", {"FastForward"}},
        {"SkipBack", {"SkipBack", "ChapterPrev", "SkipBackward", "Previous"}},
        {"SkipForward", {"SkipForward", "ChapterNext", "Next"}},
        {"Stop", {"Stop"}},
        {"Record", {"Record"}},
        {"Number1", {"1"}},
        {"Number2", {"2"}},
        {"Number3", {"3"}},
        {"Number4", {"4"}},
        {"Number5", {"5"}},
        {"Number6", {"6"}},
        {"Number7", {"7"}},
        {"Number8", {"8"}},
        {"Number9", {"9"}},
        {"Number0", {"0"}},
        {"NumberEnter", {"Enter", "NumberEnter"}},
        {"NumberPlus", {"Clear", "-/--"}},
    };
    return table;
}

bool equalsIgnoreCase(std::string_view a, std::string_view b) {
    return std::ranges::equal(a, b, [](char x, char y) {
        return std::tolower(static_cast<unsigned char>(x)) == std::tolower(static_cast<unsigned char>(y));
    });
}

/// Команда устройства, подходящая под одно из имён; nullptr — нет.
const model::Command* findCandidate(const model::Device& device,
                                    const std::vector<std::string_view>& names) {
    for (const std::string_view name : names) {
        for (const auto& command : device.commands) {
            if (equalsIgnoreCase(command.name, name)) {
                return &command;
            }
        }
    }
    return nullptr;
}

} // namespace

ActivityDraft draftOf(const model::Activity& activity) {
    ActivityDraft draft;
    draft.label = activity.label;
    draft.type = activity.type;
    draft.devices = activity.powerOn;
    draft.controlDevice = activity.roleDevice("DEFAULT");
    draft.volumeDevice = activity.roleDevice("VOLUME");
    draft.displayDevice = activity.roleDevice("DISPLAY");
    draft.inputs = activity.inputs;
    return draft;
}

int buttonsLostBy(const model::Activity& activity, const ActivityDraft& draft) {
    int lost = 0;
    for (const auto& group : activity.controlGroups) {
        for (const auto& button : group.buttons) {
            const std::string deviceId = button.actionId.substr(0, button.actionId.find('_'));
            if (std::ranges::find(draft.devices, deviceId) == draft.devices.end()) {
                ++lost;
            }
        }
    }
    return lost;
}

const std::vector<std::string>& activityTypes() {
    // Все типы, которые знает Harmony 900 (по софту Logitech); частые — первыми.
    static const std::vector<std::string> types = {
        "VirtualTelevisionN",      "VirtualDvd",          "VirtualCdMulti",
        "VirtualRadioSimple",      "VirtualGameConsole",  "VirtualPvr",
        "VirtualVcr",              "VirtualGeneric",      "VirtualMusicServer",
        "VirtualSatelliteMusic",   "VirtualTeletext",     "VirtualPresentation",
        "VirtualMediaCenterTv",    "VirtualMediaCenterVideos", "VirtualMediaCenterMusic",
        "VirtualMediaCenterPictures", "VirtualTiVo",      "VirtualReplayTV",
        "VirtualVcrRecorder",      "VirtualLaserdisc",    "VirtualCdJukebox",
        "VirtualMinidisc",         "VirtualTape",
    };
    return types;
}

std::vector<HardButtonAssignment> planHardButtons(const model::RemoteConfig& config,
                                                  const ActivityDraft& draft) {
    std::vector<HardButtonAssignment> plan;
    const model::Device* control = config.findDevice(draft.controlDevice);
    const model::Device* volume =
        draft.volumeDevice.empty() ? control : config.findDevice(draft.volumeDevice);
    for (const auto& entry : keyTable()) {
        const model::Device* device = entry.volume ? volume : control;
        if (device == nullptr) {
            continue;
        }
        if (const model::Command* command = findCandidate(*device, entry.commands)) {
            plan.push_back({std::string(entry.key), device->id, command->name});
        }
    }
    return plan;
}

namespace detail {

const std::vector<std::pair<std::string, std::string>>& activityProperties(const std::string& type) {
    // Свойства, которые пульт пишет активностям каждого типа (значения по умолчанию).
    static const std::vector<std::pair<std::string, std::string>> television = {
        {"HideModeControl", "False"},          {"HideSurfAllShows", "False"},
        {"HideSurfFavoriteChannels", "False"}, {"ScrollChannelsByPage", "True"},
        {"PowerOffUnusedDevices", "True"},     {"HideSurfFavoriteShows", "False"},
        {"HideModeNavigate", "False"},         {"ActivityStartPage", "Numbers"},
        {"TrainingWheels", "True"},            {"ChannelButtonBehaviour", "BasicChannels"},
        {"MediaButtonMode", "ShowMedia"},      {"ControlGroup_Hard Buttons", "True"},
        {"ScrollShowsByPage", "True"},         {"MaxTvContentDays", "14"},
        {"GuideButtonMode", "TunerProgramGuide"}, {"HideSurfAllChannels", "False"},
        {"ControlGroup_Soft Buttons", "True"}, {"UnusedDevicesHelp", "False"},
    };
    static const std::vector<std::pair<std::string, std::string>> dvd = {
        {"HideModeControl", "False"},   {"PowerOffUnusedDevices", "True"},
        {"EnableSmartZoom", "True"},    {"HideModeNavigate", "False"},
        {"EnableSmartMenu", "True"},    {"ActivityStartPage", "Transport"},
        {"TrainingWheels", "True"},     {"PlayOnEnter", "False"},
        {"MediaButtonMode", "ShowMedia"}, {"StopOnExit", "True"},
        {"ControlGroup_Hard Buttons", "True"}, {"ControlGroup_Soft Buttons", "True"},
        {"HideModePlay", "False"},      {"UnusedDevicesHelp", "False"},
    };
    static const std::vector<std::pair<std::string, std::string>> music = {
        {"ControlGroup_Hard Buttons", "True"}, {"HideModeControl", "False"},
        {"PowerOffUnusedDevices", "True"},     {"ActivityStartPage", "Transport"},
        {"HideModePlayMode", "False"},         {"TrainingWheels", "True"},
        {"ControlGroup_Soft Buttons", "True"}, {"PlayOnEnter", "True"},
        {"StopOnExit", "True"},                {"HideModePlay", "False"},
        {"UnusedDevicesHelp", "False"},
    };
    static const std::vector<std::pair<std::string, std::string>> game = {
        {"ControlGroup_Hard Buttons", "True"}, {"HideModeControl", "False"},
        {"PowerOffUnusedDevices", "True"},     {"ActivityStartPage", "GameController"},
        {"TrainingWheels", "True"},            {"ControlGroup_Soft Buttons", "True"},
        {"UnusedDevicesHelp", "False"},
    };
    // Запись ТВ (PVR, TiVo, ReplayTV): как ТВ плюс воспроизведение (наблюдалось у VirtualPvr).
    static const std::vector<std::pair<std::string, std::string>> recorder = [] {
        auto out = television;
        out.insert(out.end(), {{"EnableSmartMenu", "True"}, {"HideModePlay", "False"}, {"RetainStop", "False"}});
        return out;
    }();
    // Радио: каналы и навигация, без телепрограммы.
    static const std::vector<std::pair<std::string, std::string>> radio = {
        {"ControlGroup_Hard Buttons", "True"}, {"HideModeControl", "False"},
        {"PowerOffUnusedDevices", "True"},     {"ActivityStartPage", "Numbers"},
        {"HideModeNavigate", "False"},         {"MediaButtonMode", "ShowMedia"},
        {"ChannelButtonBehaviour", "BasicChannels"}, {"MaxTvContentDays", "14"},
        {"TrainingWheels", "True"},            {"ControlGroup_Soft Buttons", "True"},
        {"UnusedDevicesHelp", "False"},
    };
    // Прочее (своя, презентация, фото): только общие для всех типов свойства.
    static const std::vector<std::pair<std::string, std::string>> generic = {
        {"ControlGroup_Hard Buttons", "True"}, {"HideModeControl", "False"},
        {"PowerOffUnusedDevices", "True"},     {"ActivityStartPage", "Transport"},
        {"TrainingWheels", "True"},            {"ControlGroup_Soft Buttons", "True"},
        {"UnusedDevicesHelp", "False"},
    };
    static const std::map<std::string, const std::vector<std::pair<std::string, std::string>>*> byType = {
        {"VirtualTelevisionN", &television},   {"VirtualMediaCenterTv", &television},
        {"VirtualTeletext", &television},      {"VirtualPvr", &recorder},
        {"VirtualTiVo", &recorder},            {"VirtualReplayTV", &recorder},
        {"VirtualDvd", &dvd},                  {"VirtualVcr", &dvd},
        {"VirtualVcrRecorder", &dvd},          {"VirtualLaserdisc", &dvd},
        {"VirtualMediaCenterVideos", &dvd},    {"VirtualCdMulti", &music},
        {"VirtualCdJukebox", &music},          {"VirtualMinidisc", &music},
        {"VirtualTape", &music},               {"VirtualMusicServer", &music},
        {"VirtualMediaCenterMusic", &music},   {"VirtualRadioSimple", &radio},
        {"VirtualSatelliteMusic", &radio},     {"VirtualGameConsole", &game},
        {"VirtualGeneric", &generic},          {"VirtualPresentation", &generic},
        {"VirtualMediaCenterPictures", &generic},
    };
    const auto it = byType.find(type);
    return it != byType.end() ? *it->second : television;
}

} // namespace detail

} // namespace harmony
