#pragma once

/**
 * @file toggleSwitch.h
 * @brief Переключатель «вкл/выкл» в виде ползунка с подписью.
 *
 * Замена флажку там, где важно состояние объекта, а не галочка в списке
 * (например, «Активно» в заголовке страницы). Цвета берутся из палитры темы,
 * ползунок переезжает плавно. Подпись может зависеть от состояния.
 */

#include <QAbstractButton>

class QVariantAnimation;

namespace harmony::app {

/// Ползунок «вкл/выкл» с подписью справа.
class ToggleSwitch final : public QAbstractButton {
    Q_OBJECT

public:
    /**
     * @param onText Подпись во включённом состоянии.
     * @param offText Подпись в выключенном (пусто — та же, что onText).
     * @param parent Родитель.
     */
    explicit ToggleSwitch(const QString& onText, const QString& offText = {}, QWidget* parent = nullptr);

    [[nodiscard]] QSize sizeHint() const override;
    [[nodiscard]] QSize minimumSizeHint() const override { return sizeHint(); }

protected:
    void paintEvent(QPaintEvent* event) override;
    void enterEvent(QEnterEvent* event) override;
    void leaveEvent(QEvent* event) override;
    void changeEvent(QEvent* event) override;
    void checkStateSet() override;
    /// Нажатие считается по всей ширине (вместе с подписью), а не только по ползунку.
    [[nodiscard]] bool hitButton(const QPoint& pos) const override { return rect().contains(pos); }

private:
    /// Подпись для текущего состояния.
    [[nodiscard]] QString currentText() const;

    QString onText_;
    QString offText_;
    qreal position_ = 0.0;               ///< 0 — ползунок слева (выкл), 1 — справа (вкл)
    QVariantAnimation* slide_ = nullptr; ///< Плавный переезд ползунка
    bool hovered_ = false;
};

} // namespace harmony::app
