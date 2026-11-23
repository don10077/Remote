/**
 * @file keyScans.cpp
 * @brief Данные скан-таблиц клавиш по семействам и по моделям, где семейство не отвечает.
 *
 * Таблицы переписаны из подтверждённых источников (правило 8 [../key-names.md]); в заголовке
 * каждой — источник и лицензия. Имена в таблицах — канонические имена словаря K1: алиасы
 * источников (`Tv`, `Clear`, `ScreenPrev`, `ScreenLeft1`) записаны словарной формой, а
 * коды, имени которых нет, в таблицу не попадают вовсе — для них `keyNameForScan()`
 * возвращает nullopt, и отображается пометка `scanPlaceholder()`.
 *
 * Полнота (`complete`) — решение программы: false у таблиц, подтверждённых не целиком
 * (arch 8 без модели), и у семейств, где таблиц кодов нет (2, 3, 7, 9, 10, 11, 15, 17).
 * По неполной таблице нельзя решать, что клавиши у модели нет (K4, C1).
 */

#include "harmony/core/keyScans.h"

#include "harmony/core/remoteCatalog.h"

#include <algorithm>
#include <array>

namespace harmony {

namespace {

// Arch 14 (Harmony 600, 650, 665, 700). Источник: таблица 3.1 [../key-names.md], которую
// сверили 20.11.2026 с harmony-explorations `reference/remotes/harmony-650/keys.md` (MIT) и
// afterglow `docs/harmony_650_700/gspm.md`, раздел «Pages» (GPL-3.0). Четыре клавиши у угла
// экрана и центральная названы по измерениям 650; коды 0x37…0x3F в arch 14 нет — они
// подтверждены на 885 (arch 8), а не здесь.
constexpr std::array<KeyScan, 54> kArch14 = {{
    {0x01, "WatchMovie"},     {0x02, "SoftRight1"},     {0x03, "Help"},            {0x04, "MoreActivities"},
    {0x05, "WatchTV"},        {0x06, "Off"},            {0x07, "ListenMusic"},     {0x08, "SoftLeft1"},
    {0x09, "SoftLeft2"},      {0x0A, "Menu"},           {0x0B, "PageLeft"},        {0x0C, "Exit"},
    {0x0D, "Red"},            {0x0E, "VolumeUp"},       {0x0F, "VolumeDown"},      {0x10, "VolumeMute"},
    {0x11, "Number4"},        {0x12, "Number7"},        {0x13, "NumberPlus"},      {0x14, "Number0"},
    {0x15, "SkipBack"},       {0x16, "Rewind"},         {0x17, "Record"},           {0x18, "Number1"},
    {0x19, "Devices"},        {0x1A, "UpArrow"},        {0x1B, "DownArrow"},        {0x1C, "Yellow"},
    {0x1D, "Blue"},            {0x1E, "FastForward"},    {0x1F, "ChannelUp"},        {0x20, "ChannelDown"},
    {0x21, "Guide"},           {0x22, "SoftRight2"},     {0x23, "PageRight"},        {0x24, "Info"},
    {0x25, "Number6"},         {0x26, "SkipForward"},    {0x27, "Number3"},          {0x28, "Stop"},
    {0x29, "DirectionRight"},  {0x2A, "DirectionDown"},  {0x2B, "PrevChannel"},      {0x2C, "Play"},
    {0x2D, "Number9"},         {0x2E, "Pause"},          {0x2F, "Number2"},          {0x30, "Number5"},
    {0x31, "Green"},           {0x32, "DirectionUp"},    {0x33, "Select"},           {0x34, "DirectionLeft"},
    {0x35, "Number8"},         {0x36, "NumberEnter"},
}};

// Arch 12 (Harmony One). Источник: таблица 3.2 [../key-names.md], сверена 23.11.2026 с
// harmony-explorations `reference/remotes/harmony-one/keys.md` (MIT). Имена по карте кнопок
// Logitech; `ScreenPrev`/`ScreenNext` записаны канонами `PageLeft`/`PageRight`. Кода `0x0D`
// и кодов 0x22, 0x25, 0x26 в таблице нет: keys.md их не называет.
constexpr std::array<KeyScan, 38> kArch12 = {{
    {0x01, "Number4"},        {0x02, "Exit"},           {0x03, "VolumeUp"},      {0x04, "VolumeDown"},
    {0x05, "Rewind"},         {0x06, "SkipBack"},       {0x07, "Record"},         {0x08, "Number1"},
    {0x09, "VolumeMute"},     {0x0A, "Info"},           {0x0B, "DirectionLeft"},  {0x0C, "NumberPlus"},
    {0x0E, "Number0"},        {0x0F, "Number9"},        {0x10, "Number8"},        {0x11, "Number6"},
    {0x12, "Guide"},          {0x13, "ChannelUp"},      {0x14, "ChannelDown"},    {0x15, "FastForward"},
    {0x16, "SkipForward"},    {0x17, "Stop"},           {0x18, "Number3"},         {0x19, "Number5"},
    {0x1A, "DownArrow"},      {0x1B, "DirectionUp"},    {0x1C, "Select"},          {0x1D, "DirectionDown"},
    {0x1E, "Play"},            {0x1F, "Pause"},          {0x20, "Number2"},         {0x21, "DirectionRight"},
    {0x23, "PrevChannel"},     {0x24, "UpArrow"},        {0x27, "Number7"},         {0x28, "Menu"},
    {0x2E, "PageLeft"},        {0x2F, "PageRight"},
}};

// Arch 8 — таблица по модели 885. Источник: таблица 3.3 [../key-names.md], сверена 23.11.2026
// с harmony-explorations `reference/remotes/harmony-885/keys.md` (MIT): 55 кода прочитаны из
// памяти пульта при нажатии каждой клавиши. Алиасы источника (`AllOff`, `Device`, `Clear`,
// `Enter`, `ScreenPrev`, `ScreenNext`, `ScreenLeft1…4`, `ScreenRight1…4`) записаны канонами.
constexpr std::array<KeyScan, 55> kArch885 = {{
    {0x01, "Activities"},     {0x02, "Off"},            {0x03, "Help"},           {0x05, "SoftLeft1"},
    {0x06, "SoftLeft2"},      {0x07, "SoftLeft3"},      {0x08, "SoftLeft4"},       {0x0A, "Devices"},
    {0x0B, "VolumeMute"},     {0x0C, "PageLeft"},       {0x0D, "VolumeUp"},        {0x0E, "DirectionLeft"},
    {0x0F, "VolumeDown"},     {0x10, "DirectionUp"},    {0x11, "Menu"},            {0x12, "Exit"},
    {0x13, "Green"},           {0x14, "Red"},            {0x15, "Stop"},            {0x16, "Record"},
    {0x17, "Rewind"},          {0x18, "SkipBack"},       {0x19, "Number1"},         {0x1A, "Number4"},
    {0x1B, "Number8"},         {0x1D, "Number7"},        {0x1F, "NumberPlus"},      {0x20, "Number0"},
    {0x21, "Select"},          {0x22, "DirectionDown"},  {0x23, "DirectionRight"},  {0x24, "ChannelDown"},
    {0x25, "ChannelUp"},       {0x26, "Media"},          {0x28, "PrevChannel"},     {0x29, "Glow"},
    {0x2B, "PageRight"},       {0x2C, "SoftRight4"},     {0x2D, "SoftRight1"},      {0x2E, "SoftRight2"},
    {0x30, "SoftRight3"},      {0x32, "Guide"},          {0x33, "Info"},            {0x34, "Blue"},
    {0x35, "SkipForward"},     {0x36, "FastForward"},    {0x37, "Pause"},           {0x38, "Play"},
    {0x39, "Number2"},         {0x3A, "Number3"},        {0x3B, "Number6"},         {0x3C, "Yellow"},
    {0x3D, "Number5"},         {0x3E, "Number9"},        {0x3F, "NumberEnter"},
}};

// Arch 8 — таблица семейства для моделей, у которых своей таблицы нет (880, 720, 785).
// Источник: harmony-explorations `reference/remotes/harmony-885/keys.md` (MIT), раздел
// «Harmony 880, skin 15» — 29 кода, названные по конфигурациям двух владельцев, и каждый
// назван тем же именем, что поднимает клавишу на 885. Поэтому это пересечение 885 и 880:
// только коды, подтверждённые в обоих, и `complete == false` — 880 не измеряли нажатием.
constexpr std::array<KeyScan, 29> kArch8 = {{
    {0x0B, "VolumeMute"},     {0x0D, "VolumeUp"},       {0x0E, "DirectionLeft"},  {0x0F, "VolumeDown"},
    {0x10, "DirectionUp"},    {0x11, "Menu"},           {0x12, "Exit"},            {0x15, "Stop"},
    {0x16, "Record"},          {0x17, "Rewind"},         {0x19, "Number1"},         {0x1A, "Number4"},
    {0x1B, "Number8"},         {0x1D, "Number7"},        {0x20, "Number0"},         {0x21, "Select"},
    {0x22, "DirectionDown"},  {0x23, "DirectionRight"}, {0x24, "ChannelDown"},     {0x25, "ChannelUp"},
    {0x33, "Info"},            {0x36, "FastForward"},    {0x37, "Pause"},           {0x38, "Play"},
    {0x39, "Number2"},         {0x3A, "Number3"},        {0x3B, "Number6"},         {0x3D, "Number5"},
    {0x3E, "Number9"},
}};

// Arch 16 (Harmony 300 и 350) — таблица по модели: у 300 и 350 на одних и тех же кодах
// разные device-клавиши. Источник: harmony-explorations
// `reference/remotes/harmony-300/keys.md` (MIT, печать skin 79, 49 кодов) и
// `.../harmony-350/keys.md` (MIT, 53 кода). Имена — по надписям на корпусе (слабейший
// подтверждённый источник, словарь K1 помечает их `Printed`); `Tv` записан канонами `TV`,
// `Clear` — `NumberPlus`, `Enter` — `NumberEnter`. У 200 своей таблицы нет.
constexpr std::array<KeyScan, 49> kArch16_300 = {{
    {0x01, "CableSat"},       {0x02, "Favorite3"},      {0x03, "Power"},           {0x04, "VcrAux"},
    {0x05, "TV"},              {0x06, "WatchTV"},        {0x07, "Dvd"},             {0x08, "Favorite1"},
    {0x09, "Favorite2"},       {0x0A, "Menu"},           {0x0B, "Teletext"},        {0x0C, "Exit"},
    {0x0D, "Red"},             {0x0E, "DirectionLeft"},  {0x0F, "VolumeMute"},      {0x10, "List"},
    {0x11, "Number4"},         {0x12, "Number7"},        {0x14, "Number0"},         {0x15, "SkipBack"},
    {0x16, "Rewind"},          {0x17, "Record"},          {0x18, "Number1"},         {0x1B, "Green"},
    {0x1C, "Yellow"},          {0x1D, "Blue"},            {0x1E, "FastForward"},     {0x1F, "DirectionRight"},
    {0x20, "PrevChannel"},     {0x21, "Guide"},           {0x22, "Favorite4"},        {0x24, "Info"},
    {0x25, "Number6"},         {0x26, "SkipForward"},     {0x27, "Number3"},          {0x28, "Stop"},
    {0x29, "ChannelDown"},     {0x2A, "DirectionDown"},   {0x2C, "Play"},             {0x2D, "Number9"},
    {0x2E, "Pause"},            {0x2F, "Number2"},         {0x30, "Number5"},          {0x31, "VolumeUp"},
    {0x32, "Select"},           {0x33, "DirectionUp"},     {0x34, "VolumeDown"},       {0x35, "Number8"},
    {0x37, "ChannelUp"},
}};

constexpr std::array<KeyScan, 53> kArch16_350 = {{
    {0x01, "CablePc"},        {0x02, "Favorite5"},      {0x03, "Power"},           {0x04, "DvdMusic"},
    {0x05, "TvMedia"},         {0x06, "WatchTV"},        {0x07, "AvrGame"},         {0x08, "Favorite1"},
    {0x09, "Favorite2"},       {0x0A, "Menu"},           {0x0B, "Favorite3"},        {0x0C, "Exit"},
    {0x0D, "Red"},             {0x0E, "DirectionLeft"},  {0x0F, "VolumeMute"},      {0x11, "Number4"},
    {0x12, "Number7"},         {0x13, "NumberPlus"},     {0x14, "Number0"},          {0x15, "SkipBack"},
    {0x16, "Rewind"},          {0x17, "Record"},          {0x18, "Number1"},         {0x19, "UpArrow"},
    {0x1A, "DownArrow"},       {0x1B, "Green"},           {0x1C, "Yellow"},           {0x1D, "Blue"},
    {0x1E, "FastForward"},     {0x1F, "DirectionRight"},  {0x20, "PrevChannel"},      {0x21, "Guide"},
    {0x22, "Favorite4"},        {0x23, "Input"},          {0x24, "Info"},             {0x25, "Number6"},
    {0x26, "SkipForward"},     {0x27, "Number3"},         {0x28, "Stop"},             {0x29, "ChannelDown"},
    {0x2A, "DirectionDown"},   {0x2C, "Play"},            {0x2D, "Number9"},          {0x2E, "Pause"},
    {0x2F, "Number2"},          {0x30, "Number5"},         {0x31, "VolumeUp"},         {0x32, "Select"},
    {0x33, "DirectionUp"},      {0x34, "VolumeDown"},      {0x35, "Number8"},          {0x36, "NumberEnter"},
    {0x37, "ChannelUp"},
}};

// Arch 18 (Harmony 800). Источник: afterglow
// `src/afterglow/backends/harmony_800/keys.py` (GPL-3.0) — 35 кода, названные tester's 800
// на клавишном тесте 2026-10-15. Цифровых клавиш у 800 нет: `Numbers` открывает цифровую
// панель на экране, поэтому в словаре это служебное имя, а не цифра.
constexpr std::array<KeyScan, 35> kArch18 = {{
    {0x01, "Back"},            {0x02, "Record"},         {0x03, "SkipBack"},        {0x04, "Rewind"},
    {0x05, "Exit"},            {0x06, "Guide"},          {0x07, "VolumeMute"},      {0x08, "Home"},
    {0x09, "Pause"},           {0x0A, "Play"},           {0x0B, "DownArrow"},        {0x0C, "UpArrow"},
    {0x0D, "DirectionDown"},   {0x0E, "Select"},         {0x0F, "PIP"},              {0x10, "Stop"},
    {0x11, "SkipForward"},     {0x12, "FastForward"},    {0x13, "Menu"},             {0x14, "Info"},
    {0x15, "PrevChannel"},     {0x16, "TV"},             {0x17, "Activities"},       {0x18, "VolumeUp"},
    {0x19, "Off"},             {0x1A, "VolumeDown"},     {0x1B, "Numbers"},           {0x1C, "DirectionLeft"},
    {0x1D, "ChannelDown"},     {0x1E, "DVR"},            {0x1F, "ChannelUp"},         {0x20, "Help"},
    {0x21, "DirectionUp"},     {0x22, "Mouse"},           {0x23, "DirectionRight"},
}};

/// Таблицы по возрастанию архитектуры; внутри семейства — сначала таблица модели.
constexpr std::array<KeyScanTable, 7> kTables = {{
    {8, "885", true, kArch885},
    {8, "", false, kArch8},
    {12, "", true, kArch12},
    {14, "", true, kArch14},
    {16, "300", true, kArch16_300},
    {16, "350", true, kArch16_350},
    {18, "800", true, kArch18},
}};

/// Id модели для сравнения: нижний регистр (`normalizedModelId`) и без суффикса `i`.
std::string normalizedScanModelId(std::string_view modelId) {
    std::string id = normalizedModelId(modelId);
    if (id.size() > 1 && id.ends_with('i'))
        id.pop_back();
    return id;
}

} // namespace

std::span<const KeyScanTable> keyScanTables() {
    return kTables;
}

const KeyScanTable* keyScanTable(int architecture, std::string_view modelId) {
    // Сначала таблица модели, затем таблица семейства.
    const std::string id = normalizedScanModelId(modelId);
    if (!id.empty()) {
        for (const auto& table : kTables)
            if (table.architecture == architecture && normalizedScanModelId(table.model) == id)
                return &table;
    }
    for (const auto& table : kTables)
        if (table.architecture == architecture && table.model.empty())
            return &table;
    return nullptr;
}

std::optional<std::string_view> keyNameForScan(int architecture, std::string_view modelId, unsigned scan) {
    if (scan < 1u || scan > 0x3Fu)
        return std::nullopt;
    const auto* table = keyScanTable(architecture, modelId);
    if (table == nullptr)
        return std::nullopt;
    const auto it = std::ranges::lower_bound(table->entries, scan, {},
                                             [](const KeyScan& entry) { return entry.scan; });
    if (it == table->entries.end() || it->scan != scan)
        return std::nullopt;
    return it->name;
}

std::string scanPlaceholder(unsigned scan) {
    static constexpr char kHex[] = "0123456789ABCDEF";
    const unsigned digits = scan > 0xFFu ? 4u : 2u;   // минимум две цифры — формат ScanNN
    std::string result = "Scan";
    for (int shift = static_cast<int>(digits) - 1; shift >= 0; --shift)
        result += kHex[(scan >> (4 * shift)) & 0xFu];
    return result;
}

bool keyScanTableComplete(int architecture, std::string_view modelId) {
    const auto* table = keyScanTable(architecture, modelId);
    return table != nullptr && table->complete;
}

} // namespace harmony
