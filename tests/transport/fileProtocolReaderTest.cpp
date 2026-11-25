/**
 * @file fileProtocolReaderTest.cpp
 * @brief I9: решения читателя файлового протокола — список семейства, режим чтения, размер,
 *        признак отказа и закрытие файла.
 *
 * Канал подменён: ответы строятся по отправленному запросу, системной USB-библиотеки нет.
 * Фикстуры синтетические и лежат в git (правило 19); значения настоящих пультов не
 * воспроизводятся. Утверждаются решения программы, а не содержимое данных.
 */

#include "harmony/transport/fileProtocolReader.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <deque>
#include <string>
#include <vector>

using namespace harmony;
using namespace harmony::transport;

namespace {

constexpr std::uint8_t kPing = 0x00;
constexpr std::uint8_t kOpen = 0x01;
constexpr std::uint8_t kRead = 0x04;
constexpr std::uint8_t kClose = 0x07;

std::vector<std::uint8_t> fixture(const std::string& text) {
    std::vector<std::uint8_t> bytes;
    for (char c : text) bytes.push_back(static_cast<std::uint8_t>(c));
    return bytes;
}

/// Подменный канал файловому протоколу. `statedSize` — что заявляет open, `delivered` — что
/// отвечает пульт: последний пакет объявляет полную длину и дополняется NUL.
class FakeFileChannel final : public HidChannel {
public:
    std::vector<std::uint8_t> commands;   ///< какие команды читатель отправил, по порядку
    std::size_t pingParameterCount = 0;
    std::string openPath, openMode;
    std::vector<std::uint8_t> file;
    std::size_t statedSize = 83;
    std::size_t delivered = 124;
    bool refuseOpen = false;

    Result<void> send(const std::array<std::uint8_t, 64>& report) override {
        const auto command = report[1];
        commands.push_back(command);
        const auto params = requestParams(report);
        if (command == kPing) {
            pingParameterCount = report[3];
            replies.push_back({0xFF, kPing, 0x00, 0x00});
            return {};
        }
        if (command == kOpen) {
            openPath = text(params.empty() ? std::vector<std::uint8_t>{} : params[0]);
            openMode = params.size() > 1 ? text(params[1]) : "";
            if (refuseOpen) {
                replies.push_back({0xFF, kOpen, 0xFF, 0x01, 0x01, 0x0B});
                return {};
            }
            replies.push_back(reply(kOpen, {{1, handle}, bigEndian(statedSize)}));
            return {};
        }
        if (command == kRead) {
            replies.push_back(reply(kRead, {{1, handle}, {1, 32}}));   // эхо заголовка запроса
            std::size_t sent = 0;
            while (sent < delivered) {
                const std::size_t chunk = std::min<std::size_t>(delivered - sent, 62);
                std::vector<std::uint8_t> packet{1, static_cast<std::uint8_t>(chunk)};
                for (std::size_t i = 0; i < chunk; ++i)
                    packet.push_back(sent + i < file.size() ? file[sent + i] : 0x00);
                sent += chunk;
                replies.push_back(std::move(packet));
            }
            replies.push_back({0xFE});
            return {};
        }
        if (command == kClose) {
            replies.push_back({0xFF, kClose, 0x00, 0x00});
            return {};
        }
        return makeError(ErrorCode::Remote, "The fake channel answers only reading commands");
    }

    Result<std::vector<std::uint8_t>> receive() override {
        if (replies.empty()) return makeError(ErrorCode::Io, "Timeout");
        auto report = std::move(replies.front());
        replies.pop_front();
        return report;
    }

private:
    std::deque<std::vector<std::uint8_t>> replies;
    std::uint8_t handle = 5;

    static std::vector<std::vector<std::uint8_t>> requestParams(const std::array<std::uint8_t, 64>& report) {
        std::vector<std::vector<std::uint8_t>> params;
        std::size_t at = 4;
        for (std::size_t i = 0; i < static_cast<std::size_t>(report[3]) && at < report.size(); ++i) {
            const std::size_t tag = report[at++];
            std::vector<std::uint8_t> value;
            if (tag == 0x80) {
                while (at < report.size() && report[at] != 0) value.push_back(report[at++]);
            } else {
                for (std::size_t n = 0; n < tag && at < report.size(); ++n) value.push_back(report[at++]);
            }
            params.push_back(std::move(value));
        }
        return params;
    }

    static std::vector<std::uint8_t> reply(std::uint8_t command, const std::vector<std::vector<std::uint8_t>>& params) {
        std::vector<std::uint8_t> out{0xFF, command, 0x00, static_cast<std::uint8_t>(params.size())};
        for (const auto& param : params) {
            out.push_back(static_cast<std::uint8_t>(param.size()));
            out.insert(out.end(), param.begin(), param.end());
        }
        return out;
    }

    static std::vector<std::uint8_t> bigEndian(std::size_t value) {
        return {4, static_cast<std::uint8_t>(value >> 24), static_cast<std::uint8_t>(value >> 16),
                static_cast<std::uint8_t>(value >> 8), static_cast<std::uint8_t>(value)};
    }

    static std::string text(const std::vector<std::uint8_t>& bytes) {
        return std::string(bytes.begin(), bytes.end());
    }
};

/// Синтетическая фикстура ответа `/rf/deviceinfo` — форма JSON, а не строки «имя значение».
const std::string kDeviceInfo = "Response, {\"EquadID\":\"0\",\"RFID\":\"0\",\"Devices\":[],\"DeviceCount\":0}";
/// Синтетическая фикстура `/sys/sysinfo` — строки «имя значение».
const std::string kSysInfo = "arch 0x11\nskin 0x63\nfw_ver 4.15.330\n";

} // namespace

