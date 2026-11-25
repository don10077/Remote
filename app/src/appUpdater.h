#pragma once

#include <QByteArray>
#include <QNetworkAccessManager>
#include <QObject>
#include <QString>
#include <QUrl>
#include <optional>

class QWidget;

namespace harmony::app {

/// Проверенные сведения о выпуске проекта. Установщик может отсутствовать.
struct UpdateRelease {
    QString version;
    QUrl page;
    QUrl installer;
    QByteArray sha256;
    qint64 size = 0;
};

/**
 * @brief Выбирает самый новый числовой выпуск из ответа GitHub releases.
 * @param json JSON-массив выпусков (draft исключается; предварительные выпуски допустимы).
 * @param current Текущая версия x.y.z.
 * @return Новый выпуск; nullopt, если подходящего обновления нет.
 */
std::optional<UpdateRelease> findUpdate(const QByteArray& json, const QString& current);

/**
 * @brief Сверяет размер и SHA-256 загруженного установщика с метаданными выпуска.
 * @param file Полный путь файла.
 * @param release Сведения GitHub, полученные по HTTPS.
 * @return Файл полностью совпадает с опубликованным установщиком.
 */
bool verifyUpdate(const QString& file, const UpdateRelease& release);

/// Проверка выпусков и загрузка обновления. Установка требует отдельного нажатия пользователя.
class AppUpdater final : public QObject {
    Q_OBJECT
public:
    /// @param parent Окно для сообщений и прогресса; владеет объектом.
    explicit AppUpdater(QWidget* parent);
    /// @param interactive Показывать также ошибки сети и отсутствие обновлений.
    void check(bool interactive = false);
    /// @return Установщик запущен; отмена UAC оставляет программу открытой.
    bool launchInstaller();

signals:
    /// Файл загружен и проверен; главное окно проверяет правки и занятость пульта.
    void readyToInstall();

private:
    void offer(const UpdateRelease& release);
    void download(const UpdateRelease& release);
    void fail(const QString& message, bool interactive);
    QWidget* window_;
    QNetworkAccessManager network_;
    bool busy_ = false;
    QString readyFile_;
    UpdateRelease readyRelease_;
};

} // namespace harmony::app
