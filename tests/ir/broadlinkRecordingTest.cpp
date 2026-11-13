/**
 * @file broadlinkRecordingTest.cpp
 * @brief Сверка с настоящими сигналами, записанными Broadlink RM4 (tools/broadlinkLearn.py).
 *
 * Записи пульта Harmony (в JSON есть "code"): carrierVm по тому же коду и протоколу
 * из IrProto.bin должен дать тот же сигнал. Записи родных пультов устройств ("remote"):
 * распознавание узнаёт протокол, а encode() по узнанным адресу и команде собирает
 * тот же сигнал.
 *
 * RM4 не сообщает несущую, меряет шагом ≈33 мкс и даёт длительности примерно на 5 %
 * длиннее настоящих (так у всех записей, и у Harmony, и у пульта Sony). Поэтому
 * сравниваются только длительности первого кадра и с допуском (1/6, не меньше 200 мкс):
 * этого хватает на погрешность RM4, а ноль и единицу у всех протоколов различает
 * не меньше чем вдвое.
 */

#include "harmony/core/formats/irTables.h"
#include "harmony/core/ir/carrierVm.h"
#include "harmony/core/ir/irEncode.h"
#include "harmony/core/ir/irSignal.h"
#include "harmony/core/ir/nativeProtocol.h"

#include <gtest/gtest.h>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <map>
#include <string>
#include <tuple>
#include <vector>

using namespace harmony;

