/**
 * @file keyNames.cpp
 * @brief Общий список имён физических клавиш Harmony (скан-код → имя), 0x01…0x3F.
 *
 * Имена подтверждены своими данными: конфигурации 650 (архитектура 14) и XML формата 900.
 * Порядок — по возрастанию кода; коды без подтверждения получают UnknownKey<код> (две
 * шестнадцатеричные цифры), поэтому имя остаётся уникальным идентификатором зоны.
 */

#include "harmony/core/keyNames.h"

#include <algorithm>
#include <array>

namespace harmony {

namespace {

constexpr unsigned kFirstScan = 1;
constexpr unsigned kLastScan = 0x3F;

constexpr auto kNames = std::to_array<KeyName>({
    {0x01, "WatchMovie"},    {0x02, "UnknownKey02"},  {0x03, "Help"},            {0x04, "MoreActivities"},
    {0x05, "WatchTV"},       {0x06, "Off"},           {0x07, "ListenMusic"},     {0x08, "UnknownKey08"},
    {0x09, "UnknownKey09"},  {0x0A, "Menu"},          {0x0B, "PageLeft"},         {0x0C, "Exit"},
    {0x0D, "Red"},           {0x0E, "VolumeUp"},      {0x0F, "VolumeDown"},       {0x10, "VolumeMute"},
    {0x11, "Number4"},       {0x12, "Number7"},       {0x13, "NumberPlus"},       {0x14, "Number0"},
    {0x15, "SkipBack"},      {0x16, "Rewind"},        {0x17, "Record"},            {0x18, "Number1"},
    {0x19, "Devices"},       {0x1A, "UpArrow"},       {0x1B, "DownArrow"},         {0x1C, "Yellow"},
    {0x1D, "Blue"},           {0x1E, "FastForward"},   {0x1F, "ChannelUp"},         {0x20, "ChannelDown"},
    {0x21, "Guide"},          {0x22, "UnknownKey22"},  {0x23, "PageRight"},         {0x24, "Info"},
    {0x25, "Number6"},        {0x26, "SkipForward"},   {0x27, "Number3"},           {0x28, "Stop"},
    {0x29, "DirectionRight"}, {0x2A, "DirectionDown"}, {0x2B, "PrevChannel"},       {0x2C, "Play"},
    {0x2D, "Number9"},        {0x2E, "Pause"},         {0x2F, "Number2"},           {0x30, "Number5"},
    {0x31, "Green"},          {0x32, "DirectionUp"},   {0x33, "Select"},            {0x34, "DirectionLeft"},
    {0x35, "Number8"},        {0x36, "NumberEnter"},   {0x37, "UnknownKey37"},      {0x38, "UnknownKey38"},
    {0x39, "UnknownKey39"},   {0x3A, "UnknownKey3A"},  {0x3B, "UnknownKey3B"},      {0x3C, "UnknownKey3C"},
    {0x3D, "UnknownKey3D"},   {0x3E, "UnknownKey3E"},  {0x3F, "UnknownKey3F"},
});

} // namespace

std::span<const KeyName> keyNames() {
    return kNames;
}

std::optional<std::string_view> keyName(unsigned scan) {
    if (scan < kFirstScan || scan > kLastScan)
        return std::nullopt;
    return kNames[scan - kFirstScan].name;
}

std::string keyLabel(unsigned scan) {
    const auto name = keyName(scan);
    return name ? std::string(*name) : "Scan" + std::to_string(scan);
}

bool isUnknownKeyName(std::string_view name) {
    return name.starts_with(kUnknownKeyPrefix);
}

bool keyNameConfirmed(unsigned scan) {
    const auto name = keyName(scan);
    return name && !isUnknownKeyName(*name);
}

bool isKnownKeyName(std::string_view name) {
    return std::ranges::find_if(kNames, [name](const KeyName& entry) { return entry.name == name; }) != kNames.end();
}

} // namespace harmony
