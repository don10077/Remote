/**
 * @file harmony900LinkTest.cpp
 * @brief Тесты протокола Harmony 900 на поддельном пульте: опознание, чтение, запись, перезапуск.
 */

#include "harmony/core/formats/ezhex.h"
#include "harmony/transport/harmony900Link.h"
#include "harmony/transport/usbnet.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <deque>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <memory>

using namespace harmony::transport;
using harmony::ErrorCode;
using harmony::Result;
using usbnet::Command;
using usbnet::Packet;

namespace {

std::vector<std::uint8_t> readFile(const std::filesystem::path& path) {
    std::ifstream file(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
}

const std::vector<std::uint8_t>& sampleEzHex() {
    static const auto bytes = readFile(std::filesystem::path(HARMONY_TEST_DATA_DIR) / "ezhex" / "harmony900.EZHex");
    return bytes;
}

/// Канал поверх готовых байтов — чтобы разбирать запросы тем же usbnet::receive.
class BufferChannel final : public Channel {
public:
    explicit BufferChannel(std::deque<std::uint8_t>& buffer) : buffer_(buffer) {}
    Result<void> send(std::span<const std::uint8_t>) override { return {}; }
    Result<std::vector<std::uint8_t>> receive(std::size_t count, std::chrono::milliseconds) override {
        if (buffer_.size() < count) {
            return harmony::makeError(ErrorCode::Io, "мало данных");
        }
        std::vector<std::uint8_t> out(buffer_.begin(), buffer_.begin() + static_cast<std::ptrdiff_t>(count));
        buffer_.erase(buffer_.begin(), buffer_.begin() + static_cast<std::ptrdiff_t>(count));
        return out;
    }

private:
    std::deque<std::uint8_t>& buffer_;
};

/// Состояние поддельного пульта — общее для всех подключений к нему.
struct FakeRemote {
    std::vector<std::uint8_t> region;  ///< Область конфигурации (ZIP)
    std::vector<std::uint8_t> written; ///< Что записали
    std::vector<Command> commands;     ///< Полученные команды по порядку
    int skin = 61;
    int architecture = 15;
    int resets = 0;
    int refuseAfterReset = 2; ///< Сколько подключений отвергнуть после перезапуска («загружается»)
    int refused = 0;
    std::size_t readOffset = 0;
    std::uint32_t reportedSize = 0; ///< Не 0 — какой размер конфигурации назвать вместо настоящего
    std::vector<std::uint8_t> timeParams; ///< Коды длины параметров UPDATE_TIME
    int learnTimeouts = 0;                 ///< Сколько раз ответить «сигнала не было»
    std::vector<std::uint16_t> learnWords; ///< Слова сигнала, как их шлёт пульт
    std::size_t learnOffset = 0;
    bool learning = false;

    Packet answer(const Packet& request) {
        commands.push_back(request.command);
        Packet reply{.command = request.command, .request = false, .status = usbnet::kStatusOk, .params = {}, .lengthCodes = {}};
        switch (request.command) {
        case Command::GetSystemInfo:
            reply.params = {usbnet::number(0x046D, 2), usbnet::number(0xC11F, 2), usbnet::number(static_cast<std::uint32_t>(architecture), 2),
                            usbnet::number(5, 2), usbnet::number(9, 2), {0}, usbnet::number(static_cast<std::uint32_t>(skin), 2),
                            usbnet::number(0x0011, 2)};
            break;
        case Command::GetGuid:
            reply.params = {{0xDE, 0xAD, 0xBE, 0xEF}};
            break;
        case Command::ReadRegion:
            readOffset = 0;
            reply.params = {usbnet::number(reportedSize != 0 ? reportedSize : static_cast<std::uint32_t>(region.size()), 4)};
            break;
        case Command::ReadRegionData: {
            const std::size_t length = std::min<std::size_t>(1024, region.size() - readOffset);
            std::vector<std::uint8_t> data(1024, 0);
            std::copy_n(region.begin() + static_cast<std::ptrdiff_t>(readOffset), length, data.begin());
            readOffset += length;
            reply.params = {{usbnet::kUserConfigRegion}, data, usbnet::number(static_cast<std::uint32_t>(length), 4)};
            break;
        }
        case Command::StartUpdate:
            written.clear();
            break;
        case Command::WriteUpdateData: {
            const auto length = usbnet::toNumber(request.params[2]);
            written.insert(written.end(), request.params[1].begin(),
                           request.params[1].begin() + static_cast<std::ptrdiff_t>(length));
            break;
        }
        case Command::GetUpdateChecksum:
            reply.params = {usbnet::number(0xBEEF, 2)};
            break;
        case Command::FinishUpdate:
            region = written;
            break;
        case Command::LearnIrStart:
            learning = true;
            learnOffset = 0;
            break;
        case Command::LearnIrDone:
            learning = false;
            break;
        case Command::LearnIrSingle: {
            if (learnTimeouts > 0) {
                --learnTimeouts;
                reply.status = usbnet::kStatusTimeout;
                break;
            }
            // Порция: флаг «есть ещё» и 4 слова (недостающие — нули).
            std::vector<std::vector<std::uint8_t>> params;
            const bool more = learnOffset + 4 < learnWords.size();
            params.push_back({static_cast<std::uint8_t>(more ? 1 : 0)});
            for (int i = 0; i < 4; ++i) {
                const std::uint16_t word = learnOffset < learnWords.size() ? learnWords[learnOffset] : 0;
                ++learnOffset;
                params.push_back(usbnet::number(word, 2));
            }
            reply.params = std::move(params);
            break;
        }
        case Command::UpdateTime:
            timeParams.clear();
            for (const auto& param : request.params) {
                timeParams.push_back(static_cast<std::uint8_t>(param.size()));
            }
            break;
        default:
            break;
        }
        return reply;
    }
};

/// Одно подключение к поддельному пульту.
class FakeChannel final : public Channel {
public:
    explicit FakeChannel(std::shared_ptr<FakeRemote> remote) : remote_(std::move(remote)) {}

    Result<void> send(std::span<const std::uint8_t> bytes) override {
        inbox_.insert(inbox_.end(), bytes.begin(), bytes.end());
        BufferChannel reader(inbox_);
        while (!inbox_.empty()) {
            auto request = usbnet::receive(reader, {});
            if (!request) {
                break;
            }
            if (request->command == Command::ZReset) {
                ++remote_->resets;
                remote_->refused = 0;
                closed_ = true; // перезапуск: соединение пропадает без ответа
                return {};
            }
            auto reply = usbnet::encode(remote_->answer(*request));
            outbox_.insert(outbox_.end(), reply->begin(), reply->end());
        }
        return {};
    }

    Result<std::vector<std::uint8_t>> receive(std::size_t count, std::chrono::milliseconds) override {
        if (closed_ || outbox_.size() < count) {
            return harmony::makeError(ErrorCode::Io, "нет ответа");
        }
        std::vector<std::uint8_t> out(outbox_.begin(), outbox_.begin() + static_cast<std::ptrdiff_t>(count));
        outbox_.erase(outbox_.begin(), outbox_.begin() + static_cast<std::ptrdiff_t>(count));
        return out;
    }

private:
    std::shared_ptr<FakeRemote> remote_;
    std::deque<std::uint8_t> inbox_;
    std::deque<std::uint8_t> outbox_;
    bool closed_ = false;
};

/// Связь с поддельным пультом и короткими таймингами.
Harmony900Link linkTo(const std::shared_ptr<FakeRemote>& remote) {
    ChannelFactory factory = [remote]() -> Result<std::unique_ptr<Channel>> {
        if (remote->resets > 0 && remote->refused < remote->refuseAfterReset) {
            ++remote->refused;
            return harmony::makeError(ErrorCode::Io, "загружается");
        }
        return std::unique_ptr<Channel>(std::make_unique<FakeChannel>(remote));
    };
    LinkTimings timings;
    timings.restartPoll = std::chrono::milliseconds(1);
    timings.restartAttempts = 5;
    return Harmony900Link(factory, timings);
}

std::shared_ptr<FakeRemote> remoteWithSample() {
    auto remote = std::make_shared<FakeRemote>();
    remote->region = harmony::formats::parseEzHex(sampleEzHex())->payload;
    return remote;
}

} // namespace

TEST(Usbnet, EncodesParameterLengths) {
    EXPECT_EQ(*usbnet::encodeLength(3), 0x03);
    EXPECT_EQ(*usbnet::encodeLength(1024), 0xC2);
    EXPECT_EQ(*usbnet::encodeLength(64), 0x50);
    EXPECT_EQ(usbnet::decodeLength(0xC2), 1024U);
    EXPECT_EQ(usbnet::decodeLength(0x83), 3U);
    EXPECT_FALSE(usbnet::encodeLength(1000));
}

TEST(Usbnet, RequestLayoutMatchesProtocol) {
    const Packet packet{.command = Command::ReadRegion, .request = true, .status = 0, .params = {{4}}, .lengthCodes = {}};
    EXPECT_EQ(*usbnet::encode(packet), (std::vector<std::uint8_t>{0x20, 0x47, 0x80, 0x01, 0x01, 0x04}));
}

TEST(Harmony900Link, IdentifiesRemote) {
    auto remote = remoteWithSample();
    auto link = linkTo(remote);

    auto info = link.connect({});

    ASSERT_TRUE(info) << info.error().message;
    EXPECT_EQ(info->model, "Harmony 900");
    EXPECT_EQ(info->identity.architecture, 15);
    EXPECT_EQ(info->identity.skin, 61);
    EXPECT_EQ(info->identity.board, "0.1.0");
    EXPECT_EQ(info->firmwareVersion, "5.9");
    EXPECT_EQ(info->serial, "DEADBEEF");
    EXPECT_TRUE(link.isConnected());
}

TEST(Harmony900Link, ReadsConfigAsConcordanceBackup) {
    auto remote = remoteWithSample();
    auto link = linkTo(remote);
    ASSERT_TRUE(link.connect({}));
    std::size_t lastDone = 0;

    auto ezhex = link.readConfig([&](Stage, std::size_t done, std::size_t) {
        lastDone = done;
        return true;
    });

    ASSERT_TRUE(ezhex) << ezhex.error().message;
    EXPECT_EQ(*ezhex, sampleEzHex()); // байт в байт как резервная копия concordance
    EXPECT_EQ(lastDone, remote->region.size());
    EXPECT_EQ(remote->commands.back(), Command::ReadRegionDone);
}

TEST(Harmony900Link, Reads1100VariantsWithoutChangingTheirIdentity) {
    for (const int skin : {62, 63, 64}) {
        auto remote = remoteWithSample();
        remote->skin = skin;
        remote->architecture = 11;
        auto link = linkTo(remote);
        auto info = link.connect({});
        ASSERT_TRUE(info);
        EXPECT_EQ(info->model, "Harmony 1100");
        auto backup = link.readConfig({});
        ASSERT_TRUE(backup) << backup.error().message;
        auto container = harmony::formats::parseEzHex(*backup);
        ASSERT_TRUE(container);
        EXPECT_EQ(container->identity.architecture, 11);
        EXPECT_EQ(container->identity.skin, skin);
        EXPECT_EQ(container->payload, remote->region);
        EXPECT_TRUE(remote->written.empty());
        EXPECT_EQ(remote->resets, 0);
    }
}

TEST(Harmony900Link, WritesConfigRestartsAndSetsTime) {
    auto remote = std::make_shared<FakeRemote>();
    auto link = linkTo(remote);
    ASSERT_TRUE(link.connect({}));
    std::vector<Stage> stages;

    auto written = link.writeConfig(sampleEzHex(), [&](Stage stage, std::size_t, std::size_t) {
        if (stages.empty() || stages.back() != stage) {
            stages.push_back(stage);
        }
        return true;
    });

    ASSERT_TRUE(written) << written.error().message;
    EXPECT_EQ(remote->region, harmony::formats::parseEzHex(sampleEzHex())->payload);
    EXPECT_EQ(remote->resets, 1);
    EXPECT_TRUE(link.isConnected()); // переподключились после перезапуска
    EXPECT_EQ(remote->timeParams.size(), 12U);
    EXPECT_EQ(stages, (std::vector<Stage>{Stage::Write, Stage::Verify, Stage::Restart, Stage::SetTime}));
    // Порядок команд обновления — как у программ Logitech.
    const auto start = std::ranges::find(remote->commands, Command::StartUpdate);
    ASSERT_NE(start, remote->commands.end());
    EXPECT_EQ(*(start + 1), Command::WriteUpdateHeader);
    const auto finish = std::ranges::find(remote->commands, Command::FinishUpdate);
    EXPECT_EQ(*(finish - 2), Command::WriteUpdateDataDone);
    EXPECT_EQ(*(finish - 1), Command::GetUpdateChecksum);
    EXPECT_EQ(link.updateChecksum(), (std::vector<std::vector<std::uint8_t>>{usbnet::number(0xBEEF, 2)}));
}

TEST(Harmony900Link, RejectsImplausibleConfigSize) {
    auto remote = remoteWithSample();
    remote->reportedSize = 0xFFFFFFF0U; // сбой обмена: под такой размер нельзя выделять память
    auto link = linkTo(remote);
    ASSERT_TRUE(link.connect({}));

    auto ezhex = link.readConfig({});

    ASSERT_FALSE(ezhex);
    EXPECT_EQ(ezhex.error().code, ErrorCode::Remote);
    EXPECT_EQ(std::ranges::count(remote->commands, Command::ReadRegionData), 0);
    EXPECT_EQ(remote->commands.back(), Command::ReadRegionDone); // пульт выведен из режима чтения
}

TEST(Harmony900Link, RefusesConfigForOtherModel) {
    auto remote = std::make_shared<FakeRemote>();
    auto link = linkTo(remote);
    ASSERT_TRUE(link.connect({}));
    auto container = harmony::formats::parseEzHex(sampleEzHex());
    container->header.clear();
    container->identity.skin = 63; // Harmony 1100

    auto ezhex = harmony::formats::writeEzHex(*container);
    ASSERT_TRUE(ezhex) << ezhex.error().message;
    auto written = link.writeConfig(*ezhex, {});

    ASSERT_FALSE(written);
    EXPECT_EQ(written.error().code, ErrorCode::Unsupported);
    EXPECT_NE(written.error().message.find("Harmony 1100"), std::string::npos) << written.error().message;
    EXPECT_EQ(std::ranges::count(remote->commands, Command::StartUpdate), 0);
}

TEST(Harmony900Link, CancelStopsBeforeFinish) {
    auto remote = std::make_shared<FakeRemote>();
    auto link = linkTo(remote);
    ASSERT_TRUE(link.connect({}));

    auto written = link.writeConfig(sampleEzHex(), [](Stage, std::size_t done, std::size_t) { return done < 4096; });

    ASSERT_FALSE(written);
    EXPECT_EQ(written.error().code, ErrorCode::Cancelled);
    EXPECT_EQ(std::ranges::count(remote->commands, Command::FinishUpdate), 0);
    EXPECT_TRUE(remote->region.empty()); // пульт не применил недописанное
}

TEST(Harmony900Link, ExplainsMissingRemote) {
    Harmony900Link link([]() -> Result<std::unique_ptr<Channel>> {
        return harmony::makeError(ErrorCode::Io, "пульт не найден по адресу 169.254.1.2:3074");
    });

    auto info = link.connect({});

    ASSERT_FALSE(info);
    EXPECT_EQ(info.error().code, ErrorCode::Io);
    EXPECT_NE(info.error().message.find("driver"), std::string::npos);
}

TEST(TcpChannel, ReportsUnreachableHost) {
    // Порт 1 на localhost закрыт: подключение отвергается сразу.
    auto channel = connectTcp("127.0.0.1", 1, std::chrono::milliseconds(500));
    ASSERT_FALSE(channel);
    EXPECT_EQ(channel.error().code, ErrorCode::Io);
}

TEST(Harmony900Link, LearnsSignalAfterWaiting) {
    auto remote = std::make_shared<FakeRemote>();
    remote->learnTimeouts = 2; // пользователь нажал кнопку не сразу
    // Сигнал NEC-подобный: первая порция — служебное слово, первый импульс 9000 мкс,
    // 342 периода несущей (38 кГц), затем «импульс + пауза» = 13500; дальше пары.
    remote->learnWords = {0x0001, 9000, 342, 13500, 560, 2250, 560, 1120, 560, 40560};
    auto link = linkTo(remote);
    ASSERT_TRUE(link.connect({}));
    std::size_t lastDone = 0;

    auto signal = link.learnIr([&](Stage stage, std::size_t done, std::size_t) {
        EXPECT_EQ(stage, Stage::Learn);
        lastDone = done;
        return true;
    }, std::chrono::seconds(30));

    ASSERT_TRUE(signal) << signal.error().message;
    EXPECT_EQ(signal->frequency, 38000U);
    EXPECT_EQ(signal->durations, (std::vector<int>{9000, -4500, 560, -1690, 560, -560, 560, -40000}));
    EXPECT_FALSE(remote->learning); // режим обучения закрыт
    EXPECT_EQ(lastDone, 30U);
}

TEST(Harmony900Link, LearningCanBeCancelled) {
    auto remote = std::make_shared<FakeRemote>();
    remote->learnTimeouts = 1000;
    auto link = linkTo(remote);
    ASSERT_TRUE(link.connect({}));
    int polls = 0;

    auto signal = link.learnIr([&](Stage, std::size_t, std::size_t) { return ++polls < 3; }, std::chrono::seconds(30));

    ASSERT_FALSE(signal);
    EXPECT_EQ(signal.error().code, ErrorCode::Cancelled);
    EXPECT_FALSE(remote->learning);
}
