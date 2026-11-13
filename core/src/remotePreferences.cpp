/**
 * @file remotePreferences.cpp
 * @brief Список настроек пульта (platformconfig/system_*.dat) и проверка значений.
 */

#include "harmony/core/remotePreferences.h"

#include <algorithm>
#include <array>
#include <charconv>

namespace harmony {

namespace {

constexpr std::array<PreferenceChoice, 2> kOnOff = {{{"Off", "0"}, {"On", "1"}}};
constexpr std::array<PreferenceChoice, 2> kTimeFormat = {{{"24-hour", "Military"}, {"12-hour", "Civilian"}}};
// Значения — как пишет GeneralOptions.done_theme(); порядок в меню пульта другой.
constexpr std::array<PreferenceChoice, 4> kTheme = {
    {{"Default", "0"}, {"Diode", "1"}, {"Polymer", "2"}, {"Tron", "3"}}};

/// Настройка-выбор.
constexpr RemotePreference choice(std::string_view key, std::string_view file, std::string_view label,
                                  std::span<const PreferenceChoice> choices, std::string_view defaultValue,
                                  std::string_view xmlProperty = {}) {
    RemotePreference preference;
    preference.key = key;
    preference.file = file;
    preference.label = label;
    preference.choices = choices;
    preference.defaultValue = defaultValue;
    preference.xmlProperty = xmlProperty;
    return preference;
}

/// Настройка-число в границах.
constexpr RemotePreference number(std::string_view key, std::string_view file, std::string_view label, int min,
                                  int max, std::string_view defaultValue) {
    RemotePreference preference;
    preference.key = key;
    preference.file = file;
    preference.label = label;
    preference.min = min;
    preference.max = max;
    preference.defaultValue = defaultValue;
    return preference;
}

constexpr std::array<RemotePreference, 7> kPreferences = {
    choice("sound", "system_sound.dat", "Key beep", kOnOff, "0"),
    // Яркость — общий уровень 0–100; меню пульта пишет 33/66/100, но принимается любое значение.
    number("backlight_level", "system_backlightlevel.dat", "Backlight brightness, %", 0, 100, "60"),
    // Пульт читает своё поле как первые две цифры: 120 превратилось бы в 12.
    number("backlight_timeout", "system_backlighttimeout.dat", "Backlight timeout, s", 1, 99, "10"),
    choice("large_font", "system_uselargefont.dat", "Large font", kOnOff, "0"),
    choice("theme", "system_theme.dat", "Screen theme", kTheme, "0"),
    choice("time_format", "system_timeformat.dat", "Clock", kTimeFormat, "Military", "TimeDisplayFormat"),
    choice("child_lock", "system_childlock.dat", "Child lock", kOnOff, "0"),
};

} // namespace

std::string RemotePreference::endpoint() const {
    // «system_backlightlevel.dat» → «/system/backlightlevel».
    std::string_view name = file;
    name.remove_prefix(std::string_view("system_").size());
    name.remove_suffix(std::string_view(".dat").size());
    return "/system/" + std::string(name);
}

std::string RemotePreference::archivePath() const {
    return "platformconfig/" + std::string(file);
}

std::span<const RemotePreference> remotePreferences() {
    return kPreferences;
}

const RemotePreference* findRemotePreference(std::string_view key) {
    const auto found = std::ranges::find(kPreferences, key, &RemotePreference::key);
    return found == kPreferences.end() ? nullptr : &*found;
}

Result<void> checkPreferenceValue(const RemotePreference& preference, std::string_view value) {
    if (!preference.choices.empty()) {
        if (std::ranges::find(preference.choices, value, &PreferenceChoice::value) == preference.choices.end()) {
            return makeError(ErrorCode::Format, "invalid value “%1” for the remote setting %2",
                             {std::string(value), std::string(preference.key)});
        }
        return {};
    }
    int parsed = 0;
    const auto [end, ec] = std::from_chars(value.data(), value.data() + value.size(), parsed);
    if (ec != std::errc{} || end != value.data() + value.size() || parsed < preference.min ||
        parsed > preference.max) {
        return makeError(ErrorCode::Format, "the remote setting %1 must be a number from %2 to %3",
                         {std::string(preference.key), std::to_string(preference.min),
                          std::to_string(preference.max)});
    }
    return {};
}

} // namespace harmony
