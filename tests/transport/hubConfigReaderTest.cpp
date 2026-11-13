/**
 * @file hubConfigReaderTest.cpp
 * @brief Чтение конфигурации Harmony Hub по сценарию ответов, без сети.
 *
 * Ответы повторяют стоковый протокол порта 8088 (и его реализацию в harmony-ng,
 * tests/fixtures/ha/hbus_provision.json и hbus_config.json).
 */

#include "harmony/transport/hubConfigReader.h"

#include <gtest/gtest.h>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <memory>
#include <string>
#include <vector>

using namespace harmony;
using namespace harmony::transport;

namespace {

/// Кадр сервера WebSocket: без маски, как шлёт Hub.
std::string serverFrame(std::uint8_t opcode, const std::string& payload, bool last = true) {
    std::string frame;
    frame += static_cast<char>((last ? 0x80 : 0x00) | opcode);
    if (payload.size() < 126) {
        frame += static_cast<char>(payload.size());
    } else if (payload.size() <= 0xFFFF) {
        frame += static_cast<char>(126);
        frame += static_cast<char>(payload.size() >> 8);
        frame += static_cast<char>(payload.size() & 0xFF);
    } else {
        frame += static_cast<char>(127);
        for (int shift = 56; shift >= 0; shift -= 8) frame += static_cast<char>((payload.size() >> shift) & 0xFF);
    }
    return frame + payload;
}

/// Соединение со сценарием: заранее заданные байты ответа и запись отправленного.
class ScriptedChannel final : public Channel {
public:
    explicit ScriptedChannel(std::string reply, std::string* sent) : reply_(std::move(reply)), sent_(sent) {}
    Result<void> send(std::span<const std::uint8_t> bytes) override {
        sent_->append(bytes.begin(), bytes.end());
        return {};
    }
    Result<std::vector<std::uint8_t>> receive(std::size_t count, std::chrono::milliseconds) override {
        if (position_ + count > reply_.size()) return makeError(ErrorCode::Io, "connection to the remote lost (receiving)");
        std::vector<std::uint8_t> out(reply_.begin() + static_cast<std::ptrdiff_t>(position_),
                                      reply_.begin() + static_cast<std::ptrdiff_t>(position_ + count));
        position_ += count;
        return out;
    }

private:
    std::string reply_;
    std::string* sent_;
    std::size_t position_ = 0;
};

/// Hub из двух соединений: HTTP provisioning и WebSocket.
struct FakeHub {
    std::string provisionReply = R"({"id":1,"code":200,"msg":"OK","data":{"activeRemoteId":"1234567",)"
                                 R"("hubSwVersion":"4.15.600","hubName":"Living Room","accountId":"42",)"
                                 R"("email":"user@example.com"}})";
    std::string webSocketReply;
    std::vector<std::string> sent;
    int connections = 0;
    bool refuseConnect = false;

    FakeHub() {
        sent.reserve(4); // каналы хранят указатель на свою строку: без перераспределения
        // Текст JSON, а не списки инициализации: так нет неоднозначности «объект или массив».
        const std::string configReply = R"({"cmd":"vnd.logitech.harmony/vnd.logitech.harmony.engine?config",)"
            R"("id":"harmony-config","code":200,"msg":"OK","data":{)"
            R"("activity":[{"id":"10","label":"Watch TV","controlGroup":[]},)"
            R"({"id":"-1","label":"PowerOff","controlGroup":[]}],)"
            R"("device":[{"id":"1","label":"Living Room TV","manufacturer":"Samsung","model":"UE55",)"
            R"("type":"Television","controlGroup":[)"
            R"({"name":"Power","function":[{"name":"PowerOn"},{"name":"PowerOff"}]},)"
            R"({"name":"Volume","function":[{"name":"VolumeUp"}]}]}]}})";
        const std::string discoveryReply = R"({"cmd":"connect.discoveryinfo?get","id":"harmony-discovery",)"
            R"("code":200,"data":{"friendlyName":"Living Room","current_fw_version":"4.15.600","hubId":"secret"}})";
        const std::string digest = R"({"type":"connect.stateDigest?notify","data":{"activityId":"-1"}})";
        webSocketReply = "HTTP/1.1 101 Switching Protocols\r\nUpgrade: websocket\r\nConnection: Upgrade\r\n"
                         "Sec-WebSocket-Accept: x\r\n\r\n" +
                         serverFrame(0x1, digest) + serverFrame(0x9, "p") +
                         serverFrame(0x1, configReply) + serverFrame(0x1, discoveryReply);
    }

