/**
 * @file remoteCaptureTest.cpp
 * @brief Тесты снятого файла *.remote.json: запись и разбор, проверка целостности, старый *.hid.json.
 */

#include "harmony/core/formats/remoteCapture.h"

#include "harmony/core/formats/digest.h"

#include <gtest/gtest.h>
#include <nlohmann/json.hpp>

#include <string>

using namespace harmony;
using namespace harmony::formats;

namespace {

/// Снятие 665 с произвольными данными.
RemoteCapture sample() {
    RemoteCapture capture;
    capture.createdUtc = "2026-11-16T12:30:00Z";
    capture.program = "Harmony 0.9.0";
    capture.modelId = "665";
    capture.model = "Harmony 665";
    capture.identity = {.architecture = 14, .skin = 99, .flash = "0x01:0x49", .board = "0.1.0", .softwareType = 2};
    capture.protocol = 14;
    capture.firmware = "2.2";
    capture.container = CaptureContainer::EzHex;
    for (int i = 0; i < 1000; ++i) capture.data.push_back(static_cast<std::uint8_t>(i * 7));
    return capture;
}

std::string written(const RemoteCapture& capture) {
    auto json = writeRemoteCapture(capture);
    EXPECT_TRUE(json) << json.error().message;
    return json.value_or(std::string());
}

} // namespace

TEST(RemoteCapture, RoundTripKeepsEverything) {
    const RemoteCapture original = sample();
    const std::string json = written(original);
    EXPECT_TRUE(isRemoteCapture(json));
    const auto parsed = parseRemoteCapture(json);
    ASSERT_TRUE(parsed) << parsed.error().message;
    EXPECT_EQ(parsed->createdUtc, original.createdUtc);
    EXPECT_EQ(parsed->program, original.program);
    EXPECT_EQ(parsed->modelId, "665");
    EXPECT_EQ(parsed->model, "Harmony 665");
    EXPECT_EQ(parsed->identity.architecture, 14);
    EXPECT_EQ(parsed->identity.skin, 99);
    EXPECT_EQ(parsed->identity.softwareType, 2);
    EXPECT_EQ(parsed->identity.flash, "0x01:0x49");
    EXPECT_EQ(parsed->identity.board, "0.1.0");
    EXPECT_EQ(parsed->protocol, 14);
    EXPECT_EQ(parsed->firmware, "2.2");
    EXPECT_EQ(parsed->container, CaptureContainer::EzHex);
    EXPECT_EQ(parsed->data, original.data);
    EXPECT_FALSE(parsed->legacy);
}

TEST(RemoteCapture, WritesSizeAndSha256OfData) {
    const RemoteCapture capture = sample();
    const auto root = nlohmann::json::parse(written(capture));
    EXPECT_EQ(root["format"], "harmony-remote-capture");
    EXPECT_EQ(root["version"], 1);
    EXPECT_EQ(root["data"]["container"], "ezhex");
    EXPECT_EQ(root["data"]["size"], capture.data.size());
    EXPECT_EQ(root["data"]["sha256"], sha256Hex(capture.data));
}

TEST(RemoteCapture, EmptyProgramBecomesCurrentVersion) {
    RemoteCapture capture = sample();
    capture.program.clear();
    const auto parsed = parseRemoteCapture(written(capture));
    ASSERT_TRUE(parsed);
    EXPECT_TRUE(parsed->program.starts_with("Harmony ")) << parsed->program;
}

TEST(RemoteCapture, MhContainerIsKept) {
    RemoteCapture capture = sample();
    capture.container = CaptureContainer::Mh;
    const auto parsed = parseRemoteCapture(written(capture));
    ASSERT_TRUE(parsed);
    EXPECT_EQ(parsed->container, CaptureContainer::Mh);
}

