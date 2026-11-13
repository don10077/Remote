#pragma once

/**
 * @file findByRemoteDialog.h
 * @brief Окно «Найти модель по пульту»: несколько кнопок родного пульта → модель из базы.
 *
 * Harmony слушает родной пульт кнопка за кнопкой (RemoteSession::learn), каждая
 * кнопка ищется в индексе сигналов базы (irdb::ModelFinder). Список моделей
 * обновляется после каждого нажатия: сверху — узнавшие больше кнопок. Модели
 * с одинаковыми кодами работают одинаково, так что подходит любая из лучших.
 */

#include "harmony/core/irdb/modelFinder.h"

#include <QDialog>
#include <memory>

class QLabel;
class QLineEdit;
class QPushButton;
class QThread;
class QTreeWidget;

namespace harmony::app {

class IrDatabaseService;
class RemoteSession;

/// Поиск модели по кнопкам родного пульта.
class FindByRemoteDialog final : public QDialog {
    Q_OBJECT

public:
    /**
     * @brief Создаёт окно; индекс сигналов загружается в фоне, затем начинается прослушивание.
     * @param session Связь с пультом (живёт дольше окна).
     * @param irDatabase База ИК-кодов (открыта).
     * @param parent Родительское окно.
     */
    FindByRemoteDialog(RemoteSession* session, IrDatabaseService* irDatabase,
                       QWidget* parent = nullptr);
    ~FindByRemoteDialog() override;

    /// @return Выбранная модель: номер в каталоге базы; -1 — не выбрана.
    [[nodiscard]] int entry() const { return entry_; }

protected:
    void reject() override;

private:
    /// Индекс загружен (или нет — @p error).
    void indexLoaded(const QString& error);

    /// Просит пульт выучить следующую кнопку.
    void listen();

    /// Прекращает слушать (отменяет обучение на пульте).
    void stopListening();

    /// Кнопка выучена.
    void signalLearned(const ir::IrSignal& signal);

    /// Обновляет список моделей и сводку.
    void updateResults();

    /// Обновляет надпись о состоянии и кнопки.
    void updateState();

    /// Выбранная модель → итог окна.
    void usePicked();

    RemoteSession* session_;
    IrDatabaseService* irDatabase_;
    std::unique_ptr<irdb::ModelFinder> finder_;
    std::shared_ptr<const irdb::SignalIndex> index_; ///< Загружается в фоне
    QThread* loader_ = nullptr;
    bool listening_ = false; ///< Пользователь хочет, чтобы окно слушало
    bool waiting_ = false; ///< Идёт обучение на пульте
    QString problem_; ///< Последняя ошибка связи (показывается вместо призыва нажать)
    int entry_ = -1;

    QLabel* status_ = nullptr;
    QLabel* summary_ = nullptr;
    QLineEdit* filter_ = nullptr;
    QTreeWidget* list_ = nullptr;
    QPushButton* listen_ = nullptr;
    QPushButton* reset_ = nullptr;
    QPushButton* use_ = nullptr;
};

} // namespace harmony::app
