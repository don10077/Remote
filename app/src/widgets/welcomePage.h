#pragma once

/**
 * @file welcomePage.h
 * @brief Стартовый экран — то, что видит человек при первом запуске.
 *
 * Понятные пути: создать конфигурацию с нуля, открыть одну из своих,
 * посмотреть встроенный пример (без пульта и файлов) и подключить пульт.
 */

#include <QWidget>

namespace harmony::app {

/**
 * @brief Стартовый экран с карточками действий.
 */
class WelcomePage final : public QWidget {
    Q_OBJECT

public:
    /**
     * @brief Создаёт экран.
     * @param parent Родительский виджет.
     */
    explicit WelcomePage(QWidget* parent = nullptr);

signals:
    void createRequested();  ///< «Создать новую»
    void chooseRequested();  ///< «Открыть конфигурацию» (окно выбора)
    void demoRequested();    ///< «Посмотреть пример»
    void connectRequested(); ///< «Подключить пульт»
};

} // namespace harmony::app
