#pragma once

#include <QDialog>
#include <QString>

class QComboBox;

namespace harmony::model { struct RemoteConfig; }

namespace harmony::app {

/// Выбор целевой модели, фотография и доступность записи.
class RemoteProfileDialog final : public QDialog {
public:
    /// @param config Документ для сводки последствий смены; nullptr — выбор без документа.
    explicit RemoteProfileDialog(const QString& current, QWidget* parent = nullptr,
                                 const model::RemoteConfig* config = nullptr);
    [[nodiscard]] QString selectedRemote() const;
private:
    QComboBox* models_ = nullptr;
};

} // namespace harmony::app
