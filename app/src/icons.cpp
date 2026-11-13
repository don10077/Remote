/**
 * @file icons.cpp
 * @brief Рисование значков интерфейса.
 *
 * Все контуры задаются в сетке 24×24 (как у распространённых наборов значков)
 * и масштабируются под нужный размер при рисовании.
 */

#include "icons.h"

#include "theme.h"

#include <QIconEngine>
#include <QPainter>
#include <QPainterPath>
#include <QPixmap>

#include <memory>

namespace harmony::app::icons {

namespace {

/// Контур значка в сетке 24×24 и признак заливки (иначе — обводка).
struct Shape {
    QPainterPath path;
    bool filled = false;
};

/// Изогнутая стрелка «назад» (отмена). Повтор — её зеркальное отражение.
QPainterPath undoArrow() {
    QPainterPath path;
    // Дуга от правого нижнего края вверх и влево.
    path.moveTo(19, 17);
    path.cubicTo(19, 11.5, 15.5, 8.5, 10.5, 8.5);
    path.lineTo(5, 8.5);
    // Наконечник.
    path.moveTo(8.5, 5);
    path.lineTo(5, 8.5);
    path.lineTo(8.5, 12);
    return path;
}

Shape shapeFor(Icon icon) {
    QPainterPath path;
    switch (icon) {
    case Icon::Undo:
        return {undoArrow()};

    case Icon::Redo: {
        // Зеркалим стрелку отмены относительно вертикальной оси сетки.
        QTransform mirror;
        mirror.translate(24, 0);
        mirror.scale(-1, 1);
        return {mirror.map(undoArrow())};
    }

    case Icon::ChevronDown:
        path.moveTo(7, 10);
        path.lineTo(12, 15);
        path.lineTo(17, 10);
        return {path};

    case Icon::Upload:
        // Стрелка вверх и «лоток».
        path.moveTo(12, 15);
        path.lineTo(12, 4);
        path.moveTo(7.5, 8.5);
        path.lineTo(12, 4);
        path.lineTo(16.5, 8.5);
        path.moveTo(5, 15);
        path.lineTo(5, 19);
        path.lineTo(19, 19);
        path.lineTo(19, 15);
        return {path};

    case Icon::Download:
        path.moveTo(12, 4);
        path.lineTo(12, 15);
        path.moveTo(7.5, 10.5);
        path.lineTo(12, 15);
        path.lineTo(16.5, 10.5);
        path.moveTo(5, 15);
        path.lineTo(5, 19);
        path.lineTo(19, 19);
        path.lineTo(19, 15);
        return {path};

    case Icon::Folder:
        path.moveTo(3.5, 7);
        path.lineTo(3.5, 18);
        path.lineTo(20.5, 18);
        path.lineTo(20.5, 8.5);
        path.lineTo(11.5, 8.5);
        path.lineTo(9.5, 6);
        path.lineTo(4.5, 6);
        path.closeSubpath();
        return {path};

    case Icon::Remote:
        // Корпус пульта, экран и три кнопки.
        path.addRoundedRect(QRectF(7, 2.5, 10, 19), 3, 3);
        path.addRoundedRect(QRectF(9.5, 5, 5, 4), 0.8, 0.8);
        path.addEllipse(QPointF(12, 13), 1.4, 1.4);
        path.addEllipse(QPointF(12, 17.5), 1.0, 1.0);
        return {path};

    case Icon::Play:
        path.moveTo(8, 5.5);
        path.lineTo(18.5, 12);
        path.lineTo(8, 18.5);
        path.closeSubpath();
        return {path};

    case Icon::Dot:
        path.addEllipse(QPointF(12, 12), 7, 7);
        return {path, true};

    case Icon::Plus:
        path.moveTo(12, 5);
        path.lineTo(12, 19);
        path.moveTo(5, 12);
        path.lineTo(19, 12);
        return {path};

    case Icon::Save:
        // Дискета: корпус со срезанным углом, шторка и наклейка.
        path.moveTo(4.5, 4.5);
        path.lineTo(16, 4.5);
        path.lineTo(19.5, 8);
        path.lineTo(19.5, 19.5);
        path.lineTo(4.5, 19.5);
        path.closeSubpath();
        path.moveTo(8, 4.5);
        path.lineTo(8, 9);
        path.lineTo(15, 9);
        path.lineTo(15, 4.5);
        path.addRect(QRectF(8, 13.5, 8, 6));
        return {path};

    case Icon::Television:
        // Экран и подставка.
        path.addRoundedRect(QRectF(3, 4.5, 18, 12), 1.5, 1.5);
        path.moveTo(9, 20);
        path.lineTo(15, 20);
        path.moveTo(12, 16.5);
        path.lineTo(12, 20);
        return {path};

    case Icon::Speaker:
        // Корпус колонки: большой и малый динамик.
        path.addRoundedRect(QRectF(6, 2.5, 12, 19), 2, 2);
        path.addEllipse(QPointF(12, 15), 3.2, 3.2);
        path.addEllipse(QPointF(12, 7.5), 1.4, 1.4);
        return {path};

    case Icon::Disc:
        path.addEllipse(QPointF(12, 12), 8.5, 8.5);
        path.addEllipse(QPointF(12, 12), 2.2, 2.2);
        return {path};

    case Icon::Gamepad:
        // Корпус джойстика, крестовина и две кнопки.
        path.addRoundedRect(QRectF(2.5, 7, 19, 10.5), 5, 5);
        path.moveTo(7.5, 10.5);
        path.lineTo(7.5, 14);
        path.moveTo(5.75, 12.25);
        path.lineTo(9.25, 12.25);
        path.addEllipse(QPointF(15.5, 11), 0.9, 0.9);
        path.addEllipse(QPointF(17.5, 13.5), 0.9, 0.9);
        return {path};

    case Icon::SetTopBox:
        // Плоский корпус с индикатором и ножками.
        path.addRoundedRect(QRectF(2.5, 8, 19, 8), 1.5, 1.5);
        path.addEllipse(QPointF(6.5, 12), 1.0, 1.0);
        path.moveTo(11, 12);
        path.lineTo(18, 12);
        path.moveTo(5, 16);
        path.lineTo(5, 18);
        path.moveTo(19, 16);
        path.lineTo(19, 18);
        return {path};

    case Icon::Computer:
        path.addRoundedRect(QRectF(3, 4, 18, 12), 1.5, 1.5);
        path.moveTo(2, 20);
        path.lineTo(22, 20);
        path.moveTo(9, 16);
        path.lineTo(8, 20);
        path.moveTo(15, 16);
        path.lineTo(16, 20);
        return {path};
    }
    return {path};
}

/**
 * @brief Движок значка: рисует контур под запрошенный размер и масштаб экрана.
 */
class ShapeIconEngine final : public QIconEngine {
public:
    ShapeIconEngine(Shape shape, QColor color) : shape_(std::move(shape)), color_(color) {}

