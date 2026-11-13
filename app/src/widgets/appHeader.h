#pragma once

/**
 * @file appHeader.h
 * @brief Шапка окна: документ и отмена слева, состояние пульта и запись справа.
 *
 * Заменяет строку меню и панель инструментов. Устроена по смыслу:
 *  - слева — открытая конфигурация (её имя открывает меню файла), отмена и повтор;
 *  - справа — пульт: одна кнопка-состояние и главная кнопка «Записать на пульт»,
 *    которая видна только при подключённом пульте.
 */

#include <QWidget>

class QMenu;
class QPushButton;
class QToolButton;

namespace harmony::app {

/**
 * @brief Шапка главного окна.
 *
 * Сама ничего не делает с документом — только показывает состояние
 * и передаёт нажатия наружу через меню и сигналы.
 */
class AppHeader final : public QWidget {
    Q_OBJECT

public:
    /**
     * @brief Создаёт шапку.
     * @param connectionMenu Меню, которое открывается по щелчку на состоянии пульта.
     * @param parent Родительский виджет.
     */
    AppHeader(QMenu* connectionMenu, QWidget* parent = nullptr);

    /**
     * @brief Обновляет кнопки отмены и повтора.
     * @param canUndo Есть что отменить.
     * @param undoText Что именно будет отменено (для подсказки).
     * @param canRedo Есть что повторить.
     * @param redoText Что именно будет повторено.
     */
    void setUndoState(bool canUndo, const QString& undoText, bool canRedo, const QString& redoText);

    /**
     * @brief Показывает состояние связи с пультом.
     * @param connected true — пульт подключён: зелёная точка и кнопка записи.
     * @param text Подпись (например, «Пульт не подключён» или «Harmony 900»).
     */
    void setConnectionState(bool connected, const QString& text);

    /// Устанавливает доступность записи для выбранного проекта.
    void setWriteEnabled(bool writable);

signals:
    void undoRequested();  ///< Нажата кнопка «Отменить»
    void redoRequested();  ///< Нажата кнопка «Повторить»
    void writeRequested(); ///< Нажата кнопка «Записать на пульт»
    void readRequested(); ///< Чтение доступно и без открытого документа

private:
    QToolButton* undoButton_ = nullptr;
    QToolButton* redoButton_ = nullptr;
    QToolButton* connectionButton_ = nullptr;
    QPushButton* writeButton_ = nullptr;
};

} // namespace harmony::app
