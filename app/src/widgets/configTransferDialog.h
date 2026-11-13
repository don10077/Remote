#pragma once
#include <QDialog>

class QComboBox;
class QLabel;
class QPushButton;
class QProgressBar;
class QThread;

namespace harmony::app {
/// Чтение и запись готовых конфигураций всех моделей, независимо от редактора и фото.
class ConfigTransferDialog final : public QDialog {
    Q_OBJECT
public:
    explicit ConfigTransferDialog(QWidget* parent = nullptr);
    ~ConfigTransferDialog() override;
    /// Не закрывает окно во время программирования памяти.
    void reject() override;
private:
    /// Обновляет доступные операции по выбранной модели.
    void refresh();
    /// Выбирает файл, подтверждает запись и запускает транспорт в отдельном потоке.
    void transfer(bool writing);
    /// Спрашивает адрес Harmony Hub и читает его конфигурацию по сети в файл *.hub.json.
    void readHub();
    /// Блокирует или разблокирует элементы окна на время фоновой операции.
    void setBusy(bool busy);
    QComboBox* models_ = nullptr;
    QLabel* status_ = nullptr;
    QPushButton* read_ = nullptr;
    QPushButton* write_ = nullptr;
    QPushButton* hub_ = nullptr;
    QPushButton* close_ = nullptr;
    QProgressBar* progress_ = nullptr;
    QThread* worker_ = nullptr;
};
} // namespace harmony::app