TEST(FileProtocolReader, RefusesPathOutsideFamilyListBeforeSendingAnything) {
    FakeFileChannel channel;
    FileProtocolReader reader(channel, "touch");
    const auto result = reader.read("/cfg/log");
    ASSERT_FALSE(result);
    EXPECT_EQ(result.error().code, ErrorCode::Unsupported);
    EXPECT_TRUE(channel.commands.empty()) << "путь, который может быть действием, нельзя открывать";
}

TEST(FileProtocolReader, RefusesPathConfirmedOnAnotherModelAndNamesBoth) {
    FakeFileChannel channel;
    FileProtocolReader reader(channel, "ultimate");
    const auto result = reader.read("/rf/deviceinfo");
    ASSERT_FALSE(result);
    EXPECT_EQ(result.error().code, ErrorCode::Unsupported);
    EXPECT_TRUE(channel.commands.empty());
    EXPECT_NE(result.error().message.find("Harmony Touch"), std::string::npos) << result.error().message;
    EXPECT_NE(result.error().message.find("Harmony Ultimate"), std::string::npos) << result.error().message;
}

TEST(FileProtocolReader, RefusesModelWithoutFamilyDescriptionBeforeSendingAnything) {
    FakeFileChannel channel;
    FileProtocolReader reader(channel, "800");   // каталог называет read: true, подтверждений нет
    const auto result = reader.read("/sys/sysinfo");
    ASSERT_FALSE(result);
    EXPECT_EQ(result.error().code, ErrorCode::Unsupported);
    EXPECT_TRUE(channel.commands.empty());
}

TEST(FileProtocolReader, SendsOnlyReadingCommandsAndThePingWithoutParameters) {
    FakeFileChannel channel;
    channel.file = fixture(kDeviceInfo);
    channel.statedSize = channel.file.size();
    channel.delivered = channel.statedSize + 41;
    FileProtocolReader reader(channel, "touch");
    const auto result = reader.read("/rf/deviceinfo");
    ASSERT_TRUE(result) << result.error().message;
    EXPECT_EQ(channel.commands, std::vector<std::uint8_t>{kPing, kOpen, kRead, kClose});
    EXPECT_EQ(channel.pingParameterCount, std::size_t{0});   // ping с параметром — имитация USB-сброса
}

TEST(FileProtocolReader, OpensEveryPathForReadingModeOnly) {
    FakeFileChannel channel;
    channel.file = fixture(kSysInfo);
    channel.statedSize = channel.file.size();
    channel.delivered = channel.statedSize;
    FileProtocolReader reader(channel, "touch");
    const auto result = reader.read("/sys/sysinfo");
    ASSERT_TRUE(result) << result.error().message;
    EXPECT_EQ(channel.openPath, "/sys/sysinfo");
    EXPECT_EQ(channel.openMode, "R");
}

TEST(FileProtocolReader, CutsTheReadToTheSizeTheOpenStates) {
    FakeFileChannel channel;
    channel.file = fixture(kDeviceInfo);
    channel.statedSize = channel.file.size();
    channel.delivered = channel.statedSize + 41;   // фикстура 65, ответ 106: как на пульте (83 против 124)
    FileProtocolReader reader(channel, "touch");
    const auto result = reader.read("/rf/deviceinfo");
    ASSERT_TRUE(result) << result.error().message;
    EXPECT_EQ(result->bytes.size(), channel.statedSize);
    EXPECT_TRUE(result->complete);
    EXPECT_GT(channel.delivered, result->bytes.size()) << "дополнение пакета не стало содержимым";
    EXPECT_EQ(std::string(result->bytes.begin(), result->bytes.end()), kDeviceInfo);
}

TEST(FileProtocolReader, ShortReadStaysShortAndIsReportedIncomplete) {
    FakeFileChannel channel;
    channel.file = fixture(kDeviceInfo);
    channel.statedSize = channel.file.size();
    channel.delivered = 40;   // передача оборвалась: это факт о передаче, а не о файле
    FileProtocolReader reader(channel, "touch");
    const auto result = reader.read("/rf/deviceinfo");
    ASSERT_TRUE(result) << result.error().message;
    EXPECT_EQ(result->bytes.size(), std::size_t{40});
    EXPECT_FALSE(result->complete);
    EXPECT_EQ(result->statedSize, channel.statedSize);
}

TEST(FileProtocolReader, RefusalAtOpenIsAnErrorAndStopsBeforeReading) {
    FakeFileChannel channel;
    channel.file = fixture(kDeviceInfo);
    channel.statedSize = channel.file.size();
    channel.refuseOpen = true;
    FileProtocolReader reader(channel, "touch");
    const auto result = reader.read("/rf/deviceinfo");
    ASSERT_FALSE(result);
    EXPECT_EQ(result.error().code, ErrorCode::Remote);
    EXPECT_EQ(channel.commands, std::vector<std::uint8_t>{kPing, kOpen}) << "read не отправляется";
    EXPECT_NE(result.error().message.find("0x0B"), std::string::npos) << result.error().message;
}

TEST(FileProtocolReader, ClosesTheFileBeforeOpeningAnother) {
    FakeFileChannel channel;
    channel.file = fixture(kSysInfo);
    channel.statedSize = channel.file.size();
    channel.delivered = channel.statedSize;
    FileProtocolReader reader(channel, "touch");
    const auto first = reader.read("/sys/sysinfo");
    ASSERT_TRUE(first) << first.error().message;
    const auto second = reader.read("/sys/sysinfo");
    ASSERT_TRUE(second) << second.error().message;
    EXPECT_EQ(channel.commands, std::vector<std::uint8_t>{kPing, kOpen, kRead, kClose,
                                                           kPing, kOpen, kRead, kClose});
}
