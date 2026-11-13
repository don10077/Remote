#pragma once

/**
 * @file harmony900Link.h
 * @brief Связь с Harmony 900 (и пультами того же семейства) по USB-сети.
 *
 * Чтение конфигурации: READ_REGION (размер) → READ_REGION_DATA порциями → READ_REGION_DONE.
 * Запись: START_UPDATE → WRITE_UPDATE_HEADER (размер) → WRITE_UPDATE_DATA порциями по 1 КБ
 * → WRITE_UPDATE_DATA_DONE → GET_UPDATE_CHECKSUM → FINISH_UPDATE; затем перезапуск пульта
 * (он устанавливает конфигурацию), повторное подключение и установка часов.
 * Обучение: LEARNIR_START → LEARNIR_SINGLE порциями по 4 слова, пока пульт не скажет
 * «конец» → LEARNIR_DONE.
 */

#include "harmony/transport/channel.h"
#include "harmony/transport/remoteLink.h"

#include <chrono>
#include <memory>

namespace harmony::transport {

/// Тайминги обмена (в тестах — короткие).
struct LinkTimings {
    std::chrono::milliseconds connect{2000};      ///< Подключение
    std::chrono::milliseconds reply{10000};       ///< Ответ на команду
    std::chrono::milliseconds restartPoll{3000};  ///< Пауза между попытками после перезапуска
    int restartAttempts = 30;                     ///< Сколько попыток (≈ полторы минуты)
};

/// Связь с Harmony 900.
class Harmony900Link final : public RemoteLink {
public:
    /**
     * @brief Создаёт связь.
     * @param factory Как подключаться; пусто — TCP к адресу из connect().
     * @param timings Тайминги.
     */
    explicit Harmony900Link(ChannelFactory factory = {}, LinkTimings timings = {});
    ~Harmony900Link() override;

    [[nodiscard]] Result<RemoteInfo> connect(const Endpoint& endpoint) override;
    void disconnect() override;
    [[nodiscard]] bool isConnected() const override { return channel_ != nullptr; }
    [[nodiscard]] Result<std::vector<std::uint8_t>> readConfig(const ProgressHandler& progress) override;
    [[nodiscard]] Result<void> writeConfig(const std::vector<std::uint8_t>& ezhex,
                                           const ProgressHandler& progress) override;
    [[nodiscard]] Result<ir::IrSignal> learnIr(const ProgressHandler& progress,
                                               std::chrono::seconds timeout) override;

    /// @return Сведения о пульте с последнего подключения.
    [[nodiscard]] const RemoteInfo& info() const { return info_; }

    /**
     * @brief Ответ пульта на GET_UPDATE_CHECKSUM при последней записи.
     *
     * Алгоритм суммы не известен, поэтому writeConfig() её не сверяет; значение нужно, чтобы
     * подобрать алгоритм по реальным записям и затем включить проверку.
     * @return Параметры ответа как есть; пусто, если записи не было.
     */
    [[nodiscard]] const std::vector<std::vector<std::uint8_t>>& updateChecksum() const { return updateChecksum_; }

private:
    struct Exchange;
    [[nodiscard]] Result<void> openChannel();
    [[nodiscard]] Result<RemoteInfo> identify();

    ChannelFactory factory_;
    LinkTimings timings_;
    Endpoint endpoint_;
    std::unique_ptr<Channel> channel_;
    RemoteInfo info_;
    std::vector<std::vector<std::uint8_t>> updateChecksum_;
};

/**
 * @brief Название модели по skin.
 * @return «Harmony 900», «Harmony 1000», … или пусто, если модель неизвестна.
 */
[[nodiscard]] std::string modelName(int skin);

} // namespace harmony::transport
