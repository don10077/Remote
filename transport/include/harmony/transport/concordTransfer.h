#pragma once

#include "remoteLink.h"
#include "harmony/core/formats/remoteCapture.h"
#include "harmony/core/remoteCatalog.h"
#include <filesystem>
#include <memory>
#include <span>

namespace harmony::transport {

/// Назначение исходного образа: пульт из каталога либо периферийное устройство.
/// Не даёт права редактировать конфигурацию и не добавляет периферию в каталог пультов.
struct TransferTarget {
    std::string_view id;
    std::string_view name;
    std::span<const int> skins;
    LinkKind link = LinkKind::None;
    bool readable = false;
    bool writable = false;
    int architecture = 0; ///< Для периферии: обязательная архитектура; 0 — проверяется транспортом.
    int containerSlots = 0; ///< Для периферии: обязательный вариант контейнера.
};

/// @return Назначение по id (включая auto и c-re4a); пусто для неизвестного id.
std::optional<TransferTarget> transferTarget(std::string_view id);
/// @return Назначение по skin; неизвестные устройства не получают права чтения или записи.
std::optional<TransferTarget> transferTargetBySkin(int skin);

/// Готовый файл конфигурации; внутренний формат не требует поддержки редактором.
struct TransferFile {
    formats::Bytes payload;
    std::string xml;
    int protocol = -1;
    int skin = -1;
    bool mh = false;
};

/// Проверяет контейнер, назначение и checksum без обращения к пульту.
/// Принимает и снятый файл *.remote.json: проверяется контейнер внутри него.
Result<TransferFile> inspectTransferFile(std::span<const std::uint8_t> bytes);

/**
 * @brief Достаёт из снятого файла *.remote.json контейнер для пульта (EZHex или ZIP MH).
 * @param bytes Содержимое файла.
 * @return Контейнер; любой другой файл возвращается как есть. ErrorCode::Unsupported —
 *         старый *.hid.json (образ без контейнера записать нельзя), ErrorCode::Format — файл испорчен.
 */
Result<formats::Bytes> unwrapCapture(std::span<const std::uint8_t> bytes);

/// Итог проверки снятого файла.
struct CaptureCheck {
    TransferFile file;         ///< Разобранный контейнер
    bool checksum = false;     ///< Сошлась сумма контейнера (CHECKSUM EZHex или XOR16 MH)
    bool frame = false;        ///< Проверена рамка образа: признак GSPM, конец, PTYY, XOR16 (One, 600–700)
};

/**
 * @brief Проверяет снятую конфигурацию без пульта: контейнер, его сумму, совпадение
 * пульта в контейнере со сведениями о снятии и, где формат известен, рамку образа.
 * @param capture Разобранный *.remote.json (целостность Base64/SHA-256 уже проверена разбором).
 * @return Что проверено; ошибка — с причиной. Старый *.hid.json (Raw) — ErrorCode::Unsupported.
 */
Result<CaptureCheck> inspectCapture(const formats::RemoteCapture& capture);

/// Состояние транспорта, полученное от настоящего пульта, а не из фотографии.
struct TransferInfo {
    std::string modelId;       ///< id назначения транспорта (пульт либо периферия); auto определяет его по skin.
    RemoteInfo remote;
    int protocol = -1;
    bool mh = false;
    bool network = false;
    bool readable = false;
    bool writable = false;
    std::uint32_t capacity = 0;
    int containerSlots = 0;    ///< Обязательное число слотов образа периферии; 0 — не ограничено.
};

/// Проверяет совместимость готового файла с устройством до любых команд записи.
Result<void> validateTransfer(const TransferFile& file, const TransferInfo& info);

/**
 * @brief Собирает снятый файл из прочитанного ConcordTransfer::read().
 * @param info Сведения подключённого пульта (connect()).
 * @param data Контейнер, как его вернул read().
 * @param now Момент снятия.
 */
formats::RemoteCapture makeCapture(const TransferInfo& info, formats::Bytes data,
                                   std::chrono::system_clock::time_point now = std::chrono::system_clock::now());

/// Сеанс конфигурационного транспорта. libconcord имеет глобальное состояние:
/// одновременно разрешён один сеанс; класс предназначен для рабочего потока.
class ConcordTransfer {
public:
    ConcordTransfer();
    ~ConcordTransfer();
    ConcordTransfer(const ConcordTransfer&) = delete;
    ConcordTransfer& operator=(const ConcordTransfer&) = delete;

    /// Подключает назначение транспорта и сверяет фактический skin; неизвестный id отклоняется.
    Result<TransferInfo> connect(std::string_view modelId, const Endpoint& endpoint = {});
    /// Читает исходный контейнер EZHex либо ZIP MH; не преобразует внутренние данные.
    Result<formats::Bytes> read(const ProgressHandler& progress = {});
    /// Проверяет файл, сохраняет резервную копию и записывает конфигурацию.
    /// @p file — EZHex, ZIP MH или снятый *.remote.json (пишется контейнер из него).
    /// allowWithoutBackup применим только к моделям с известным отсутствием readback.
    /// После начала стирания отмена не выполняется; firmware API не вызываются.
    Result<void> write(std::span<const std::uint8_t> file, const std::filesystem::path& backup,
                       bool allowWithoutBackup = false, const ProgressHandler& progress = {});

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace harmony::transport