    ChannelFactory factory() {
        return [this]() -> Result<std::unique_ptr<Channel>> {
            if (refuseConnect) return makeError(ErrorCode::Io, "remote not found at %1:%2", {"10.0.0.2", "8088"});
            sent.emplace_back();
            const std::string reply =
                connections++ == 0
                    ? "HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nContent-Length: " +
                          std::to_string(provisionReply.size()) + "\r\n\r\n" + provisionReply
                    : webSocketReply;
            return std::unique_ptr<Channel>(std::make_unique<ScriptedChannel>(reply, &sent.back()));
        };
    }
};

/// Снимает маску с первого текстового кадра клиента после HTTP-заголовков.
std::string firstClientText(const std::string& sent) {
    const auto start = sent.find("\r\n\r\n") + 4;
    const auto* bytes = reinterpret_cast<const std::uint8_t*>(sent.data() + start);
    EXPECT_EQ(bytes[0], 0x81);
    EXPECT_NE(bytes[1] & 0x80, 0); // клиент обязан маскировать
    std::size_t length = bytes[1] & 0x7F;
    std::size_t offset = 2;
    if (length == 126) {
        length = (static_cast<std::size_t>(bytes[2]) << 8) | bytes[3];
        offset = 4;
    }
    std::string text;
    for (std::size_t i = 0; i < length; ++i)
        text += static_cast<char>(bytes[offset + 4 + i] ^ bytes[offset + (i % 4)]);
    return text;
}

} // namespace

TEST(HubConfigReader, ReadsDevicesAndActivitiesWithoutAccountData) {
    FakeHub hub;
    const auto result = readHubConfig(hub.factory(), "10.0.0.2", kHubPort);
    ASSERT_TRUE(result) << result.error().message;
    EXPECT_EQ(result->remoteId, "1234567");
    EXPECT_EQ(result->hubName, "Living Room");
    EXPECT_EQ(result->firmware, "4.15.600");
    ASSERT_EQ(result->devices.size(), 1U);
    EXPECT_EQ(result->devices[0].manufacturer, "Samsung");
    EXPECT_EQ(result->devices[0].commands, 3U);
    ASSERT_EQ(result->activities.size(), 1U); // PowerOff не считается
    EXPECT_EQ(result->activities[0], "Watch TV");

    // В файл не попадают e-mail, учётная запись и hubId.
    EXPECT_EQ(result->backupJson.find("user@example.com"), std::string::npos);
    EXPECT_EQ(result->backupJson.find("accountId"), std::string::npos);
    EXPECT_EQ(result->backupJson.find("secret"), std::string::npos);
    const auto backup = nlohmann::json::parse(result->backupJson);
    EXPECT_EQ(backup["format"], "harmony-hub-config");
    EXPECT_EQ(backup["config"]["device"][0]["model"], "UE55");
}

TEST(HubConfigReader, SendsStockRequests) {
    FakeHub hub;
    ASSERT_TRUE(readHubConfig(hub.factory(), "10.0.0.2", kHubPort));
    ASSERT_EQ(hub.sent.size(), 2U);
    EXPECT_TRUE(hub.sent[0].starts_with("POST / HTTP/1.1\r\n"));
    EXPECT_NE(hub.sent[0].find("setup.account?getProvisionInfo"), std::string::npos);
    EXPECT_NE(hub.sent[0].find("Origin: http://sl.dhg.myharmony.com"), std::string::npos);
    EXPECT_TRUE(hub.sent[1].starts_with("GET /?domain=svcs.myharmony.com&hubId=1234567 HTTP/1.1\r\n"));
    EXPECT_NE(hub.sent[1].find("Sec-WebSocket-Version: 13"), std::string::npos);

    const auto request = nlohmann::json::parse(firstClientText(hub.sent[1]));
    EXPECT_EQ(request["hubId"], "1234567");
    EXPECT_EQ(request["hbus"]["cmd"], "vnd.logitech.harmony/vnd.logitech.harmony.engine?config");
    EXPECT_EQ(request["hbus"]["id"], "harmony-config");
}

