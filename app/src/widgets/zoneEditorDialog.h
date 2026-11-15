#pragma once

/**
 * @file zoneEditorDialog.h
 * @brief Редактор зон пультов: zones.json корпуса, раскладки экранов в layouts.json,
 *        рамка `screen` в profile.json.
 *
 * Слева холст (ZoneCanvas) с фото модели, справа список зон, свойства выделенной зоны
 * и замечания проверки. Правки идут через общую историю отмены модели; сохранение
 * заменяет в файлах папки ресурсов только изменённые фрагменты (zoneEditing) и просит
 * владельца перечитать ресурсы сигналом resourcesSaved().
 */

#include "zoneCanvas.h"

#include <QDialog>
#include <QHash>
#include <QLabel>
#include <QJsonArray>
#include <QJsonObject>
#include <QList>
#include <QPixmap>
#include <QRectF>
#include <QSet>
#include <QSizeF>
#include <QStringList>
#include <QUndoStack>

#include <optional>

class QAction;
class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QLineEdit;
class QListWidget;
class QPushButton;
class QSpinBox;

namespace harmony::app {

/**
 * @brief Подпись в одну строку: длинный текст сокращается многоточием.
 *
 * Нужна для строки состояния редактора: высота окна не меняется от длины
 * текста, а полный текст остаётся во всплывающей подсказке.
 */
class ElidedLabel final : public QLabel {
    Q_OBJECT

public:
    explicit ElidedLabel(QWidget* parent = nullptr);

    /// Задать полный текст; на экране он сокращается по ширине подписи.
    void setFullText(const QString& text);

protected:
    void resizeEvent(QResizeEvent* event) override;

private:
    /// Пересчитать видимый текст под текущую ширину.
    void elide();

    QString full_;
};

class ZoneEditorDialog final : public QDialog {
    Q_OBJECT

public:
    /// Содержимое слоя: зоны и рамка экрана (рамка есть только у корпуса).
    struct State {
        QJsonArray zones;
        std::optional<QRectF> screen;
        bool operator==(const State&) const = default;
    };

    /**
     * @brief Открыть редактор на модели.
     * @param model id модели; неизвестная — первая из списка.
     */
    explicit ZoneEditorDialog(const QString& model, QWidget* parent = nullptr);

    /**
     * @brief Поставить слою состояние из истории правок и показать этот слой.
     * @param layerKey Ключ слоя (Layer::key).
     */
    void applyState(const QString& layerKey, const State& state);

signals:
    /// Файлы ресурсов записаны: их нужно перечитать.
    void resourcesSaved();

public slots:
    /// Закрытие (Esc, крестик, «Закрыть») с вопросом о несохранённых правках.
    void reject() override;

private:
    /// Слой разметки: корпус либо одна раскладка экрана модели.
    struct Layer {
        QString key;       ///< "body" или id раскладки при загрузке; не меняется до перезагрузки модели
        QString layout;    ///< id раскладки в layouts.json (после «копии для модели» — новый); пусто у корпуса
        QStringList modes; ///< режимы экрана модели, показывающие эту раскладку
        QString caption;
        QString background; ///< цвет фона экрана из remote.json; пусто — цвет по умолчанию (у корпуса не используется)
        QString role;       ///< роль экрана из remote.json
        QJsonObject backgroundStyle; ///< фон экрана-образца для нового экрана (цвет, картинка)
        QJsonObject cellStyle;       ///< оформление ячеек экрана-образца
        bool draft = false; ///< новый экран, ещё не записанный в remote.json (живёт только в редакторе)
        QSizeF ref;        ///< единицы слоя: размер фото или `ref` раскладки
        State original;    ///< как в файле
        QList<QByteArray> originalText; ///< исходный текст зон `original` (пусто — не разобран)
        State current;
        [[nodiscard]] bool body() const { return key == QLatin1String("body"); }
    };
    enum class Align { Left, Right, Top, Bottom, CentreX, CentreY, Width, Height };

    /// Прочитать модель из папки ресурсов; false — модель не прочитана, прежнее состояние сохранено.
    bool loadModel(const QString& model);
    /// Текст файла папки ресурсов, а если его нет — из загруженного снимка.
    static QByteArray readResource(const QString& relative);

    [[nodiscard]] bool hasLayer() const { return current_ >= 0 && current_ < layers_.size(); }
    Layer& layer() { return layers_[current_]; }
    const Layer& layer() const { return layers_[current_]; }
    [[nodiscard]] QJsonObject zoneAt(int item) const;
    [[nodiscard]] QString uniqueId(const QString& base, const QSet<QString>& taken) const;
    [[nodiscard]] QSet<QString> idsOf(const QJsonArray& zones) const;
    [[nodiscard]] QList<zoneEditing::Issue> issuesOf(const Layer& layer) const;
    /// Строки зон для записи: неизменённые зоны — исходным текстом.
    [[nodiscard]] static QList<QByteArray> linesOf(const Layer& layer);
    [[nodiscard]] bool isDirty() const;

    void showLayer(int index, bool keepView = false);
    /// Перерисовать подложку холста реальным видом экрана с несохранёнными правками зон.
    void updateScreenBackdrop();
    void refreshAll();
    void refreshList();
    void syncListSelection();
    void refreshProperties();
    void refreshContext();
    void refreshCheck();
    void updateTitle();
    void note(const QString& text);

