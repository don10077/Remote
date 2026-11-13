/**
 * @file toggleSwitch.cpp
 * @brief Переключатель «вкл/выкл» в виде ползунка.
 */

#include "toggleSwitch.h"

#include "../theme.h"

#include <QEnterEvent>
#include <QPainter>
#include <QVariantAnimation>

#include <algorithm>

namespace harmony::app {

namespace {

constexpr int kTrackWidth = 34;  ///< Ширина дорожки
constexpr int kTrackHeight = 18; ///< Высота дорожки
constexpr int kKnobMargin = 2;   ///< Зазор между ползунком и краем дорожки
constexpr int kGap = 8;          ///< От дорожки до подписи

/// Смешивает два цвета: @p t = 0 — первый, 1 — второй.
QColor mix(const QColor& a, const QColor& b, qreal t) {
    return QColor::fromRgbF(static_cast<float>(a.redF() + (b.redF() - a.redF()) * t),
                            static_cast<float>(a.greenF() + (b.greenF() - a.greenF()) * t),
                            static_cast<float>(a.blueF() + (b.blueF() - a.blueF()) * t));
}

} // namespace

ToggleSwitch::ToggleSwitch(const QString& onText, const QString& offText, QWidget* parent)
    : QAbstractButton(parent), onText_(onText), offText_(offText.isEmpty() ? onText : offText) {
    setCheckable(true);
    setCursor(Qt::PointingHandCursor);
    setFocusPolicy(Qt::TabFocus);
    setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    setText(currentText()); // для специальных возможностей и QSS-селекторов по тексту

    slide_ = new QVariantAnimation(this);
    slide_->setDuration(120);
    slide_->setEasingCurve(QEasingCurve::OutCubic);
    connect(slide_, &QVariantAnimation::valueChanged, this, [this](const QVariant& value) {
        position_ = value.toReal();
        update();
    });
    // Ползунок едет плавно, только когда переключили мышью или клавишей; из кода
    // (setChecked) он сразу встаёт на место — см. checkStateSet().
    connect(this, &QAbstractButton::toggled, this, [this](bool checked) {
        setText(currentText());
        const qreal target = checked ? 1.0 : 0.0;
        if (qFuzzyCompare(position_ + 1.0, target + 1.0) || !isVisible()) {
            slide_->stop();
            position_ = target;
            update();
            return;
        }
        slide_->stop();
        slide_->setStartValue(position_);
        slide_->setEndValue(target);
        slide_->start();
    });
}

void ToggleSwitch::checkStateSet() {
    // Вызывается при setChecked() из кода (не при нажатии): без анимации.
    slide_->stop();
    position_ = isChecked() ? 1.0 : 0.0;
    update();
    QAbstractButton::checkStateSet();
}

QString ToggleSwitch::currentText() const {
    return isChecked() ? onText_ : offText_;
}

QSize ToggleSwitch::sizeHint() const {
    // Ширина — по длинной подписи, чтобы при переключении соседние виджеты не прыгали.
    const QFontMetrics metrics(font());
    const int textWidth =
        std::max(metrics.horizontalAdvance(onText_), metrics.horizontalAdvance(offText_));
    const int height = std::max(kTrackHeight, metrics.height()) + 6;
    return {kTrackWidth + (textWidth > 0 ? kGap + textWidth : 0) + 4, height};
}

void ToggleSwitch::paintEvent(QPaintEvent* /*event*/) {
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);

    const bool enabled = isEnabled();
    const QColor accent = theme::color(hovered_ && enabled ? theme::Color::AccentHover : theme::Color::Accent);
    // Выключенная дорожка — цвет рамок, чуть темнее при наведении.
    QColor off = theme::color(theme::Color::Border);
    if (hovered_ && enabled) {
        off = theme::isDark() ? off.lighter(130) : off.darker(112);
    }
    QColor track = mix(off, accent, position_);
    QColor knob = theme::isDark() && position_ < 0.5 ? theme::color(theme::Color::TextMuted) : QColor(Qt::white);
    if (!enabled) {
        track.setAlphaF(0.45);
        knob.setAlphaF(0.8);
    }

    // Дорожка — по вертикальному центру.
    const QRectF trackRect(1, (height() - kTrackHeight) / 2.0, kTrackWidth, kTrackHeight);
    painter.setPen(Qt::NoPen);
    painter.setBrush(track);
    painter.drawRoundedRect(trackRect, kTrackHeight / 2.0, kTrackHeight / 2.0);

    // Ползунок с лёгкой тенью.
    const qreal knobSide = kTrackHeight - 2 * kKnobMargin;
    const qreal travel = kTrackWidth - 2 * kKnobMargin - knobSide;
    const QRectF knobRect(trackRect.left() + kKnobMargin + travel * position_, trackRect.top() + kKnobMargin,
                          knobSide, knobSide);
    painter.setBrush(QColor(0, 0, 0, enabled ? 40 : 15));
    painter.drawEllipse(knobRect.translated(0, 0.8));
    painter.setBrush(knob);
    painter.drawEllipse(knobRect);

    // Подпись: включено — основной цвет текста, выключено — второстепенный.
    const QString label = currentText();
    if (!label.isEmpty()) {
        QColor color = theme::color(isChecked() ? theme::Color::Text : theme::Color::TextMuted);
        if (!enabled) {
            color.setAlphaF(0.6);
        }
        painter.setPen(color);
        painter.setFont(font());
        const QRect textRect(static_cast<int>(trackRect.right()) + kGap, 0,
                             width() - static_cast<int>(trackRect.right()) - kGap, height());
        painter.drawText(textRect, Qt::AlignVCenter | Qt::AlignLeft, label);
    }

    // Рамка фокуса — только при переходе с клавиатуры.
    if (hasFocus()) {
        QPen pen(theme::color(theme::Color::Accent), 1.5);
        painter.setPen(pen);
        painter.setBrush(Qt::NoBrush);
        painter.drawRoundedRect(trackRect.adjusted(-2, -2, 2, 2), kTrackHeight / 2.0 + 2, kTrackHeight / 2.0 + 2);
    }
}

void ToggleSwitch::enterEvent(QEnterEvent* event) {
    hovered_ = true;
    update();
    QAbstractButton::enterEvent(event);
}

void ToggleSwitch::leaveEvent(QEvent* event) {
    hovered_ = false;
    update();
    QAbstractButton::leaveEvent(event);
}

void ToggleSwitch::changeEvent(QEvent* event) {
    // Смена шрифта или темы: пересчитать размер и перерисовать.
    if (event->type() == QEvent::FontChange || event->type() == QEvent::StyleChange) {
        updateGeometry();
        update();
    }
    QAbstractButton::changeEvent(event);
}

} // namespace harmony::app
