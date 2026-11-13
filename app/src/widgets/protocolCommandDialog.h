#pragma once

/**
 * @file protocolCommandDialog.h
 * @brief Окно «Команда вручную»: протокол, адрес и код → команда без родного пульта.
 *
 * Пишете «NEC, адрес 0x04, команда 0x08» — окно сразу показывает поля команды
 * и код, как его печатает Arduino IRremote. NEC, Samsung32, Sony, JVC, RC5 и Onkyo
 * сохраняются протоколом в IrProto.bin (пульт сам шлёт повторы и меняет бит
 * переключения), остальные — сигналом, как выученные. Для правки
 * существующей команды поля заполняются тем, что распознано в её сигнале;
 * для новой — протоколом и адресом других команд устройства.
 */

#include "harmony/core/ir/irSignal.h"

#include <QDialog>
#include <optional>

class QComboBox;
class QLabel;
class QLineEdit;
class QPushButton;

namespace harmony::app {

class RemoteSession;

/// Команда по протоколу.
class ProtocolCommandDialog final : public QDialog {
    Q_OBJECT

public:
    /**
     * @param deviceName Имя устройства (для заголовка).
     * @param existing Команды устройства (отмечаются «заменить»).
     * @param name Имя команды (пусто — новая).
     * @param initial Протокол, адрес и команда для начала (распознанные).
     * @param parent Родительское окно.
     * @param session Сеанс считывания команды; nullptr отключает считывание.
     */
    ProtocolCommandDialog(const QString& deviceName, const QStringList& existing,
                          const QString& name, const std::optional<ir::IrDecoded>& initial,
                          QWidget* parent = nullptr, RemoteSession* session = nullptr);

    ~ProtocolCommandDialog() override;

signals:
    /// Сохранить считанный сигнал, включая неизвестные протоколы.
    void signalReady(const QString& name, const harmony::ir::IrSignal& signal);
    /// Команду собрали и нажали «Сохранить» (окно может остаться открытым для следующей).
    void commandReady(const QString& name, const QString& protocol, quint32 address, quint32 command);

private:
    /// Собирает сигнал по полям и показывает его.
    void rebuild();

    /// Сохраняет; @p next — оставить окно для следующей команды.
    void save(bool next);

    [[nodiscard]] QString commandName() const;

    RemoteSession* session_ = nullptr;
    bool listening_ = false;
    std::optional<ir::IrSignal> captured_;
    QPushButton* read_ = nullptr;
    QStringList existing_;
    std::optional<ir::IrSignal> signal_;
    QString protocolName_;      ///< Протокол собранного сигнала
    quint32 addressValue_ = 0;  ///< Адрес собранного сигнала
    quint32 commandValue_ = 0;  ///< Команда собранного сигнала

    QComboBox* name_ = nullptr;
    QComboBox* protocol_ = nullptr;
    QLineEdit* address_ = nullptr;
    QLineEdit* command_ = nullptr;
    QLabel* ranges_ = nullptr;
    QLabel* result_ = nullptr;
    QComboBox* inputMode_ = nullptr;
    QLineEdit* raw_ = nullptr;
    QLineEdit* bits_ = nullptr;
    QPushButton* save_ = nullptr;
    QPushButton* saveNext_ = nullptr;
};

} // namespace harmony::app