    /// Записать правку в историю: before — текущее состояние слоя.
    void edit(const QString& text, const State& after, const QString& mergeKey = {});
    void onCanvasGeometry(const QString& text, const QString& mergeKey);
    void onListSelection();
    void onModelChosen(int index);

    void addZone(const std::optional<QRectF>& drawn);
    /// Распознать кнопки на фото (tools/autoZones.py) и добавить зоны в слой корпуса.
    void runAutoZones();
    /// Выделить все зоны текущего слоя (Ctrl+A).
    void selectAll();
    void duplicateZones();
    void removeZones();
    void copyZones();
    void pasteZones();
    void align(Align how);
    void setScreenArea(bool on);
    void applyRect();
    void applyId();
    void applyKind(int index);
    void applyRadius(double value);
    void applyFields();
    /// Применить действие экранной зоны из обычных полей формы.
    void applyAction();

    /// Сохранить все изменённые слои модели; false — отменено или ошибка (показана).
    bool save();
    /// Обновить вкладку «Экраны»: список экранов модели, выбранный экран и его цвет фона.
    void refreshScreens();
    /// Прочитать remote.json модели; false — ошибка уже показана пользователю.
    bool readRemoteJson(QJsonObject& remote, QByteArray& text);
    /**
     * @brief Записать remote.json, перечитать модель и показать экран.
     * @param original Текст файла, с которым сверяется запись (защита от правки извне).
     * @param remote Новое содержимое файла.
     * @param screenId Экран, который нужно показать после перезагрузки; пусто — корпус.
     * @return false — запись не удалась (ошибка показана).
     */
    bool writeRemoteJson(const QByteArray& original, const QJsonObject& remote, const QString& screenId);
    /// Разложить плитки открытого экрана сеткой «колонки × ряды» одной отменяемой правкой.
    void applyGrid();
    /// Начать новый экран: черновик сразу показывается на холсте, параметры правятся вживую.
    void beginNewScreen();
    /// Оставить в списке типов только экраны, которых у пульта ещё нет.
    void updateRoleChoices();
    /// Записать черновик нового экрана в remote.json.
    void createNewScreen();
    /// Отбросить черновик нового экрана.
    void cancelNewScreen();
    /// Удалить экран, который сейчас открыт в редакторе.
    void removeScreen();
    /// Применить цвет фона из поля ввода к открытому экрану.
    void applyBackground();
    /// Нет несохранённых правок либо пользователь их сохранил или отбросил.
    bool confirmDiscard();

    QString model_;
    QString modelName_;
    QPixmap photo_;
    QSizeF photoSize_;
    QSet<QString> modes_;     ///< режимы экрана модели (для `goto`)
    QList<Layer> layers_;     ///< [0] — корпус, далее экраны
    int current_ = -1;
    /// Тексты файлов на момент загрузки/сохранения: для обнаружения правки извне.
    QHash<QString, QByteArray> files_;
    QUndoStack undo_;

    ZoneCanvas* canvas_ = nullptr;
    QComboBox* modelBox_ = nullptr;
    QComboBox* layerBox_ = nullptr;
    std::optional<QRectF> previousScreen_; ///< Рамка до отключения экрана.
    QCheckBox* screenCheck_ = nullptr;
    QListWidget* screenList_ = nullptr;      ///< экраны модели (вкладка «Экраны»)
    QComboBox* roleBox_ = nullptr;           ///< роль нового экрана
    QPushButton* addScreenButton_ = nullptr;
    QWidget* draftPanel_ = nullptr;          ///< кнопки «Создать»/«Отмена» черновика
    int draftSerial_ = 0;                    ///< номер для уникального ключа черновика (история правок ссылается на ключ)
    QSpinBox* arrangeColsBox_ = nullptr;     ///< колонок плиток открытого экрана
    QSpinBox* arrangeRowsBox_ = nullptr;     ///< рядов плиток открытого экрана
    QPushButton* removeScreenButton_ = nullptr;
    QLineEdit* backgroundEdit_ = nullptr;    ///< цвет фона открытого экрана
    QPushButton* pickButton_ = nullptr;
    QListWidget* zoneList_ = nullptr;
    QLineEdit* idEdit_ = nullptr;
    QComboBox* kindBox_ = nullptr;
    QDoubleSpinBox* xBox_ = nullptr;
    QDoubleSpinBox* yBox_ = nullptr;
    QDoubleSpinBox* wBox_ = nullptr;
    QDoubleSpinBox* hBox_ = nullptr;
    QDoubleSpinBox* radiusBox_ = nullptr;
    QLineEdit* fieldsEdit_ = nullptr;
    QWidget* actionPanel_ = nullptr;
    QComboBox* actionBox_ = nullptr;
    QComboBox* commandBox_ = nullptr;
    QComboBox* targetBox_ = nullptr;
    QComboBox* buttonBox_ = nullptr;
    QLabel* selectionHint_ = nullptr;
    QLabel* checkLabel_ = nullptr;
    QListWidget* checkList_ = nullptr;
    ElidedLabel* statusLabel_ = nullptr;
    ElidedLabel* noteLabel_ = nullptr;
    QPushButton* saveButton_ = nullptr;
    QAction* drawAction_ = nullptr;
    QAction* autoAction_ = nullptr;
    QAction* selectAllAction_ = nullptr;
    QAction* duplicateAction_ = nullptr;
    QAction* deleteAction_ = nullptr;
    QAction* copyAction_ = nullptr;
    QList<QAction*> alignActions_;
};

} // namespace harmony::app
