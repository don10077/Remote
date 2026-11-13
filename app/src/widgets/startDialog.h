#pragma once

/**
 * @file startDialog.h
 * @brief Окно выбора конфигурации: недавние, создать, открыть, пример.
 *
 * Показывается при запуске (если не выбрана конфигурация «открывать при запуске»)
 * и из меню. Галочка «Открывать при запуске» запоминает выбранную конфигурацию.
 */

#include <QDialog>

class QCheckBox;
class QListWidget;
class QListWidgetItem;
class QPushButton;

namespace harmony::app {

/**
 * @brief Окно выбора конфигурации.
 *
 * Само ничего не открывает: после exec() главное окно смотрит choice() и folder().
 */
class StartDialog final : public QDialog {
    Q_OBJECT

public:
    /// Что выбрал пользователь.
    enum class Choice {
        None,   ///< Закрыл окно
        Open,   ///< Открыть конфигурацию из списка (folder())
        Create, ///< Создать новую
        Browse, ///< Открыть папку или файл через диалог
        Demo,   ///< Открыть встроенный пример
    };

    /**
     * @brief Создаёт окно и заполняет список недавних конфигураций.
     * @param parent Родительское окно.
     */
    explicit StartDialog(QWidget* parent = nullptr);

    /// @return Выбор пользователя.
    [[nodiscard]] Choice choice() const { return choice_; }

    /// @return Папка выбранной конфигурации (для Choice::Open).
    [[nodiscard]] QString folder() const { return folder_; }

private:
    void fillList();
    void updateButtons();
    void openSelected();
    void finish(Choice choice);

    QListWidget* list_ = nullptr;
    QCheckBox* startup_ = nullptr;
    QPushButton* open_ = nullptr;
    QPushButton* remove_ = nullptr;
    Choice choice_ = Choice::None;
    QString folder_;
};

} // namespace harmony::app
