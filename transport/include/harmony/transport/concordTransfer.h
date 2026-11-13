#pragma once

#include "remoteLink.h"
#include <filesystem>
#include <memory>
#include <span>

namespace harmony::transport {

/// Готовый файл конфигурации; внутренний формат не требует поддержки редактором.
struct TransferFile {
    formats::Bytes payload;
    std::string xml;
    int protocol = -1;
    int skin = -1;
    bool mh = false;
};

/// Проверяет контейнер, назначение и checksum без обращения к пульту.
Result<TransferFile> inspectTransferFile(std::span<const std::uint8_t> bytes);

/// Состояние транспорта, полученное от настоящего пульта, а не из фотографии.
struct TransferInfo {
    RemoteInfo remote;
    int protocol = -1;
    bool mh = false;
    bool network = false;
    bool readable = false;
    bool writable = false;
    std::uint32_t capacity = 0;
};

/// Проверяет совместимость готового файла с устройством до любых команд записи.
Result<void> validateTransfer(const TransferFile& file, const TransferInfo& info);

/// Сеанс конфигурационного транспорта. libconcord имеет глобальное состояние:
/// одновременно разрешён один сеанс; класс предназначен для рабочего потока.
class ConcordTransfer {
public:
    ConcordTransfer();
    ~ConcordTransfer();
    ConcordTransfer(const ConcordTransfer&) = delete;
    ConcordTransfer& operator=(const ConcordTransfer&) = delete;

    /// Подключает выбранную модель и сверяет фактический skin; неизвестный id отклоняется.
    Result<TransferInfo> connect(std::string_view modelId, const Endpoint& endpoint = {});
    /// Читает исходный контейнер EZHex либо ZIP MH; не преобразует внутренние данные.
    Result<formats::Bytes> read(const ProgressHandler& progress = {});
    /// Проверяет файл, сохраняет резервную копию и записывает конфигурацию.
    /// allowWithoutBackup применим только к моделям с известным отсутствием readback.
    /// После начала стирания отмена не выполняется; firmware API не вызываются.
    Result<void> write(std::span<const std::uint8_t> file, const std::filesystem::path& backup,
                       bool allowWithoutBackup = false, const ProgressHandler& progress = {});

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace harmony::transport
