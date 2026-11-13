/**
 * @file harmony900Link.cpp
 * @brief Обмен с Harmony 900: опознание, чтение и запись конфигурации.
 */

#include "harmony/transport/harmony900Link.h"

#include "harmony/transport/usbnet.h"

#include <cstdio>
#include <ctime>
#include <thread>

namespace harmony::transport {

using usbnet::Command;
using usbnet::Packet;
using Param = std::vector<std::uint8_t>;

namespace {

constexpr std::uint8_t kRegion = usbnet::kUserConfigRegion;

/// Верхняя граница размера конфигурации, о котором сообщает пульт. Реальные — единицы
/// мегабайт; больше — сбой обмена, и выделять под него память нельзя.
constexpr std::size_t kMaxConfigSize = 32U * 1024U * 1024U;

std::string hex2(unsigned value) {
    char text[8];
    std::snprintf(text, sizeof text, "0x%02X", value);
    return text;
}

/// Пауза между попытками подключиться после перезапуска.
void wait(std::chrono::milliseconds duration) {
    std::this_thread::sleep_for(duration);
}

bool report(const ProgressHandler& progress, Stage stage, std::size_t done, std::size_t total) {
    return !progress || progress(stage, done, total);
}

} // namespace

std::string modelName(int skin) {
    switch (skin) {
    case 49: return "Harmony 1000";
    case 52: return "Harmony 1000i";
    case 54: return "Harmony One";
    case 61: return "Harmony 900";
    case 62: case 63: case 64: return "Harmony 1100";
    case 66: return "Harmony 700";
    default: return {};
    }
}

/// Запрос — ответ по каналу с проверкой, что пульт ответил на эту команду успехом.
struct Harmony900Link::Exchange {
    static Result<Packet> request(Channel& channel, Command command, std::vector<Param> params,
                                  std::chrono::milliseconds timeout, std::vector<std::uint8_t> codes = {}) {
        Packet packet{.command = command, .request = true, .status = 0, .params = std::move(params),
                      .lengthCodes = std::move(codes)};
        auto bytes = usbnet::encode(packet);
        if (!bytes) {
            return std::unexpected(std::move(bytes.error()));
        }
        if (auto sent = channel.send(*bytes); !sent) {
            return std::unexpected(std::move(sent.error()));
        }
        auto reply = usbnet::receive(channel, timeout);
        if (!reply) {
            return std::unexpected(std::move(reply.error()));
        }
        const auto code = static_cast<unsigned>(command);
        if (reply->request || reply->command != command) {
            return makeError(ErrorCode::Remote, "the remote answered a different command (expected %1)", {hex2(code)});
        }
        if (reply->status != usbnet::kStatusOk) {
            return makeError(ErrorCode::Remote, "the remote rejected command %1 (status %2)", {hex2(code), std::to_string(reply->status)});
        }
        return reply;
    }
};

Harmony900Link::Harmony900Link(ChannelFactory factory, LinkTimings timings)
    : factory_(std::move(factory)), timings_(timings) {}

Harmony900Link::~Harmony900Link() = default;

Result<void> Harmony900Link::openChannel() {
    auto channel = factory_ ? factory_() : connectTcp(endpoint_.host, endpoint_.port, timings_.connect);
    if (!channel) {
        return wrapError(ErrorCode::Io, channel.error(),
                         "no connection to the remote — connect it via USB and wait a few "
                         "seconds (on Windows the driver from Logitech Harmony Remote Software "
                         "is required)");
    }
    channel_ = std::move(*channel);
    return {};
}

Result<RemoteInfo> Harmony900Link::identify() {
    auto system = Exchange::request(*channel_, Command::GetSystemInfo, {}, timings_.reply);
    if (!system) {
        return std::unexpected(std::move(system.error()));
    }
    const auto& p = system->params;
    if (p.size() < 8) {
        return makeError(ErrorCode::Remote, "the remote sent incomplete information about itself");
    }
    RemoteInfo info;
    info.identity.architecture = static_cast<int>(usbnet::toNumber(p[2]));
    info.identity.skin = static_cast<int>(usbnet::toNumber(p[6]));
    info.identity.softwareType = static_cast<int>(usbnet::toNumber(p[5]));
    const auto hardware = usbnet::toNumber(p[7]);
    info.identity.board = std::to_string(hardware >> 8) + "." + std::to_string((hardware >> 4) & 0x0F) + ".0";
    info.identity.flash = "0x01:0x49"; // у этого семейства пульт его не сообщает
    info.firmwareVersion = std::to_string(usbnet::toNumber(p[3])) + "." + std::to_string(usbnet::toNumber(p[4]));
    info.model = modelName(info.identity.skin);
    if (info.model.empty()) {
        info.model = "Harmony (skin " + std::to_string(info.identity.skin) + ")"; // модель неизвестна
    }

    auto guid = Exchange::request(*channel_, Command::GetGuid, {}, timings_.reply);
    if (guid && !guid->params.empty()) {
        for (const std::uint8_t byte : guid->params[0]) {
            char text[3];
            std::snprintf(text, sizeof text, "%02X", byte);
            info.serial += text;
        }
    }
    return info;
}

Result<RemoteInfo> Harmony900Link::connect(const Endpoint& endpoint) {
    disconnect();
    endpoint_ = endpoint;
    if (auto opened = openChannel(); !opened) {
        return std::unexpected(std::move(opened.error()));
    }
    auto info = identify();
    if (!info) {
        disconnect();
        return info;
    }
    info_ = *info;
    return info;
}

void Harmony900Link::disconnect() {
    channel_.reset();
}

Result<std::vector<std::uint8_t>> Harmony900Link::readConfig(const ProgressHandler& progress) {
    if (!channel_) {
        return makeError(ErrorCode::Io, "the remote is not connected");
    }
    auto start = Exchange::request(*channel_, Command::ReadRegion, {{kRegion}}, timings_.reply);
    if (!start) {
        return std::unexpected(std::move(start.error()));
    }
    if (start->params.empty()) {
        return makeError(ErrorCode::Remote, "the remote did not report the configuration size");
    }
    const std::size_t size = usbnet::toNumber(start->params[0]);
    if (size > kMaxConfigSize) {
        (void)Exchange::request(*channel_, Command::ReadRegionDone, {{kRegion}}, timings_.reply);
        return makeError(ErrorCode::Remote, "the remote reported an implausible configuration size: %1 bytes",
                         {std::to_string(size)});
    }

    formats::Bytes payload;
    payload.reserve(size);
    while (payload.size() < size) {
        auto chunk = Exchange::request(*channel_, Command::ReadRegionData, {{kRegion}}, timings_.reply);
        if (!chunk) {
            return std::unexpected(std::move(chunk.error()));
        }
        if (chunk->params.size() < 3) {
            return makeError(ErrorCode::Remote, "the remote sent an incomplete configuration chunk");
        }
        const std::size_t length = std::min<std::size_t>(usbnet::toNumber(chunk->params[2]), chunk->params[1].size());
        if (length == 0) {
            return makeError(ErrorCode::Remote, "the remote sent an empty configuration chunk");
        }
        payload.insert(payload.end(), chunk->params[1].begin(),
                       chunk->params[1].begin() + static_cast<std::ptrdiff_t>(length));
        if (!report(progress, Stage::Read, payload.size(), size)) {
            (void)Exchange::request(*channel_, Command::ReadRegionDone, {{kRegion}}, timings_.reply);
            return makeError(ErrorCode::Cancelled, "reading cancelled");
        }
    }
    if (auto done = Exchange::request(*channel_, Command::ReadRegionDone, {{kRegion}}, timings_.reply); !done) {
        return std::unexpected(std::move(done.error()));
    }
    payload.resize(size);
    info_.configSize = static_cast<std::uint32_t>(size);

    // Тот же заголовок, что у резервной копии concordance.
    formats::EzHex container;
    container.identity = info_.identity;
    container.payload = std::move(payload);
    return formats::writeEzHex(container);
}

Result<void> Harmony900Link::writeConfig(const std::vector<std::uint8_t>& ezhex, const ProgressHandler& progress) {
    if (!channel_) {
        return makeError(ErrorCode::Io, "the remote is not connected");
    }
    auto container = formats::parseEzHex(ezhex);
    if (!container) {
        return std::unexpected(std::move(container.error()));
    }
    // Чужая конфигурация может сделать пульт неработоспособным — сверяем модель до записи.
    if (!container->identity.sameModel(info_.identity)) {
        const std::string model = modelName(container->identity.skin).empty()
                                      ? "skin " + std::to_string(container->identity.skin)
                                      : modelName(container->identity.skin);
        return makeError(ErrorCode::Unsupported, "configuration for a different remote (%1), but %2 is connected",
                         {model, info_.model});
    }
    const formats::Bytes& payload = container->payload;
    if (payload.size() < 4 || payload[0] != 'P' || payload[1] != 'K') {
        return makeError(ErrorCode::Format, "the .EZHex contains no configuration archive");
    }

    Channel& channel = *channel_;
    const auto request = [&](Command command, std::vector<Param> params) {
        return Exchange::request(channel, command, std::move(params), timings_.reply);
    };
    if (auto r = request(Command::StartUpdate, {{0x00}, {kRegion}}); !r) {
        return std::unexpected(std::move(r.error()));
    }
    if (auto r = request(Command::WriteUpdateHeader, {usbnet::number(static_cast<std::uint32_t>(payload.size()), 4), {kRegion}}); !r) {
        return std::unexpected(std::move(r.error()));
    }

    // Данные — порциями по 1 КБ; поле данных всегда 1 КБ, фактическая длина — отдельным параметром.
    for (std::size_t offset = 0; offset < payload.size(); offset += usbnet::kChunkSize) {
        const std::size_t length = std::min(usbnet::kChunkSize, payload.size() - offset);
        Param data(usbnet::kChunkSize, 0);
        std::copy_n(payload.begin() + static_cast<std::ptrdiff_t>(offset), length, data.begin());
        if (auto r = request(Command::WriteUpdateData, {{kRegion}, std::move(data), usbnet::number(static_cast<std::uint32_t>(length), 4)}); !r) {
            return std::unexpected(std::move(r.error()));
        }
        if (!report(progress, Stage::Write, offset + length, payload.size())) {
            disconnect(); // без FINISH_UPDATE пульт не применяет недописанное
            return makeError(ErrorCode::Cancelled, "the write was interrupted before completion; the remote should keep "
                                                   "its previous configuration");
        }
    }
    report(progress, Stage::Verify, 0, 3);
    if (auto r = request(Command::WriteUpdateDataDone, {{kRegion}}); !r) {
        return std::unexpected(std::move(r.error()));
    }
    // Формат ответа (алгоритм суммы, начальное 0xFFFF) пока не известен, поэтому сумма не
    // сверяется, а сохраняется для журнала — по нескольким записям алгоритм можно будет подобрать.
    auto checksum = request(Command::GetUpdateChecksum, {{0xFF, 0xFF}, {kRegion}});
    if (!checksum) {
        return std::unexpected(std::move(checksum.error()));
    }
    updateChecksum_ = checksum->params;
    if (auto r = request(Command::FinishUpdate, {{0x01}, {kRegion}}); !r) {
        return std::unexpected(std::move(r.error()));
    }
    report(progress, Stage::Verify, 3, 3);

    // Перезапуск: пульт устанавливает конфигурацию и пропадает из сети на десятки секунд.
    report(progress, Stage::Restart, 0, static_cast<std::size_t>(timings_.restartAttempts));
    if (auto bytes = usbnet::encode({.command = Command::ZReset, .request = true, .status = 0, .params = {}, .lengthCodes = {}})) {
        (void)channel.send(*bytes);
        (void)usbnet::receive(channel, std::chrono::milliseconds(500)); // ответа может и не быть
    }
    disconnect();
    bool back = false;
    for (int attempt = 1; attempt <= timings_.restartAttempts && !back; ++attempt) {
        wait(timings_.restartPoll);
        report(progress, Stage::Restart, static_cast<std::size_t>(attempt), static_cast<std::size_t>(timings_.restartAttempts));
        if (openChannel()) {
            if (auto info = identify()) {
                info_ = *info;
                back = true;
            } else {
                disconnect();
            }
        }
    }
    if (!back) {
        return makeError(ErrorCode::Io, "configuration written, but the remote did not come back after "
                                        "restarting — reconnect it and check");
    }

    // Часы: пульт берёт время с компьютера.
    report(progress, Stage::SetTime, 0, 1);
    const std::time_t now = std::time(nullptr);
    std::tm local{};
#ifdef _WIN32
    localtime_s(&local, &now);
#else
    localtime_r(&now, &local);
#endif
    const auto byte = [](int value) { return Param{static_cast<std::uint8_t>(value)}; };
    std::vector<Param> time = {usbnet::number(static_cast<std::uint32_t>(local.tm_year + 1900), 2),
                               byte(local.tm_mon + 1), byte(local.tm_mday), byte(local.tm_hour),
                               byte(local.tm_min), byte(local.tm_sec), byte(local.tm_wday),
                               {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0, 0}};
    std::vector<std::uint8_t> codes = {0x02, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x02, 0x02, 0x02, 0x02, 0x83};
    if (auto r = Exchange::request(*channel_, Command::UpdateTime, std::move(time), timings_.reply, std::move(codes)); !r) {
        return wrapError(ErrorCode::Remote, r.error(), "configuration written, but setting the clock failed");
    }
    report(progress, Stage::SetTime, 1, 1);
    return {};
}

Result<ir::IrSignal> Harmony900Link::learnIr(const ProgressHandler& progress, std::chrono::seconds timeout) {
    if (!channel_) {
        return makeError(ErrorCode::Io, "the remote is not connected");
    }
    using Clock = std::chrono::steady_clock;
    const auto started = Clock::now();
    const auto elapsed = [&] {
        return static_cast<std::size_t>(std::chrono::duration_cast<std::chrono::seconds>(Clock::now() - started).count());
    };
    const auto total = static_cast<std::size_t>(timeout.count());

    // Заход: START, порции SINGLE до «конца», DONE. Пустой результат — сигнала не было.
    const auto attempt = [&]() -> Result<std::vector<std::uint16_t>> {
        if (auto r = Exchange::request(*channel_, Command::LearnIrStart, {{0x00}}, timings_.reply); !r) {
            return wrapError(ErrorCode::Remote, r.error(), "could not start IR learning");
        }
        std::vector<std::uint16_t> words;
        Result<void> outcome;
        for (bool more = true; more;) {
            auto bytes = usbnet::encode({.command = Command::LearnIrSingle, .request = true, .status = 0, .params = {}, .lengthCodes = {}});
            if (!bytes) {
                outcome = std::unexpected(std::move(bytes.error()));
                break;
            }
            if (auto sent = channel_->send(*bytes); !sent) {
                outcome = std::unexpected(std::move(sent.error()));
                break;
            }
            auto reply = usbnet::receive(*channel_, timings_.reply);
            if (!reply) {
                outcome = wrapError(ErrorCode::Remote, reply.error(), "could not receive an IR signal chunk");
                break;
            }
            if (reply->command != Command::LearnIrSingle || reply->status == usbnet::kStatusTimeout) {
                words.clear(); // за это время пульт сигнала не увидел
                break;
            }
            if (reply->status != usbnet::kStatusOk || reply->params.size() < 5) {
                outcome = makeError(ErrorCode::Remote, "the remote sent an unexpected signal chunk");
                break;
            }
            for (std::size_t i = 1; i < reply->params.size(); ++i) {
                words.push_back(static_cast<std::uint16_t>(usbnet::toNumber(reply->params[i])));
            }
            more = !reply->params[0].empty() && reply->params[0][0] != 0;
        }
        // DONE — всегда: иначе пульт останется в режиме обучения.
        (void)Exchange::request(*channel_, Command::LearnIrDone, {}, timings_.reply);
        if (!outcome) {
            return std::unexpected(std::move(outcome.error()));
        }
        return words;
    };

    while (elapsed() < total) {
        if (!report(progress, Stage::Learn, elapsed(), total)) {
            return makeError(ErrorCode::Cancelled, "learning cancelled");
        }
        auto words = attempt();
        if (!words) {
            return std::unexpected(std::move(words.error()));
        }
        // Первая порция: слова 0 и 2 — служебные, по ним считается несущая
        // (число периодов несущей за первый импульс / его длительность).
        if (words->size() >= 4 && (*words)[1] > 0) {
            const std::uint16_t firstMark = (*words)[1];
            const auto frequency = static_cast<std::uint32_t>(static_cast<double>((*words)[2]) * 1e6 / firstMark);
            std::vector<std::uint16_t> markSegments = {firstMark};
            markSegments.insert(markSegments.end(), words->begin() + 3, words->end());
            ir::IrSignal signal = ir::fromMarkSegments(frequency, markSegments);
            if (!signal.durations.empty()) {
                report(progress, Stage::Learn, total, total);
                return signal;
            }
        }
    }
    return makeError(ErrorCode::Cancelled, "no signal — point the original remote at the Harmony and press a "
                                           "button");
}

} // namespace harmony::transport
