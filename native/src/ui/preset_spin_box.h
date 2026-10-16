#pragma once

#include <QDoubleSpinBox>
#include <QMenu>
#include <QResizeEvent>
#include <QToolButton>
#include <initializer_list>
#include <vector>

namespace bf {

// Editable numeric input with a keyboard-accessible menu of common values.
// The owner supplies localized accessible text and reserves 26 px of right
// padding through the presetControl stylesheet property.
class PresetDoubleSpinBox final : public QDoubleSpinBox {
public:
    explicit PresetDoubleSpinBox(std::initializer_list<double> presets, QWidget* parent = nullptr)
        : QDoubleSpinBox(parent), presets_(presets), button_(new QToolButton(this)),
          menu_(new QMenu(button_)) {
        setProperty("presetControl", true);
        setButtonSymbols(QAbstractSpinBox::NoButtons);
        setMinimumHeight(24);
        button_->setArrowType(Qt::DownArrow);
        button_->setAutoRaise(true);
        button_->setFocusPolicy(Qt::StrongFocus);
        button_->setPopupMode(QToolButton::InstantPopup);
        button_->setMenu(menu_);
        setTabOrder(this, button_);
        connect(this, &QObject::objectNameChanged, this, [this](const QString& name) {
            button_->setObjectName(name.isEmpty() ? QString() : name + "PresetButton");
            menu_->setObjectName(name.isEmpty() ? QString() : name + "PresetMenu");
        });
        connect(menu_, &QMenu::aboutToShow, this, [this]() {
            menu_->clear();
            for (double value : presets_) {
                // Presets outside the current input range are unavailable.
                if (value < minimum() || value > maximum()) continue;
                auto* action = menu_->addAction(prefix() + textFromValue(value) + suffix());
                connect(action, &QAction::triggered, this, [this, value]() { setValue(value); });
            }
        });
    }

    void setPresetAccessibleName(const QString& name) {
        button_->setAccessibleName(name);
        button_->setToolTip(name);
    }

    QSize sizeHint() const override {
        return QDoubleSpinBox::sizeHint() + QSize(26, 0);
    }

    QSize minimumSizeHint() const override {
        return QDoubleSpinBox::minimumSizeHint() + QSize(26, 0);
    }

protected:
    void resizeEvent(QResizeEvent* event) override {
        QDoubleSpinBox::resizeEvent(event);
        button_->setGeometry(qMax(0, width() - 25), 1, qMin(24, width()), qMax(0, height() - 2));
    }

private:
    std::vector<double> presets_;
    QToolButton* button_;
    QMenu* menu_;
};

}  // namespace bf
