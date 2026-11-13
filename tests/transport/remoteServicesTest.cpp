/**
 * @file remoteServicesTest.cpp
 * @brief Служба настроек (HTTP) и консоль (telnet) пульта на подставном канале.
 */

#include "harmony/transport/remoteServices.h"

#include <gtest/gtest.h>

#include <deque>
#include <memory>
#include <string>

using namespace harmony::transport;
using harmony::ErrorCode;
using harmony::Result;

namespace {

/// Подставной сервер: отдаёт заранее заданный текст, запоминает отправленное.
struct Script {
    std::deque<std::uint8_t> toClient;
    std::string fromClient;
};

class ScriptChannel final : public Channel {
public:
    explicit ScriptChannel(std::shared_ptr<Script> script) : script_(std::move(script)) {}
    Result<void> send(std::span<const std::uint8_t> bytes) override {
        script_->fromClient.append(bytes.begin(), bytes.end());
        return {};
    }
    Result<std::vector<std::uint8_t>> receive(std::size_t count, std::chrono::milliseconds) override {
        if (script_->toClient.size() < count) {
            return harmony::makeError(ErrorCode::Io, "closed");
        }
        std::vector<std::uint8_t> out(script_->toClient.begin(),
                                      script_->toClient.begin() + static_cast<std::ptrdiff_t>(count));
        script_->toClient.erase(script_->toClient.begin(), script_->toClient.begin() + static_cast<std::ptrdiff_t>(count));
        return out;
    }

private:
    std::shared_ptr<Script> script_;
};

ChannelFactory factoryFor(const std::shared_ptr<Script>& script) {
    return [script]() -> Result<std::unique_ptr<Channel>> { return std::make_unique<ScriptChannel>(script); };
}

void say(Script& script, std::string_view text) {
    script.toClient.insert(script.toClient.end(), text.begin(), text.end());
}

} // namespace

TEST(RemoteServices, ReadsSettingFromEventEnvelope) {
    auto script = std::make_shared<Script>();
    say(*script, "HTTP/1.0 200 OK\r\n\r\n<Event><Name>backlightlevel</Name><Value>60</Value></Event>");
    auto value = readRemoteSetting("host", "/system/backlightlevel", factoryFor(script));
    ASSERT_TRUE(value) << value.error().message;
    EXPECT_EQ(*value, "60");
    EXPECT_EQ(script->fromClient, "GET /system/backlightlevel HTTP/1.0\r\n\r\n");
}

TEST(RemoteServices, PostsTheBareValue) {
    auto script = std::make_shared<Script>();
    say(*script, "HTTP/1.0 200 OK\r\n\r\n");
    ASSERT_TRUE(writeRemoteSetting("host", "/system/sound", "1", factoryFor(script)));
    EXPECT_TRUE(script->fromClient.starts_with("POST /system/sound HTTP/1.0\r\n"));
    EXPECT_TRUE(script->fromClient.ends_with("Content-Length: 1\r\n\r\n1"));
}

TEST(RemoteServices, HttpErrorIsReported) {
    auto script = std::make_shared<Script>();
    say(*script, "HTTP/1.0 404 Not Found\r\n\r\n");
    EXPECT_EQ(readRemoteSetting("host", "/system/none", factoryFor(script)).error().code, ErrorCode::Remote);
}

TEST(RemoteServices, ShellLogsInRefusesOptionsAndReturnsOutput) {
    auto script = std::make_shared<Script>();
    // Сервер предлагает эхо (IAC WILL ECHO) и просит терминал (IAC DO TTYPE).
    script->toClient.insert(script->toClient.end(), {255, 251, 1, 255, 253, 24});
    say(*script, "QNX Neutrino\r\nlogin: ");
    say(*script, "Password:");
    say(*script, "\r\n# ");
    say(*script, "sloginfo; echo \"__harmony\"\"_done__\"\r\nline one\r\nline two\r\n__harmony_done__");
    auto output = readRemoteLog("host", factoryFor(script));
    ASSERT_TRUE(output) << output.error().message;
    EXPECT_EQ(*output, "line one\nline two");
    // Отказ на оба предложения, затем вход.
    const std::string refused = {'\xFF', '\xFE', '\x01', '\xFF', '\xFC', '\x18'};
    EXPECT_TRUE(script->fromClient.starts_with(refused));
    EXPECT_NE(script->fromClient.find("root\r\n"), std::string::npos);
    EXPECT_NE(script->fromClient.find("ethanol\r\n"), std::string::npos);
}

TEST(RemoteServices, ShellReportsRefusedLogin) {
    auto script = std::make_shared<Script>();
    say(*script, "login: Password:\r\nLogin incorrect\r\nlogin:");
    EXPECT_EQ(runRemoteShell("host", "ls", std::chrono::seconds(1), factoryFor(script)).error().code,
              ErrorCode::Remote);
}

TEST(RemoteServices, WriteWithoutHttpStatusIsAnError) {
    auto script = std::make_shared<Script>();
    EXPECT_EQ(writeRemoteSetting("host", "/system/sound", "1", factoryFor(script)).error().code, ErrorCode::Remote);
    auto garbage = std::make_shared<Script>();
    say(*garbage, "ok");
    EXPECT_EQ(writeRemoteSetting("host", "/system/sound", "1", factoryFor(garbage)).error().code, ErrorCode::Remote);
}

TEST(RemoteServices, ShellWithoutEndMarkerIsAnError) {
    auto script = std::make_shared<Script>();
    say(*script, "login: Password:\r\n# ");
    say(*script, "ls; echo \"__harmony\"\"_done__\"\r\npartial output\r\n");
    EXPECT_EQ(runRemoteShell("host", "ls", std::chrono::seconds(1), factoryFor(script)).error().code,
              ErrorCode::Remote);
}

TEST(RemoteServices, SettingValueWithoutEnvelope) {
    EXPECT_EQ(settingValue("HTTP/1.0 200 OK\r\nX: y\r\n\r\n 25 \r\n"), "25");
    EXPECT_EQ(settingValue("Military"), "Military");
}