TEST(HubConfigReader, ReportsUnreachableHub) {
    FakeHub hub;
    hub.refuseConnect = true;
    const auto result = readHubConfig(hub.factory(), "10.0.0.2", kHubPort);
    ASSERT_FALSE(result);
    EXPECT_EQ(result.error().code, ErrorCode::Io);
    EXPECT_NE(result.error().message.find("10.0.0.2"), std::string::npos);
}

TEST(HubConfigReader, RejectsRefusedWebSocket) {
    FakeHub hub;
    hub.webSocketReply = "HTTP/1.1 404 Not Found\r\nContent-Length: 0\r\n\r\n";
    const auto result = readHubConfig(hub.factory(), "10.0.0.2", kHubPort);
    ASSERT_FALSE(result);
    EXPECT_EQ(result.error().code, ErrorCode::Remote);
}

TEST(HubConfigReader, RejectsErrorCodeFromHub) {
    FakeHub hub;
    hub.webSocketReply = "HTTP/1.1 101 Switching Protocols\r\n\r\n" +
                         serverFrame(0x1, R"({"id":"harmony-config","code":500,"msg":"Error"})");
    const auto result = readHubConfig(hub.factory(), "10.0.0.2", kHubPort);
    ASSERT_FALSE(result);
    EXPECT_EQ(result.error().code, ErrorCode::Remote);
}

TEST(HubConfigReader, RejectsInvalidRemoteId) {
    FakeHub hub;
    hub.provisionReply = R"({"code":200,"data":{"activeRemoteId":"../x"}})";
    const auto result = readHubConfig(hub.factory(), "10.0.0.2", kHubPort);
    ASSERT_FALSE(result);
    EXPECT_EQ(result.error().code, ErrorCode::Format);
}

TEST(HubConfigReader, AssemblesFragmentedMessage) {
    FakeHub hub;
    const std::string reply =
        R"({"id":"harmony-config","code":200,"data":{"device":[],"activity":[{"id":"5","label":"Music"}]}})";
    const std::string bigPadding(70000, ' '); // проверка 64-битной длины кадра
    hub.webSocketReply = "HTTP/1.1 101 Switching Protocols\r\n\r\n" + serverFrame(0x1, reply.substr(0, 20), false) +
                         serverFrame(0x0, reply.substr(20) + bigPadding);
    const auto result = readHubConfig(hub.factory(), "10.0.0.2", kHubPort);
    ASSERT_TRUE(result) << result.error().message;
    ASSERT_EQ(result->activities.size(), 1U);
    EXPECT_EQ(result->activities[0], "Music");
    EXPECT_TRUE(result->firmware == "4.15.600"); // из provisioning, discoveryinfo не пришёл
}

TEST(HubConfigReader, CancelsFromProgress) {
    FakeHub hub;
    const auto result = readHubConfig(hub.factory(), "10.0.0.2", kHubPort,
                                      [](Stage, std::size_t, std::size_t) { return false; });
    ASSERT_FALSE(result);
    EXPECT_EQ(result.error().code, ErrorCode::Cancelled);
}

TEST(HubConfigReader, OpensSavedBackup) {
    FakeHub hub;
    const auto read = readHubConfig(hub.factory(), "10.0.0.2", kHubPort);
    ASSERT_TRUE(read);
    const auto opened = openHubBackup(read->backupJson);
    ASSERT_TRUE(opened) << opened.error().message;
    EXPECT_EQ(opened->devices.size(), 1U);
    EXPECT_EQ(opened->activities.size(), 1U);
    EXPECT_FALSE(openHubBackup(R"({"format":"harmony-hid-backup","version":1})"));
    EXPECT_EQ(openHubBackup(R"({"format":"harmony-hub-config","version":2,"config":{}})").error().code,
              ErrorCode::Unsupported);
}
