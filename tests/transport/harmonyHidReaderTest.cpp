#include "harmony/transport/harmonyHidReader.h"
#include <gtest/gtest.h>
#include <deque>

using namespace harmony;
using namespace harmony::transport;

namespace {
/// Имитирует classic HID: ответы не зависят от системной USB-библиотеки.
class FakeHid final : public HidChannel {
public:
    std::vector<std::uint8_t> identity{0x28, 0x12, 0x10, 0x1c, 0x15, 0xe0, 75, 0};
    std::vector<std::uint8_t> config = std::vector<std::uint8_t>(1100, 0x5a);
    std::vector<std::array<std::uint8_t, 64>> requests;
    std::deque<std::vector<std::uint8_t>> replies;
    bool badSequence = false;
    bool shortRead = false;
    bool oversizedPacket = false;
    std::size_t base = 0x030000;

    FakeHid() {
        config[0] = 0x47; config[1] = 0x53; config[2] = 0x50; config[3] = 0x4d;
        setBase(base);
    }
    /// Задаёт карту памяти эмулятора независимо от реализации читателя.
    void setBase(std::size_t address) {
        base = address;
        const auto end = base + config.size() - 4;
        config[4] = static_cast<std::uint8_t>(end);
        config[5] = static_cast<std::uint8_t>(end >> 8);
        config[6] = static_cast<std::uint8_t>(end >> 16);
    }
    Result<void> send(const std::array<std::uint8_t, 64>& request) override {
        requests.push_back(request);
        if (request[0] == 0x10) { replies.push_back(identity); return {}; }
        if (request[0] != 0x55) return makeError(ErrorCode::Remote, "Unexpected write command");
        const std::size_t address = (static_cast<std::size_t>(request[1]) << 16) |
            (static_cast<std::size_t>(request[2]) << 8) | request[3];
        std::size_t count = (static_cast<std::size_t>(request[4]) << 8) | request[5];
        if (address < base) return makeError(ErrorCode::Remote, "Read below configuration");
        std::size_t offset = address - base;
        if (offset + count > config.size()) return makeError(ErrorCode::Remote, "Out of range");
        std::uint8_t seq = badSequence ? 2 : 1;
        while (count > 0 && !shortRead) {
            const std::size_t n = count >= 62 ? 62 : (count >= 30 ? 30 : (count >= 14 ? 14 : (count >= 6 ? 6 : count)));
            const auto code = static_cast<std::uint8_t>(n == 62 ? 10 : n == 30 ? 9 : n == 14 ? 8 : n + 1);
            std::vector<std::uint8_t> packet{static_cast<std::uint8_t>(0x60 | code), seq};
            packet.insert(packet.end(), config.begin() + static_cast<std::ptrdiff_t>(offset), config.begin() + static_cast<std::ptrdiff_t>(offset + n));
            if (oversizedPacket) packet[0] = 0x6a;
            replies.push_back(std::move(packet));
            offset += n; count -= n; seq = static_cast<std::uint8_t>(seq + 0x11);
        }
        replies.push_back({0xf1, 0x50});
        return {};
    }
    Result<std::vector<std::uint8_t>> receive() override {
        if (replies.empty()) return makeError(ErrorCode::Io, "Timeout");
        auto packet = std::move(replies.front()); replies.pop_front(); return packet;
    }
};
}

TEST(HarmonyHidReader, Reads665WithoutAnyProgrammingCommands) {
    FakeHid channel;
    HarmonyHidReader reader(channel);
    const auto result = reader.read();
    ASSERT_TRUE(result) << result.error().message;
    EXPECT_EQ(result->info.model, "Harmony 665");
    EXPECT_EQ(result->info.identity.architecture, 14);
    EXPECT_EQ(result->configBase, 0x030000u);
    EXPECT_EQ(result->config, channel.config);
    EXPECT_EQ(result->identityReport, channel.identity);
    for (const auto& request : channel.requests)
        EXPECT_TRUE(request[0] == 0x10 || request[0] == 0x55);
}

