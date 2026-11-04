#pragma once
#include <FluentQt/FluentQt.h>
#include <QApplication>
#include <QDynamicPropertyChangeEvent>
#include <algorithm>

namespace bf {
// Keep the platform family and larger accessibility fonts; small Windows
// defaults otherwise make scientific controls much smaller than Fluent menus.
inline QFont workbenchFont() {
    QFont font = QApplication::font();
    if (font.pointSizeF() > 0) font.setPointSizeF(std::max(10.5, font.pointSizeF()));
    else font.setPixelSize(std::max(14, font.pixelSize()));
    return font;
}
// Retain QPushButton's public contract so controller wiring and keyboard
// navigation remain shared with the existing native workbench.
class ActionButton final : public fluent::basicinput::Button {
public:
    explicit ActionButton(QWidget* parent = nullptr) : ActionButton(QString(), parent) {}
    explicit ActionButton(const QString& text, QWidget* parent = nullptr)
        : fluent::basicinput::Button(text, prepare(parent)) {
        setFluentSize(fluent::basicinput::Button::Small);
        setFont(workbenchFont());
    }
protected:
    bool event(QEvent* e) override {
        if (e->type() == QEvent::DynamicPropertyChange &&
            static_cast<QDynamicPropertyChangeEvent*>(e)->propertyName() == "devicePrimary")
            setFluentStyle(property("devicePrimary").toBool() ? Accent : Standard);
        return fluent::basicinput::Button::event(e);
    }
private:
    static QWidget* prepare(QWidget* parent) {
        static const bool initialized = fluent::initializeResources();
        Q_UNUSED(initialized);
        return parent;
    }
};
class ChoiceBox final : public fluent::basicinput::ComboBox {
public:
    explicit ChoiceBox(QWidget* parent = nullptr) : fluent::basicinput::ComboBox(parent) {
        fluent::initializeResources();
        setFont(workbenchFont());
    }
};
class TextInput final : public fluent::textfields::LineEdit {
public:
    explicit TextInput(QWidget* parent = nullptr) : TextInput(QString(), parent) {}
    TextInput(const QString& text, QWidget* parent = nullptr) : fluent::textfields::LineEdit(parent) {
        fluent::initializeResources();
        setFont(workbenchFont());
        setText(text);
    }
};
class CheckOption final : public fluent::basicinput::CheckBox {
public:
    explicit CheckOption(QWidget* parent = nullptr) : CheckOption(QString(), parent) {}
    explicit CheckOption(const QString& text, QWidget* parent = nullptr)
        : fluent::basicinput::CheckBox(text, parent) {
        fluent::initializeResources();
        setFont(workbenchFont());
        setBoxSize(18);
        setTextGap(7);
    }
};
class ProgressSlider final : public fluent::basicinput::Slider {
public:
    explicit ProgressSlider(Qt::Orientation direction = Qt::Horizontal, QWidget* parent = nullptr)
        : fluent::basicinput::Slider(direction, parent) { fluent::initializeResources(); }
};
// Native numeric editing keeps its exact decimals, units, validation and signals.
// Resolve the surrounding chrome from the same Fluent tokens as real controls.
struct SurfaceTokens : fluent::FluentElement {};
inline fluent::FluentElement::Colors surfaceColors() { return SurfaceTokens().themeColors(); }
inline QPalette fluentPalette() {
    const auto colors = surfaceColors();
    QPalette palette;
    palette.setColor(QPalette::Window, colors.bgCanvas);
    palette.setColor(QPalette::Base, colors.controlDefault);
    palette.setColor(QPalette::AlternateBase, colors.bgLayer);
    palette.setColor(QPalette::Button, colors.controlDefault);
    for (auto role : {QPalette::WindowText, QPalette::Text, QPalette::ButtonText}) {
        palette.setColor(role, colors.textPrimary);
        palette.setColor(QPalette::Disabled, role, colors.textDisabled);
    }
    palette.setColor(QPalette::PlaceholderText, colors.textSecondary);
    palette.setColor(QPalette::Highlight, colors.accentDefault);
    palette.setColor(QPalette::HighlightedText, colors.textOnAccent);
    palette.setColor(QPalette::Link, colors.accentDefault);
    palette.setColor(QPalette::ToolTipBase, colors.bgLayer);
    palette.setColor(QPalette::ToolTipText, colors.textPrimary);
    palette.setColor(QPalette::Mid, colors.strokeDefault);
    return palette;
}
inline QString nativeFluentStyle() {
    const auto c = surfaceColors();
    const auto css = [](const QColor& color) {
        return QString("rgba(%1,%2,%3,%4)").arg(color.red()).arg(color.green()).arg(color.blue()).arg(color.alpha());
    };
    return QString(R"(
        QMainWindow, QDialog { background: %1; color: %3; }
        QLabel, QToolButton { color: %3; }
        QMenuBar, QMenu, QStatusBar { background: %2; color: %3; border: none; }
        QMenuBar::item, QMenu::item { padding: 5px 10px; background: transparent; }
        QMenuBar::item:selected, QMenu::item:selected { background: %6; border-radius: 4px; }
        QToolBar { background: %2; border: none; spacing: 6px; }
        QWidget#eegWorkspaceHeader { background: %2; border-bottom: 1px solid %4; }
        QWidget#eegSeparationWorkspace { background: %1; color: %3; }
        QFrame#separationWorkflowPanel, QFrame#separationInputSummary { background: %2; border: 1px solid %4; border-radius: 6px; }
        QLabel[workflowStepLabel="true"] { background: transparent; border: none; border-radius: 4px; padding: 6px 8px; }
        QLabel[workflowStepLabel="true"][currentStep="true"] { background: %6; color: %5; }
        QTabWidget::pane { border: none; background: transparent; }
        QTabBar { background: transparent; }
        QTabBar::tab { background: transparent; color: %7; border: none; border-bottom: 3px solid transparent; padding: 9px 12px; }
        QTabBar::tab:hover { background: %6; border-radius: 4px; }
        QTabBar::tab:selected { color: %3; border-bottom: 3px solid %5; }
        QSpinBox, QDoubleSpinBox { background: %2; color: %3; border: 1px solid %4; border-bottom: 1px solid %7; border-radius: 4px; padding: 4px 28px 4px 8px; min-height: 22px; max-height: 22px; }
        QSpinBox:focus, QDoubleSpinBox:focus { border-bottom: 2px solid %5; }
        QSpinBox:disabled, QDoubleSpinBox:disabled { color: %7; background: %1; }
        QAbstractSpinBox QLineEdit { background: transparent; border: none; padding: 0; selection-background-color: %5; }
        QSpinBox::up-button, QDoubleSpinBox::up-button { subcontrol-origin: border; subcontrol-position: top right; width: 24px; border: none; }
        QSpinBox::down-button, QDoubleSpinBox::down-button { subcontrol-origin: border; subcontrol-position: bottom right; width: 24px; border: none; }
        QSpinBox::up-button:hover, QSpinBox::down-button:hover, QDoubleSpinBox::up-button:hover, QDoubleSpinBox::down-button:hover { background: %6; border-radius: 4px; }
        QDoubleSpinBox[presetControl="true"] QToolButton::menu-indicator { image: none; }
        QToolButton[controlBarAction="true"] { background: %2; border: 1px solid %4; border-radius: 4px; padding: 4px 8px; }
        QToolButton[controlBarAction="true"]:hover { background: %6; }
        QHeaderView::section { color: %3; background: %2; border: none; padding: 5px; }
        QTableWidget { color: %3; background: %2; border: 1px solid %4; border-radius: 5px; gridline-color: %4; selection-background-color: %6; selection-color: %3; }
        QFrame[processingDivider="true"] { border: none; min-height: 1px; max-height: 1px; background: %4; }
    )").arg(css(c.bgCanvas), css(c.controlDefault), css(c.textPrimary), css(c.strokeDefault),
             css(c.accentDefault), css(c.subtleSecondary), css(c.textSecondary));
}
}
