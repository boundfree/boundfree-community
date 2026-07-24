#pragma once

#include <QLayout>
#include <QString>
#include <QToolButton>
#include <QVBoxLayout>
#include <QWidget>

namespace bf {

// CollapsiblePanel is a presentational container: a clickable titled header
// above a content area that can be expanded or collapsed. Clicking the header
// toggles the content visibility and flips the arrow indicator between
// Qt::RightArrow (collapsed) and Qt::DownArrow (expanded).
//
// The widget is intentionally free of any I18nService dependency: callers pass
// already-translated title text to the constructor / setTitle and call
// setTitle again when the locale changes. This keeps it reusable across the
// channel, filter and marker docks that will host it.
class CollapsiblePanel : public QWidget {
    Q_OBJECT
public:
    explicit CollapsiblePanel(const QString& title, QWidget* parent = nullptr);

    // Install the caller's layout into the content area. Ownership of the
    // layout transfers to the content container.
    void setContentLayout(QLayout* layout);

    // Convenience: add a widget into the content area's default vertical layout.
    void addWidget(QWidget* w);

    // Expand or collapse the content area and update the arrow indicator.
    void setExpanded(bool expanded);
    bool isExpanded() const;

    // Title accessors so callers can retranslate the header text.
    void setTitle(const QString& title);
    QString title() const;

signals:
    // Emitted whenever the expansion state changes, carrying the new state.
    void toggled(bool expanded);

private:
    void onHeaderClicked();

    QToolButton* headerButton_ = nullptr;
    QWidget* content_ = nullptr;
    QVBoxLayout* contentLayout_ = nullptr;
    bool expanded_ = true;
};

}  // namespace bf
