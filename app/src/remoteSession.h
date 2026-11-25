#pragma once

/**
 * @file remoteSession.h
 * @brief Работа с пультом из окна: проверка подключения, чтение и запись в фоновом потоке.
 *
 * Обмен с пультом занимает от секунды (опознание) до минуты-двух (запись и
 * перезапуск), поэтому идёт в отдельном потоке, а окно получает прогресс и
 * итог сигналами. Одновременно выполняется только одна операция.
 */

#include "harmony/core/modelInfo.h"
#include "harmony/transport/linkFactory.h"
#include "harmony/transport/remoteLink.h"

#include <QObject>
#include <QString>

#include <atomic>
#include <chrono>
#include <cstdint>
#include <functional>
#include <memory>
#include <vector>

class QThread;

namespace harmony::app {

/// Операции с пультом для главного окна.
class RemoteSession final : public QObject {
    Q_OBJECT

public:
    explicit RemoteSession(QObject* parent = nullptr);
    ~RemoteSession() override;

    /// @return true, пока идёт операция.
    [[nodiscard]] bool busy() const { return thread_ != nullptr; }

    /// @return true, если занята только фоновой проверкой подключения (закончится за секунды).
    [[nodiscard]] bool probing() const { return busy() && probing_; }

    /// Тихо проверяет, подключён ли пульт (результат — stateChanged).
    void probe();

    /**
     * @brief Модель документа: по ней фабрика связи решает, чем подключаться (U4b).
     * @param model Сводка модели (`modelInfo(id)`); модель не выбрана — связь выбирается
     *        по подключённому железу (путь `connect("auto")`, решение владельца 23.11.2026).
     */
    void setModel(const harmony::ModelInfo& model) { model_ = model; }

    /**
     * @brief Читает любой читаемый пульт (модель определяется сама) в один файл *.remote.json
     * и проверяет его (итог — captureReadFinished или failed).
     */
    void readCapture();

    /**
     * @brief Первый шаг записи: читает конфигурацию с пульта и сохраняет её резервной копией
     * (итог — backupSaved или failed). Дальше окно показывает, что изменится, и зовёт
     * writeConfig() без повторного чтения.
     * @param backupFile Куда сохранить.
     */
    void prepareWrite(QString backupFile);

    /**
     * @brief Записывает конфигурацию; перед этим сохраняет резервную копию с пульта.
     * @param ezhex Конфигурация для пульта (ConfigDocument::toEzHex(true)).
     * @param backupFile Куда сохранить текущую конфигурацию пульта перед записью;
     *        пусто — копия уже сохранена (prepareWrite()), сразу запись.
     */
    void writeConfig(std::vector<std::uint8_t> ezhex, QString backupFile);

    /**
     * @brief Учит ИК-команду с родного пульта (итог — learnFinished или failed).
     * @param seconds Сколько ждать нажатия.
     */
    void learn(int seconds = 30);

    /// Просит отменить операцию (там, где это ещё безопасно).
    void cancel() { cancelled_ = true; }

signals:
    /// Пульт найден или пропал; @p text — «Harmony 900 · прошивка 5.9» или «Пульт не подключён».
    void stateChanged(bool connected, const QString& text);
    /// Ход операции: этап и выполнено из всего.
    void progress(harmony::transport::Stage stage, qint64 done, qint64 total);
    /// Снятый файл *.remote.json (контейнер, сведения о пульте, SHA-256) уже проверен:
    /// @p modelId — id модели, @p check — что именно сошлось (для окна и журнала).
    void captureReadFinished(const QByteArray& json, const QString& modelId, const QString& check);
    /// Резервная копия сохранена (prepareWrite()): содержимое .EZHex с пульта и файл.
    void backupSaved(const QByteArray& ezhex, const QString& backupFile);
    /// Конфигурация записана; @p backupFile — где резервная копия прежней.
    void writeFinished(const QString& backupFile);
    /// Сигнал выучен.
    void learnFinished(const harmony::ir::IrSignal& signal);
    /// Операция не удалась (или отменена — @p cancelled).
    void failed(const QString& message, bool cancelled);
    /// Операция закончилась, можно начинать следующую.
    void idle();

private:
    /// Запускает работу в фоновом потоке (по завершении поток удаляется).
    void run(std::function<void()> work);

    /// Обработчик прогресса для transport: сигнал в окно и проверка отмены.
    [[nodiscard]] transport::ProgressHandler progressHandler();

    /// Подключается и сообщает окну состояние пульта.
    Result<transport::RemoteInfo> connectAndReport(transport::RemoteLink& link);

    /// Связь для модели документа: решение принимает фабрика связи (U4b).
    [[nodiscard]] Result<transport::LinkChoice> openLink(std::chrono::milliseconds connectTimeout = {});

    /// Текст отказа для модели, которая подключается не по USB-сети.
    [[nodiscard]] QString transferText() const;

    /// Сообщает окну состояние пульта из фонового потока.
    void reportState(bool connected, const QString& text);
    /// Сообщает об ошибке из фонового потока.
    void reportFailure(const QString& message, bool cancelled);
    void reportFailure(const Error& error);

    QThread* thread_ = nullptr;
    harmony::ModelInfo model_;             ///< модель документа (setModel)
    std::atomic<bool> cancelled_{false};
    bool probing_ = false;       ///< Идёт проверка подключения (probe())
    bool lastConnected_ = false; ///< Последнее известное состояние (для журнала)
    QString lastStateText_; ///< Подробности последнего состояния, без повторов в журнале.

public:
    /// @return Подробности последней проверки связи (обновляются в потоке интерфейса).
    [[nodiscard]] QString connectionStatus() const { return lastStateText_; }
};

} // namespace harmony::app
