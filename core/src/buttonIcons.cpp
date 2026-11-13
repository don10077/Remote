/**
 * @file buttonIcons.cpp
 * @brief Имена значков экранных кнопок Harmony 900 (по afterglow icons/buttons).
 */

#include "harmony/core/buttonIcons.h"

#include <algorithm>
#include <array>

namespace harmony {

namespace {

constexpr auto kIcons = std::to_array<std::string_view>({
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
});

} // namespace

std::span<const std::string_view> buttonIcons() {
    return kIcons;
}

bool isKnownButtonIcon(std::string_view name) {
    return std::ranges::find(kIcons, name) != kIcons.end();
}

} // namespace harmony
