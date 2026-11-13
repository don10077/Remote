#pragma once

/**
 * @file learnWizardDialog.h
 * @brief Мастер «Обучить команды»: список команд и обучение одна за другой.
 *
 * Единственное окно обучения: и для одной команды, и для многих подряд.
 * Сначала отмечаются команды (по умолчанию — привычные, которых у устройства
 * ещё нет; выделенные в таблице — сразу; можно добавить свои). Потом мастер сам слушает пульт для каждой по
 * очереди: нажали кнопку родного пульта — команда сохранена, следующая. С
 * проверкой каждую кнопку нажимают дважды: сохраняется, только если оба
 * нажатия дали тот же сигнал (защита от помех и промахов).
 */

#include "harmony/core/ir/irSignal.h"
#include "harmony/core/model/remoteConfig.h"

#include <QDialog>
#include <QStringList>

#include <optional>

class QCheckBox;
class QLabel;
class QLineEdit;
class QListWidget;
class QPushButton;
class QStackedWidget;

namespace harmony::app {

class RemoteSession;


/// Привычные имена команд (PowerToggle, VolumeUp, 0…9…): по ним пульт сам назначает кнопки и питание.
const QStringList& standardCommandNames();

/// Обучение команд: одна или много подряд.
class LearnWizardDialog final : public QDialog {
    Q_OBJECT

public:
    /**
     * @param session Связь с пультом (живёт дольше окна).
     * @param device Устройство.
     * @param parent Родительское окно.
     */
    LearnWizardDialog(RemoteSession* session, const model::Device& device, QWidget* parent = nullptr);
    ~LearnWizardDialog() override;

    /// Отмечает команды по списку (выделенные в таблице — переучить): остальные снимаются.
    void markOnly(const QStringList& names);

signals:
    /// Команда выучена (каждая — сразу, отдельным шагом отмены).
    void commandLearned(const QString& name, const harmony::ir::IrSignal& signal);

protected:
    void reject() override;

private:
    /// Что сделать, когда слушание прервано по просьбе пользователя.
    enum class After { Nothing, Repeat, Next, Back, Finish };

    void buildChoosePage();
    void buildLearnPage();
    void buildDonePage();

    /// Отмечает: недостающие / все / ничего.
    void mark(bool missingOnly, bool all);
    void updateChooseState();

    /// Начинает обучение отмеченных.
    void start();

    /// Показывает текущую команду и начинает слушать.
    void showCurrent();

    /// Слушает, как только пульт освободится.
    void listenSoon();

    /// Прерывает слушание и затем делает @p after (сразу, если не слушаем).
    void interrupt(After after);
    void perform(After after);

    void received(const ir::IrSignal& signal);
    void save(const ir::IrSignal& signal);
    void finish();
    void updateLearnButtons();

    RemoteSession* session_;
    QStringList existing_; ///< Команды, которые у устройства уже есть
    QStringList queue_;    ///< Что учить, по порядку
    int index_ = 0;
    int learned_ = 0;
    int skipped_ = 0;
    bool listening_ = false;
    bool alive_ = true; ///< Окно не закрывается (иначе слушать заново не надо)
    After after_ = After::Nothing;
    std::optional<ir::IrSignal> first_; ///< Первое нажатие (при проверке)

    QStackedWidget* pages_ = nullptr;
    // Выбор команд
    QListWidget* list_ = nullptr;
    QLineEdit* own_ = nullptr;
    QCheckBox* verify_ = nullptr;
    QLabel* chosen_ = nullptr;
    QPushButton* start_ = nullptr;
    // Обучение
    QLabel* step_ = nullptr;
    QLabel* command_ = nullptr;
    QLabel* status_ = nullptr;
    QLabel* view_ = nullptr;
    QPushButton* back_ = nullptr;
    QPushButton* repeat_ = nullptr;
    QPushButton* skip_ = nullptr;
    QPushButton* stop_ = nullptr;
    // Итог
    QLabel* summary_ = nullptr;
};

} // namespace harmony::app