TEST(HarmonyHidReader, ReadsOneAtItsOwnConfigurationAddress) {
    FakeHid channel;
    channel.identity = {0x28, 0x05, 0x05, 0xc8, 0x1f, 0xc0, 54, 0};
    channel.setBase(0x040000);
    HarmonyHidReader reader(channel);
    const auto result = reader.read();
    ASSERT_TRUE(result) << result.error().message;
    EXPECT_EQ(result->info.model, "Harmony One");
    EXPECT_EQ(result->configBase, 0x040000u);
    EXPECT_EQ(result->config, channel.config);
    for (const auto& request : channel.requests)
        EXPECT_TRUE(request[0] == 0x10 || request[0] == 0x55);
}

TEST(HarmonyHidReader, RejectsSkinFromAnotherArchitectureBeforeReadingFlash) {
    for (const auto& pair : {std::pair{12, 75}, std::pair{14, 54}, std::pair{11, 54}}) {
        FakeHid channel;
        channel.identity[5] = static_cast<std::uint8_t>(pair.first << 4);
        channel.identity[6] = static_cast<std::uint8_t>(pair.second);
        HarmonyHidReader reader(channel);
        EXPECT_FALSE(reader.read());
        EXPECT_EQ(channel.requests.size(), 1);
    }
}

TEST(HarmonyHidReader, RejectsUnknownArchitectureBeforeFlashRead) {
    FakeHid channel;
    channel.identity[5] = 0xf0;
    HarmonyHidReader reader(channel);
    EXPECT_FALSE(reader.read());
    EXPECT_EQ(channel.requests.size(), 1);
}

TEST(HarmonyHidReader, RecognizesOtherSupportedSkins) {
    for (const auto skin : {66, 69, 71, 72, 73, 74}) {
        FakeHid channel;
        channel.identity[6] = static_cast<std::uint8_t>(skin);
        HarmonyHidReader reader(channel);
        const auto result = reader.read();
        ASSERT_TRUE(result) << result.error().message;
        EXPECT_EQ(result->info.identity.skin, skin);
        EXPECT_EQ(result->config, channel.config);
    }
}

TEST(HarmonyHidReader, RejectsUnknownFlashBeforeFlashRead) {
    FakeHid channel;
    channel.identity[3] = 0;
    HarmonyHidReader reader(channel);
    EXPECT_FALSE(reader.read());
    EXPECT_EQ(channel.requests.size(), 1);
}

TEST(HarmonyHidReader, RejectsLostAndTruncatedPackets) {
    for (int mode = 0; mode < 3; ++mode) {
        FakeHid channel;
        channel.badSequence = mode == 0;
        channel.shortRead = mode == 1;
        channel.oversizedPacket = mode == 2;
        HarmonyHidReader reader(channel);
        EXPECT_FALSE(reader.read()) << mode;
    }
}

TEST(HarmonyHidReader, RejectsInvalidSignatureAndSize) {
    for (int mode = 0; mode < 2; ++mode) {
        FakeHid channel;
        if (mode == 0) channel.config[0] = 0;
        else channel.config[6] = 0xff;
        HarmonyHidReader reader(channel);
        EXPECT_FALSE(reader.read());
    }
}

TEST(HarmonyHidReader, CancelsBeforeSendingAnything) {
    FakeHid channel;
    HarmonyHidReader reader(channel);
    const auto result = reader.read([](Stage, std::size_t, std::size_t) { return false; });
    ASSERT_FALSE(result);
    EXPECT_EQ(result.error().code, ErrorCode::Cancelled);
    EXPECT_TRUE(channel.requests.empty());
}

TEST(HarmonyHidReader, CancelsDuringTransferWithoutReturningPartialBackup) {
    FakeHid channel;
    HarmonyHidReader reader(channel);
    const auto result = reader.read([](Stage, std::size_t done, std::size_t) { return done < 62; });
    ASSERT_FALSE(result);
    EXPECT_EQ(result.error().code, ErrorCode::Cancelled);
    ASSERT_EQ(channel.requests.size(), 2);
    EXPECT_EQ(channel.requests.back()[0], 0x55);
}