    void paint(QPainter* painter, const QRect& rect, QIcon::Mode mode, QIcon::State state) override {
        (void)state;
        QColor penColor = color_;
        if (mode == QIcon::Disabled) {
            penColor.setAlphaF(0.35); // неактивный значок — полупрозрачный
        }

        painter->save();
        painter->setRenderHint(QPainter::Antialiasing);
        // Вписываем сетку 24×24 в прямоугольник с сохранением пропорций.
        const qreal side = qMin(rect.width(), rect.height());
        painter->translate(rect.x() + (rect.width() - side) / 2.0,
                           rect.y() + (rect.height() - side) / 2.0);
        painter->scale(side / 24.0, side / 24.0);

        if (shape_.filled) {
            painter->setPen(Qt::NoPen);
            painter->setBrush(penColor);
        } else {
            QPen pen(penColor, 1.8, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
            painter->setPen(pen);
            painter->setBrush(Qt::NoBrush);
        }
        painter->drawPath(shape_.path);
        painter->restore();
    }

    QPixmap pixmap(const QSize& size, QIcon::Mode mode, QIcon::State state) override {
        return scaledPixmap(size, mode, state, 1.0);
    }

    QPixmap scaledPixmap(const QSize& size, QIcon::Mode mode, QIcon::State state,
                         qreal scale) override {
        // Учитываем масштаб экрана, чтобы на 125–200 % значок не был размытым.
        QPixmap pixmap(size * scale);
        pixmap.setDevicePixelRatio(scale);
        pixmap.fill(Qt::transparent);
        QPainter painter(&pixmap);
        paint(&painter, QRect(QPoint(0, 0), size), mode, state);
        return pixmap;
    }

    QIconEngine* clone() const override { return new ShapeIconEngine(shape_, color_); }

private:
    Shape shape_;
    QColor color_;
};

} // namespace

QIcon deviceType(const std::string& type, const QColor& color) {
    // Группы типов устройств из конфигурации пульта.
    static const std::initializer_list<std::pair<const char*, Icon>> table = {
        {"Television", Icon::Television}, {"TvDvd", Icon::Television},
        {"TvVcr", Icon::Television},      {"TvDvdVcr", Icon::Television},
        {"Monitor", Icon::Television},    {"Projector", Icon::Television},
        {"Receiver", Icon::Speaker},      {"Amplifier", Icon::Speaker},
        {"AudioVideoSwitch", Icon::Speaker},
        {"DvdCd", Icon::Disc},            {"DvdCdRadio", Icon::Disc},
        {"DvdRecorder", Icon::Disc},      {"DvdVcr", Icon::Disc},
        {"Cd", Icon::Disc},               {"CdJukebox", Icon::Disc},
        {"Laserdisc", Icon::Disc},        {"Vcr", Icon::Disc},
        {"DvdCdGame", Icon::Gamepad},     {"GameConsole", Icon::Gamepad},
        {"SetTopBox", Icon::SetTopBox},   {"CableBox", Icon::SetTopBox},
        {"Satellite", Icon::SetTopBox},   {"Pvr", Icon::SetTopBox},
        {"TvHdd", Icon::SetTopBox},
        {"Computer", Icon::Computer},     {"MediaCenterPC", Icon::Computer},
        {"Laptop", Icon::Computer},
    };
    for (const auto& [name, icon] : table) {
        if (type == name) {
            return make(icon, color);
        }
    }
    return make(Icon::Remote, color);
}

QIcon make(Icon icon, const QColor& color) {
    const QColor effective = color.isValid() ? color : theme::color(theme::Color::Text);
    return QIcon(new ShapeIconEngine(shapeFor(icon), effective));
}

} // namespace harmony::app::icons