namespace {

const std::filesystem::path kData(HARMONY_TEST_DATA_DIR);
const std::filesystem::path kRecordings = kData / "broadlink";
const std::filesystem::path kHarmonyBench = std::filesystem::path(HARMONY_SOURCE_DIR) / "docs" / "harmony-test-captures";

/// Пауза длиннее этой — конец кадра (самая длинная пауза внутри кадра — 4,5 мс у NEC).
constexpr int kFrameGapUs = 5200;

struct Recording {
    std::string file;
    nlohmann::json data;
    ir::IrSignal signal;
};

formats::Bytes readFile(const std::filesystem::path& path) {
    std::ifstream file(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
}

/// Записи из tests/data/broadlink: с пульта Harmony (fromHarmony) или с родных пультов.
std::vector<Recording> recordings(bool fromHarmony) {
    std::vector<Recording> out;
    if (!std::filesystem::exists(kRecordings)) {
        return out;
    }
    for (const auto& entry : std::filesystem::directory_iterator(kRecordings)) {
        if (entry.path().extension() != ".json") {
            continue;
        }
        std::ifstream file(entry.path());
        auto data = nlohmann::json::parse(file);
        if (data.contains("code") != fromHarmony) {
            continue;
        }
        Recording recording{entry.path().filename().string(), std::move(data), {}};
        recording.signal.durations = recording.data.at("durations").get<std::vector<int>>();
        out.push_back(std::move(recording));
    }
    std::ranges::sort(out, {}, &Recording::file);
    return out;
}

/// Первый кадр: без пауз перед ним и без паузы после него.
std::vector<int> firstFrame(const std::vector<int>& durations) {
    const auto begin = std::ranges::find_if(durations, [](int d) { return d > 0; });
    const auto end = std::find_if(begin, durations.end(), [](int d) { return d < -kFrameGapUs; });
    return {begin, end};
}

void expectSameTiming(const std::vector<int>& recorded, const std::vector<int>& expected) {
    const auto got = firstFrame(recorded);
    const auto want = firstFrame(expected);
    ASSERT_EQ(got.size(), want.size()) << "число длительностей в первом кадре";
    for (std::size_t i = 0; i < got.size(); ++i) {
        // Приёмник RM4 иногда сдвигает фронт на 2 шага: импульс короче на ~65 мкс,
        // следующая пауза на столько же длиннее (у Denon 394/−1445 вместо 460/−1346).
        const int tolerance = std::max(200, std::abs(want[i]) / 6);
        EXPECT_NEAR(got[i], want[i], tolerance) << "длительность №" << i;
    }
}

} // namespace

TEST(BroadlinkRecording, HarmonySignalsMatchCarrierVm) {
    const auto all = recordings(true);
    if (all.empty()) {
        GTEST_SKIP() << "нет записей Harmony в " << kRecordings.string();
    }
    auto table = formats::parseIrProto(readFile(kData / "userconfig" / "IrProto.bin"));
    ASSERT_TRUE(table) << table.error().message;
    for (const auto& recording : all) {
        SCOPED_TRACE(recording.file);
        const auto protocol = static_cast<std::size_t>(std::stoul(recording.data.at("protocol").get<std::string>()));
        ASSERT_LT(protocol, table->entries.size());
        auto code = ir::codeBytes(recording.data.at("code").get<std::string>());
        ASSERT_TRUE(code);
        auto run = ir::runCarrier(table->entries[protocol], *code);
        ASSERT_TRUE(run) << run.error().message;

        const auto recorded = ir::decode(recording.signal);
        const auto computed = ir::decode(run->signal);
        ASSERT_TRUE(recorded) << "запись не распознана: " << ir::timingText(recording.signal);
        ASSERT_TRUE(computed) << "сигнал carrierVm не распознан: " << ir::timingText(run->signal);
        EXPECT_EQ(recorded->protocol, computed->protocol);
        EXPECT_EQ(recorded->address, computed->address);
        EXPECT_EQ(recorded->command, computed->command);
        expectSameTiming(recording.signal.durations, run->signal.durations);
    }
}

TEST(BroadlinkRecording, NativeRemotesDecodeAndEncodeBack) {
    const auto all = recordings(false);
    if (all.empty()) {
        GTEST_SKIP() << "нет записей родных пультов в " << kRecordings.string();
    }
    // Пульт Sony RMT-D177P (DVD): SIRC 20, устройство 26, расширение 73 → адрес 26 + 73·32.
    const std::map<std::string, std::tuple<std::string, std::uint32_t, std::uint32_t>> known = {
        {"sony_dvd-1.json", {"Sony SIRC 20", 2362, 0}},
        {"sony_dvd-Power.json", {"Sony SIRC 20", 2362, 21}},
        {"sony_dvd-Play.json", {"Sony SIRC 20", 2362, 50}},
        {"sony_dvd-Stop.json", {"Sony SIRC 20", 2362, 56}},
    };
    for (const auto& recording : all) {
        SCOPED_TRACE(recording.file);
        const auto decoded = ir::decode(recording.signal);
        ASSERT_TRUE(decoded) << "запись не распознана: " << ir::timingText(recording.signal);
        if (const auto it = known.find(recording.file); it != known.end()) {
            const auto& [protocol, address, command] = it->second;
            EXPECT_EQ(decoded->protocol, protocol);
            EXPECT_EQ(decoded->address, address);
            EXPECT_EQ(decoded->command, command);
            // Запись с удержанием (несколько кадров) — целиком та же команда: обучение сохранит её протоколом.
            const auto whole = ir::nativeProtocolOf(recording.signal);
            ASSERT_TRUE(whole);
            EXPECT_EQ(whole->command, command);
        }
        auto built = ir::encode(decoded->protocol, decoded->address, decoded->command);
        ASSERT_TRUE(built) << built.error().message;
        expectSameTiming(recording.signal.durations, built->durations);
    }
}

TEST(BroadlinkRecording, HarmonyTestDeviceCommandsMatchProgramEncoder) {
    struct Case {
        const char* name;
        const char* protocol;
        std::uint32_t address;
        std::uint32_t command;
    };
    constexpr Case cases[] = {
        {"NEC", "NEC", 0x04, 0x08}, {"NECext", "NEC (extended)", 0x1E7, 0x45},
        {"Samsung32", "Samsung32", 0x0707, 0x02}, {"SIRC12", "Sony SIRC 12", 0x01, 0x15},
        {"SIRC15", "Sony SIRC 15", 0x97, 0x2A}, {"SIRC20", "Sony SIRC 20", 0x1A3, 0x3C},
        {"RC5", "RC5", 0x05, 0x35}, {"RC5X", "RC5X", 0x10, 0x4C},
        {"RC6", "RC6", 0xA5, 0x5A}, {"Panasonic", "Panasonic", 0x80, 0x3D},
        {"LG", "LG", 0x04, 0x10EF}, {"JVC", "JVC", 0x03, 0x17},
        {"Onkyo", "Onkyo", 0xD2D2, 0x1234}, {"Denon", "Denon", 0x02, 0xE1},
        {"Sharp", "Sharp", 0x01, 0x16}, {"RCA", "RCA", 0x0F, 0x54},
    };
    for (const Case& c : cases) {
        SCOPED_TRACE(c.name);
        std::ifstream file(kHarmonyBench / (std::string("44791927-") + c.name + ".json"));
        ASSERT_TRUE(file);
        const auto data = nlohmann::json::parse(file);
        ir::IrSignal captured;
        captured.durations = data.at("durations").get<std::vector<int>>();
        const auto built = ir::encode(c.protocol, c.address, c.command);
        ASSERT_TRUE(built);
        captured.frequency = built->frequency; // RM4 не измеряет несущую
        const auto decoded = ir::decode(captured);
        ASSERT_TRUE(decoded);
        EXPECT_EQ(decoded->protocol, c.protocol);
        EXPECT_EQ(decoded->address, c.address);
        EXPECT_EQ(decoded->command, c.command);
        EXPECT_TRUE(ir::sameSignal(captured, *built));
        if (ir::hasNativeProtocol(c.protocol)) {
            const auto native = ir::nativeProtocolOf(captured);
            ASSERT_TRUE(native) << "обученная команда должна сохраняться протоколом";
            EXPECT_EQ(native->protocol, c.protocol);
            EXPECT_EQ(native->address, c.address);
            EXPECT_EQ(native->command, c.command);
        }
        if (std::string_view(c.protocol) != "RC5" && std::string_view(c.protocol) != "RC5X") {
            expectSameTiming(captured.durations, built->durations);
        }
    }
}

TEST(BroadlinkRecording, HarmonyRc5AndRc6TogglePreservesCommandIdentity) {
    for (const char* name : {"RC5", "RC6"}) {
        SCOPED_TRACE(name);
        auto capture = [name](const char* suffix) {
            std::ifstream file(kHarmonyBench / (std::string(name) + "-" + suffix + ".json"));
            ir::IrSignal signal;
            signal.durations = nlohmann::json::parse(file).at("durations").get<std::vector<int>>();
            signal.frequency = 36000; // RM4 не измеряет несущую
            return signal;
        };
        const auto first = capture("first");
        const auto second = capture("second");
        EXPECT_NE(first.durations, second.durations);
        EXPECT_TRUE(ir::sameSignal(first, second));
        const auto a = ir::nativeProtocolOf(first);
        const auto b = ir::nativeProtocolOf(second);
        ASSERT_TRUE(a && b);
        EXPECT_EQ(a->protocol, b->protocol);
        EXPECT_EQ(a->address, b->address);
        EXPECT_EQ(a->command, b->command);
    }
}
