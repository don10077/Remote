/**
 * @file propertyCatalog.cpp
 * @brief Каталог свойств устройств и активностей (по afterglow library/properties.json, GPL-3.0).
 */

#include "harmony/core/propertyCatalog.h"

#include <algorithm>
#include <array>
#include <cctype>

namespace harmony {

namespace {

using Type = PropertyInfo::Type;

constexpr std::array<std::string_view, 3> kStartPages = {"Numbers", "Transport", "GameController"};
constexpr std::array<std::string_view, 1> kChannelButtons = {"BasicChannels"};
constexpr std::array<std::string_view, 1> kGuideButton = {"TunerProgramGuide"};
constexpr std::array<std::string_view, 1> kMediaButton = {"ShowMedia"};
constexpr std::array<std::string_view, 1> kPvrType = {"Generic"};
constexpr std::array<std::string_view, 1> kTuner = {"Tuner"};

constexpr PropertyInfo flag(PropertyScope scope, std::string_view name, std::string_view label,
                            std::string_view description, bool readOnly = false) {
    PropertyInfo info;
    info.scope = scope;
    info.name = name;
    info.label = label;
    info.description = description;
    info.type = Type::Bool;
    info.readOnly = readOnly;
    return info;
}

constexpr PropertyInfo number(PropertyScope scope, std::string_view name, std::string_view label,
                              std::string_view description, int min, int max) {
    PropertyInfo info;
    info.scope = scope;
    info.name = name;
    info.label = label;
    info.description = description;
    info.type = Type::Int;
    info.min = min;
    info.max = max;
    return info;
}

constexpr PropertyInfo choice(PropertyScope scope, std::string_view name, std::string_view label,
                              std::string_view description, std::span<const std::string_view> values) {
    PropertyInfo info;
    info.scope = scope;
    info.name = name;
    info.label = label;
    info.description = description;
    info.type = Type::Enum;
    info.values = values;
    return info;
}

constexpr auto D = PropertyScope::Device;
constexpr auto T = PropertyScope::DeviceTiming;
constexpr auto A = PropertyScope::Activity;

constexpr std::array kCatalog = {
    // --- Паузы между нажатиями (мс) -----------------------------------------------------
    number(T, "PressPreSilence", "Pause before a press, ms",
           "Quiet time the remote keeps before each tap. Sets how fast the same key can be tapped again: "
           "1000 feels like a second between taps, 300 is responsive.", 0, 10000),
    number(T, "PressInterKey", "Pause between different keys, ms", "Gap the remote keeps between two different keys.",
           0, 10000),
    number(T, "HoldPreSilence", "Pause before a held key, ms", "As above, for a key that is held. Usually 50.", 0,
           10000),
    number(T, "HoldInterKey", "Interval of repeats while held, ms",
           "How often a held key repeats (volume, channel). Usually 100.", 0, 10000),

    // --- Устройство ------------------------------------------------------------------------
    flag(D, "AlwaysOn", "Always on", "You keep this device switched on; activities do not touch its power."),
    flag(D, "ManualPower", "Manual power", "The remote cannot switch this device, so it asks you instead of trying."),
    flag(D, "Scart", "SCART auto-switching",
         "Connected by SCART: switching it on makes the television switch to it by itself. The remote powers it "
         "first and waits for it to settle."),
    flag(D, "IsDisplayDevice", "Is a display", "This device is a screen, so activities can use it in the display role."),
    flag(D, "MenuOnDevice", "Menu on device", "The device has its own on-screen menu."),
    flag(D, "OnScreenGuide", "On-screen guide", "The device provides a programme guide."),
    flag(D, "HasBands", "Has bands", "The tuner has switchable bands (AM/FM)."),
    flag(D, "HasPresets", "Has presets", "The tuner supports preset stations."),
    flag(D, "Dimmer", "Has a dimmer", "The lighting controller supports dimming, not just on/off."),
    flag(D, "RecordMedia Fixed Disc", "Records to disc", "The recorder writes to a built-in disc."),
    number(D, "NumDiscs", "Disc slots", "How many discs the changer holds.", 1, 400),
    number(D, "NumCassettes", "Cassette slots", "How many cassettes the changer holds.", 1, 10),
    number(D, "NumLights", "Lights", "How many lights this controller drives.", 1, 100),
    choice(D, "PvrType", "Recorder type", "Which recorder behaviour the remote assumes.", kPvrType),
    choice(D, "TunerInput", "Tuner input",
           "Always “Tuner” in every configuration seen; read by the touchscreen. What it changes is not established.",
           kTuner),
    flag(D, "IsNewDevice", "Newly added", "Set when a device has just been added; the remote clears it itself.", true),

    // --- Активность ------------------------------------------------------------------------
    flag(A, "PowerOffUnusedDevices", "Power off unused devices", "Turn off devices this activity does not use."),
    choice(A, "ActivityStartPage", "Start page", "Which touchscreen page the activity opens on.", kStartPages),
    flag(A, "PlayOnEnter", "Play on start", "Send Play when the activity starts."),
    flag(A, "StopOnExit", "Stop on exit", "Send Stop when the activity ends."),
    flag(A, "TrainingWheels", "Show help prompts", "Show the remote's built-in help prompts in this activity."),
    flag(A, "UnusedDevicesHelp", "Unused-device help", "Offer help about devices this activity does not use."),
    flag(A, "ControlGroup_Hard Buttons", "Show hard buttons", "Show the physical-button group."),
    flag(A, "ControlGroup_Soft Buttons", "Show soft buttons", "Show the on-screen button group."),
    flag(A, "HideModeControl", "Hide Control page", "Hide the Control page in this activity."),
    flag(A, "HideModeNavigate", "Hide Navigate page", "Hide the Navigate page."),
    flag(A, "HideModePlay", "Hide Play page", "Hide the Play page."),
    flag(A, "HideModePlayMode", "Hide Play-mode page", "Hide the play-mode page."),
    flag(A, "EnableSmartMenu", "Smart menu", "Offer the menu shortcut."),
    flag(A, "EnableSmartZoom", "Smart zoom", "Offer the aspect/zoom shortcut."),
    choice(A, "ChannelButtonBehaviour", "Channel buttons", "What the channel buttons do in this activity.",
           kChannelButtons),
    choice(A, "GuideButtonMode", "Guide button", "What the Guide button opens.", kGuideButton),
    choice(A, "MediaButtonMode", "Media button", "What the Media button opens.", kMediaButton),
    flag(A, "HideSurfAllChannels", "Hide all-channels list", "Hide the full channel list."),
    flag(A, "HideSurfAllShows", "Hide all shows", "Hide the full show list."),
    flag(A, "HideSurfFavoriteChannels", "Hide favourite channels", "Hide the favourites list."),
    flag(A, "HideSurfFavoriteShows", "Hide favourite shows", "Hide the favourite-show list."),
    flag(A, "ScrollChannelsByPage", "Scroll channels by page", "Page through channels instead of one at a time."),
    flag(A, "ScrollShowsByPage", "Scroll shows by page", "Page through shows instead of one at a time."),
    flag(A, "RetainStop", "Retain stop", "Keep the stopped state when leaving."),
    number(A, "MaxTvContentDays", "Guide days", "How many days of programme guide to keep.", 1, 31),
};

std::string lowered(std::string_view text) {
    std::string out(text);
    for (char& c : out) {
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    return out;
}

} // namespace

std::span<const PropertyInfo> propertyCatalog() {
    return kCatalog;
}

const PropertyInfo* findProperty(PropertyScope scope, std::string_view name) {
    const auto found = std::ranges::find_if(
        kCatalog, [&](const PropertyInfo& info) { return info.scope == scope && info.name == name; });
    return found == kCatalog.end() ? nullptr : &*found;
}

std::string formatPropertyValue(PropertyScope scope, std::string_view name, std::string_view value) {
    const PropertyInfo* info = findProperty(scope, name);
    if (info == nullptr || info->type != PropertyInfo::Type::Bool) {
        return std::string(value);
    }
    const std::string text = lowered(value);
    const bool on = text == "true" || text == "1" || text == "yes";
    if (scope == PropertyScope::Activity) {
        return on ? "True" : "False";
    }
    return on ? "true" : "false";
}

} // namespace harmony