TEST(RemoteCapture, RejectsDamagedData) {
    auto root = nlohmann::json::parse(written(sample()));
    auto damaged = root;
    damaged["data"]["sha256"] = std::string(64, '0');
    EXPECT_EQ(parseRemoteCapture(damaged.dump()).error().code, ErrorCode::Format);
    damaged = root;
    damaged["data"]["size"] = 999;
    EXPECT_EQ(parseRemoteCapture(damaged.dump()).error().code, ErrorCode::Format);
    damaged = root;
    damaged["data"]["base64"] = "not base64!";
    EXPECT_EQ(parseRemoteCapture(damaged.dump()).error().code, ErrorCode::Format);
    damaged = root;
    damaged["data"].erase("size");
    EXPECT_EQ(parseRemoteCapture(damaged.dump()).error().code, ErrorCode::Format);
    damaged = root;
    damaged["remote"].erase("skin");
    EXPECT_EQ(parseRemoteCapture(damaged.dump()).error().code, ErrorCode::Format);
}

TEST(RemoteCapture, RejectsOtherFilesAndVersions) {
    EXPECT_FALSE(isRemoteCapture("not json"));
    EXPECT_FALSE(isRemoteCapture(R"({"format":"harmony-hub-config","version":1})"));
    EXPECT_EQ(parseRemoteCapture(R"({"format":"harmony-hub-config","version":1})").error().code, ErrorCode::Format);
    EXPECT_EQ(parseRemoteCapture("[]").error().code, ErrorCode::Format);
    auto root = nlohmann::json::parse(written(sample()));
    root["version"] = 2;
    EXPECT_EQ(parseRemoteCapture(root.dump()).error().code, ErrorCode::Unsupported);
    root["version"] = 1;
    root["data"]["container"] = "tar";
    EXPECT_EQ(parseRemoteCapture(root.dump()).error().code, ErrorCode::Unsupported);
}

TEST(RemoteCapture, RawIsOnlyRead) {
    RemoteCapture capture = sample();
    capture.container = CaptureContainer::Raw;
    EXPECT_EQ(writeRemoteCapture(capture).error().code, ErrorCode::Unsupported);
    capture = sample();
    capture.data.clear();
    EXPECT_EQ(writeRemoteCapture(capture).error().code, ErrorCode::Format);
}

TEST(RemoteCapture, ReadsLegacyHidBackup) {
    // Так писал RemoteSession::readHidConfig (QJsonObject): плоский объект, данные в configBase64.
    const std::vector<std::uint8_t> raw = {'G', 'S', 'P', 'M', 1, 2, 3, 4, 5};
    nlohmann::json legacy = {
        {"format", "harmony-hid-backup"}, {"version", 1}, {"createdUtc", "2026-11-02T15:30:00Z"},
        {"model", "Harmony 665"}, {"architecture", 14}, {"skin", 99}, {"firmware", "2.2"},
        {"flash", "0x01:0x49"}, {"board", "0.1.0"}, {"identityHex", "0102"}, {"configBase", 0x030000},
        {"configSize", raw.size()}, {"configSha256", sha256Hex(raw)}, {"configBase64", base64Encode(raw)}};
    const std::string json = legacy.dump();
    EXPECT_TRUE(isRemoteCapture(json));
    const auto parsed = parseRemoteCapture(json);
    ASSERT_TRUE(parsed) << parsed.error().message;
    EXPECT_TRUE(parsed->legacy);
    EXPECT_EQ(parsed->container, CaptureContainer::Raw);
    EXPECT_EQ(parsed->data, raw);
    EXPECT_EQ(parsed->rawBase, 0x030000u);
    EXPECT_EQ(parsed->identityHex, "0102");
    EXPECT_EQ(parsed->identity.skin, 99);
    EXPECT_EQ(parsed->model, "Harmony 665");

    legacy["configSha256"] = std::string(64, 'f');
    EXPECT_EQ(parseRemoteCapture(legacy.dump()).error().code, ErrorCode::Format);
}

TEST(RemoteCapture, TimestampAndFileName) {
    using namespace std::chrono;
    const auto time = sys_days{year{2026} / 11 / 16} + hours{9} + minutes{5} + seconds{7} + milliseconds{900};
    EXPECT_EQ(utcTimestamp(time), "2026-11-16T09:05:07Z");
    EXPECT_EQ(captureFileName("665", 2026, 11, 16, 15, 30), "Harmony-665-20261116-1530.remote.json");
    EXPECT_EQ(captureFileName("auto", 2026, 1, 2, 3, 4), "Harmony-remote-20260102-0304.remote.json");
    EXPECT_EQ(captureFileName("a/b:c", 2026, 1, 2, 3, 4), "Harmony-a-b-c-20260102-0304.remote.json");
}
