#pragma once

/**
 * @file newConfigDialog.h
 * @brief Окно создания конфигурации: имя и место файла EZHex.
 */

#include "../documentSource.h"

#include <QDialog>

class QComboBox;
class QLabel;
class QLineEdit;
class QPushButton;

namespace harmony::app {

/**
 * @brief Окно «Новая конфигурация».
 *
 * Файл конфигурации = «место» / «имя.EZHex». По умолчанию место — «Документы/Harmony».
 * Кнопка «Создать» недоступна, пока имя пустое, содержит недопустимые символы
 * или такой файл уже существует.
 */
class NewConfigDialog final : public QDialog {
    Q_OBJECT

public:
    /**
     * @brief Создаёт окно.
     * @param initialName Имя, предложенное в поле.
     * @param parent Родительское окно.
     */
    explicit NewConfigDialog(const QString& initialName = {},
                             QWidget* parent = nullptr);

    /// @return Имя конфигурации.
    [[nodiscard]] QString name() const;

    /// @return Полный путь создаваемого файла EZHex.
    [[nodiscard]] QString filePath() const;

    /// Значение source() для встроенного примера.
    static constexpr const char* kDemoSource = app::kDemoSource;

    /**
     * @brief Основа новой конфигурации.
     * @return Пусто — пустая конфигурация; kDemoSource — встроенный пример;
     *         иначе — путь существующей конфигурации, которую нужно скопировать.
     */
    [[nodiscard]] QString source() const;

private:
    /// Проверяет имя и показывает итоговый файл или причину, почему создать нельзя.
    void validate();

    QLineEdit* name_ = nullptr;
    QLabel* location_ = nullptr;
    QLabel* result_ = nullptr;
    QPushButton* create_ = nullptr;
    /// Добавляет путь в список основ (если её там нет) и выбирает её.
    void addSource(const QString& folder);

    /// Предлагает имя по выбранной основе, пока пользователь не ввёл своё.
    void suggestName();

    QComboBox* source_ = nullptr; ///< Основа: пустая, пример, недавние, выбранные файлы
    QString base_;                ///< Место, в котором создаётся файл конфигурации
    bool nameEdited_ = false;     ///< Пользователь сам менял имя — больше не подсказываем
};

} // namespace harmony::app
