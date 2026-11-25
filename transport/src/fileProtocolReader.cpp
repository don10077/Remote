// SPDX-License-Identifier: GPL-3.0-only
// Пакет, команды и ответы файлового протокола — по данным
// `support/Other project/harmony-explorations` (sections 198, 200, 201). Шаг I9.
#include "harmony/transport/fileProtocolReader.h"

#include "harmony/core/remoteCatalog.h"

#include <algorithm>
#include <cstdio>
#include <string>
#include <vector>

namespace harmony::transport {
namespace {

constexpr std::uint8_t kService = 0xFF;   ///< единственный виденный номер сервиса
constexpr std::uint8_t kPing = 0x00;
constexpr std::uint8_t kOpen = 0x01;
constexpr std::uint8_t kRead = 0x04;
constexpr std::uint8_t kClose = 0x07;

/// На месте номера последовательности 0xFF — признак отказа, и каждый шаблон проверяет именно
/// этот байт. Caller, сверяющий только команду, прочитал бы payload отказа как данные.
constexpr std::uint8_t kErrorMarker = 0xFF;
/// Пакет, которым читатель заканчивает чтение. Последний пакет объявляет полную длину и
/// дополняется NUL, поэтому конец файла говорит только размер, который заявил open.
constexpr std::uint8_t kTerminator = 0xFE;
/// Строка-параметр запроса: признак 0x80, символы, завершающий ноль.
constexpr std::uint8_t kStringPrefix = 0x80;
constexpr std::size_t kPacketsPerRequest = 32;

std::string hexByte(std::uint8_t byte) {
    char text[8]{};
    std::snprintf(text, sizeof(text), "0x%02X", static_cast<unsigned>(byte));
    return text;
}

/// Запрос: сервис, команда, номер последовательности (шаблоны всегда ставят 0), число параметров.
Result<std::array<std::uint8_t, 64>> buildRequest(std::uint8_t command,
                                                   const std::vector<std::vector<std::uint8_t>>& params) {
    std::array<std::uint8_t, 64> report{};
    report[0] = kService;
    report[1] = command;
    report[2] = 0;
    report[3] = static_cast<std::uint8_t>(params.size());
    std::size_t at = 4;
    for (const auto& param : params) {
        for (auto byte : param) {
            if (at >= report.size())
                return makeError(ErrorCode::Format, "Parameters do not fit in one 64-byte report");
            report[at++] = byte;
        }
    }
    return report;
}

/// Параметр запроса: строка — признак и символы до NUL, число — длина и сами байты.
std::vector<std::uint8_t> stringParam(std::string_view value) {
    std::vector<std::uint8_t> param{kStringPrefix};
    for (char c : value) param.push_back(static_cast<std::uint8_t>(c));
    param.push_back(0);
    return param;
}

std::vector<std::uint8_t> byteParam(std::uint8_t value) { return {1, value}; }

/// Параметры ответа идут с длиной: счётчик на offsets[3], затем длина и bytes. Отказ — один
/// байт, значение которого никто не прочитал, поэтому он называется как есть.
std::vector<std::vector<std::uint8_t>> replyParams(const std::vector<std::uint8_t>& reply) {
    std::vector<std::vector<std::uint8_t>> params;
    if (reply.size() < 4) return params;
    std::size_t at = 4;
    for (std::size_t i = 0; i < static_cast<std::size_t>(reply[3]) && at < reply.size(); ++i) {
        const std::size_t length = reply[at++];
        std::vector<std::uint8_t> value;
        for (std::size_t n = 0; n < length && at < reply.size(); ++n) value.push_back(reply[at++]);
        params.push_back(std::move(value));
    }
    return params;
}

/// Проверяет, что ответ относится к отправленной команде и не является отказом, и возвращает
/// его параметры. Отказ — ошибка, а не payload.
Result<std::vector<std::vector<std::uint8_t>>> checkReply(const std::vector<std::uint8_t>& reply,
                                                           const std::string& what, std::uint8_t command) {
    if (reply.size() < 4)
        return makeError(ErrorCode::Format, "%1 reply is shorter than its header", {what});
    if (reply[0] != kService)
        return makeError(ErrorCode::Remote, "%1 reply names another service", {what});
    if (reply[1] != command)
        return makeError(ErrorCode::Remote, "%1 reply answers another command", {what});
    const auto params = replyParams(reply);
    if (reply[2] != kErrorMarker) return params;
    const std::string code = (params.empty() || params.front().empty())
        ? "no code" : hexByte(params.front().front());
    return makeError(ErrorCode::Remote, "The remote refused %1 (the answer carries %2)",
                     {what, code});
}

} // namespace

Result<FileContent> FileProtocolReader::read(std::string_view path, const ProgressHandler& progress) {
    const auto* model = harmony::catalogModel(modelId_);
    if (!model) return makeError(ErrorCode::Unsupported, "Unknown model %1", {modelId_});
    const auto* family = harmony::remoteFamily(model->architecture);
    if (!family)
        return makeError(ErrorCode::Unsupported,
                         "Model %1 has no family description, so no file is confirmed for it on the file protocol",
                         {std::string(model->name)});
    const auto* entry = harmony::answerableFile(model->architecture, path);
    if (!entry)
        return makeError(ErrorCode::Unsupported,
                         "Model %1 is not confirmed to answer %2 on the file protocol",
                         {std::string(model->name), std::string(path)});
    if (!harmony::confirmedOn(*entry, model->id)) {
        std::string confirmed;
        for (const auto& id : entry->confirmedOn) {
            const auto* source = harmony::catalogModel(id);
            if (!confirmed.empty()) confirmed += ", ";
            confirmed += source ? std::string(source->name) : std::string(id);
        }
        return makeError(ErrorCode::Unsupported, "%1 is confirmed on %2, not on %3",
                         {std::string(path), confirmed, std::string(model->name)});
    }
    if (progress && !progress(Stage::Connect, 0, 0))
        return makeError(ErrorCode::Cancelled, "Reading cancelled");

    // Клиентский порядок: перед файловой операцией — ping. Форма без параметров: с параметром
    // это уже другое (комментарий клиента называет его имитацией USB-сброса), и читатель такой
    // формы не отправляет.
    auto ping = buildRequest(kPing, {});
    if (!ping) return std::unexpected(ping.error());
    if (auto sent = channel_.send(ping.value()); !sent) return std::unexpected(sent.error());
    auto reply = channel_.receive();
    if (!reply) return std::unexpected(reply.error());
    if (auto checked = checkReply(*reply, "ping", kPing); !checked)
        return std::unexpected(checked.error());

    // Режим чтения: на этом протоколе путь может быть действием, поэтому читатель не просит
    // запись ни для какого пути.
    auto openRequest = buildRequest(kOpen, {stringParam(path), stringParam("R")});
    if (!openRequest) return std::unexpected(openRequest.error());
    if (auto sent = channel_.send(openRequest.value()); !sent) return std::unexpected(sent.error());
    reply = channel_.receive();
    if (!reply) return std::unexpected(reply.error());
    auto params = checkReply(*reply, std::string(path), kOpen);
    if (!params) return std::unexpected(params.error());
    if (params->size() < 2 || params->front().size() != 1 || params->back().size() != 4)
        return makeError(ErrorCode::Format, "Open reply does not carry a handle and a size");

    FileContent content{.modelId = modelId_, .path = std::string(path), .shape = entry->shape};
    const std::uint8_t handle = params->front().front();
    const auto& stated = params->back();
    content.statedSize = (static_cast<std::size_t>(stated[0]) << 24) |
                         (static_cast<std::size_t>(stated[1]) << 16) |
                         (static_cast<std::size_t>(stated[2]) << 8) |
                         static_cast<std::size_t>(stated[3]);

    // Первый ответ на read — эхо заголовка запроса; данные идут пакетами после него.
    bool terminated = false;
    while (!terminated && content.bytes.size() < content.statedSize) {
        auto request = buildRequest(kRead, {byteParam(handle),
                                            byteParam(static_cast<std::uint8_t>(kPacketsPerRequest))});
        if (!request) return std::unexpected(request.error());
        if (auto sent = channel_.send(request.value()); !sent) return std::unexpected(sent.error());
        auto header = channel_.receive();
        if (!header) return std::unexpected(header.error());
        if (auto checked = checkReply(*header, "read", kRead); !checked)
            return std::unexpected(checked.error());
        for (std::size_t packet = 0; packet < kPacketsPerRequest; ++packet) {
            auto report = channel_.receive();
            if (!report) return std::unexpected(report.error());
            if (report->empty()) continue;
            if ((*report)[0] == kTerminator) { terminated = true; break; }
            if (report->size() < 2)
                return makeError(ErrorCode::Format, "Data packet is shorter than its header");
            const std::size_t length = std::min<std::size_t>((*report)[1], report->size() - 2);
            content.bytes.insert(content.bytes.end(), report->begin() + 2,
                                 report->begin() + 2 + static_cast<std::ptrdiff_t>(length));
        }
        if (progress && !progress(Stage::Read, content.bytes.size(), content.statedSize))
            return makeError(ErrorCode::Cancelled, "Reading cancelled");
    }

    // Размер, который заявил open, — единственный конец файла: пакеты дополняются NUL. Короткий
    // ответ остаётся коротким, потому что это факт о передаче, а не о файле.
    if (content.bytes.size() > content.statedSize) content.bytes.resize(content.statedSize);
    content.complete = content.bytes.size() == content.statedSize;

    // Файл закрывается после каждого чтения: open отказывает, пока другой файл открыт.
    auto closeRequest = buildRequest(kClose, {byteParam(handle)});
    if (!closeRequest) return std::unexpected(closeRequest.error());
    if (auto sent = channel_.send(closeRequest.value()); !sent) return std::unexpected(sent.error());
    reply = channel_.receive();
    if (!reply) return std::unexpected(reply.error());
    if (auto checked = checkReply(*reply, "close", kClose); !checked)
        return std::unexpected(checked.error());

    return content;
}

} // namespace harmony::transport
