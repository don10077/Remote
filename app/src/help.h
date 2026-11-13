#pragma once

/**
 * @file help.h
 * @brief Справка программы: разделы, их тексты и показ по F1.
 */

#include <QList>
#include <QString>

class QApplication;
class QWidget;

namespace harmony::app::help {

/// Раздел справки. Порядок — порядок в оглавлении.
enum class Topic {
    Start,      ///< С чего начать: конфигурации, файлы, пример
    Connect,    ///< Подключение пульта
    Activities, ///< Активности
    Devices,    ///< Устройства: база кодов, новое, замена, сравнение, свойства
    Commands,   ///< Команды: обучение, вручную, питание и входы
    Buttons,    ///< Кнопки и экран: назначение, макросы, значки, копирование
    Remote,     ///< Проверка, запись, резервные копии, настройки пульта
    Versions,   ///< Версии, сравнение конфигураций, поиск, журнал
    Settings,   ///< Настройки программы и переводы
    Keys,       ///< Горячие клавиши
};

/// @return Все разделы в порядке оглавления.
[[nodiscard]] QList<Topic> topics();

/// @return Заголовок раздела (переведённый).
[[nodiscard]] QString title(Topic topic);

/**
 * @brief Текст раздела в HTML (переведённый).
 *
 * Ссылки на другие разделы — `topic:<номер>`, внешние — обычные `https://`.
 * Цвета не задаются: их подставляет окно справки по текущей теме.
 */
[[nodiscard]] QString html(Topic topic);

/// @return Ссылка на раздел для `<a href=…>` внутри текста справки.
[[nodiscard]] QString link(Topic topic);

/**
 * @brief Показывает окно справки на разделе.
 *
 * Окно немодальное, одно на родителя: повторный вызов только переключает раздел.
 * Из модального диалога родителем должен быть сам диалог, иначе окно справки
 * окажется заблокированным им.
 *
 * @param parent Окно, к которому относится справка (главное окно или диалог).
 * @param topic Раздел.
 */
void show(QWidget* parent, Topic topic);

/**
 * @brief Включает F1 во всех диалогах программы.
 *
 * Главное окно обрабатывает F1 само (раздел зависит от открытой вкладки);
 * в диалоге F1 открывает раздел по типу диалога, у незнакомых — «С чего начать».
 *
 * @param app Приложение; фильтр событий живёт, пока живёт оно.
 */
void installF1Handler(QApplication& app);

/**
 * @brief Имя свойства окна с разделом справки (int из Topic).
 *
 * Для окон без своего класса, например QMessageBox:
 * `box.setProperty(help::kTopicProperty, int(help::Topic::Connect))`.
 */
inline constexpr const char* kTopicProperty = "helpTopic";

} // namespace harmony::app::help
