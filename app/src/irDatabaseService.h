#pragma once

/**
 * @file irDatabaseService.h
 * @brief База ИК-кодов в программе: открытие, скачивание файлов архива и сборка базы.
 *
 * Пути и адрес — из настроек (appSettings.h): папка файлов архива (сюда
 * скачивается и отсюда собирается база) и файл собранной базы. Сборка
 * идёт в отдельном потоке, скачивание — через QNetworkAccessManager.
 */

#include "appSettings.h"
#include "harmony/core/irdb/irDatabase.h"

#include <QObject>
#include <QList>
#include <QString>
#include <atomic>
#include <memory>

class QSaveFile;
class QNetworkAccessManager;
class QNetworkReply;
class QThread;

namespace harmony::app {

/// База ИК-кодов: одна на программу.
class IrDatabaseService final : public QObject {
    Q_OBJECT

public:
    /// Файл архива, который нужен для сборки.
    struct Archive {
        settings::IrdbArchive kind;
        QString url;  ///< Откуда скачивать
        QString name; ///< Имя файла в папке (последняя часть адреса)
    };

    explicit IrDatabaseService(QObject* parent = nullptr);
    ~IrDatabaseService() override;

    /**
     * @brief Открытая база; при первом обращении открывается файл из настроек.
     * @return nullptr — базы нет или она не открылась (причина — lastError()).
     */
    [[nodiscard]] const irdb::IrDatabase* database();

    /// @return Почему база не открылась (пусто — открылась или ещё не открывали).
    [[nodiscard]] QString lastError() const { return error_; }

    /// Закрывает базу; следующий database() откроет её заново (после смены настроек).
    void reload();

    /// @return Идёт скачивание или сборка.
    [[nodiscard]] bool busy() const { return busy_; }

    /**
     * @brief Файл архива по адресу: имя — последняя часть адреса (пустая — имя по умолчанию).
     */
    [[nodiscard]] static Archive archive(settings::IrdbArchive kind, const QString& url);

    /// @return Оба файла архива с адресами из настроек.
    [[nodiscard]] static QList<Archive> archives();

    /**
     * @brief Ищет файл архива в папке: под его именем, сжатым или распакованным
     *        (без «.gz»), а если нет — под именем по умолчанию.
     * @return Путь; пусто — нет.
     */
    [[nodiscard]] static QString findArchive(const QString& folder, const Archive& archive);

    /**
     * @brief Файлы архива в папке (адреса — из настроек).
     * @param folder Папка.
     * @param devices [out] Файл устройств.
     * @param protocols [out] Файл протоколов.
     * @return Оба файла найдены.
     */
    static bool findSources(const QString& folder, QString& devices, QString& protocols);

public slots:
    /// Собирает базу из файлов в папке архива (без скачивания).
    void build();

    /**
     * @brief Скачивает файлы архива в папку (собрать — отдельно, build()).
     * @param all true — все заново; false — только тех, которых в папке нет.
     */
    void download(bool all = false);

    /// Прерывает скачивание или сборку.
    void cancel();

signals:
    /// Ход работы: что делается, сделано и всего (байт).
    void progress(const QString& stage, qint64 done, qint64 total);
    /// Работа закончена: успех и сообщение для показа.
    void finished(bool success, const QString& message);
    /// База открыта заново или закрыта.
    void changed();

private:
    /// Скачивает следующий недостающий файл; все есть — сборка.
    void downloadNext();

    void fail(const QString& message);

    std::unique_ptr<irdb::IrDatabase> database_;
    bool opened_ = false;
    QString error_;

    bool busy_ = false;
    std::atomic<bool> cancelled_{false};
    QNetworkAccessManager* network_ = nullptr;
    QNetworkReply* reply_ = nullptr;
    QSaveFile* download_ = nullptr;
    QList<Archive> queue_; ///< Файлы, которые осталось скачать
    int downloaded_ = 0;   ///< Сколько скачано в этот раз
    QThread* thread_ = nullptr;
};

} // namespace harmony::app
