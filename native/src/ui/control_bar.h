#pragma once

#include <QAction>
#include <QLayout>
#include <QHBoxLayout>
#include <QToolButton>
#include <QWidget>
#include <QResizeEvent>
#include <QShowEvent>
#include <algorithm>

namespace bf {
// A wrapping native control row: no overflow menu hides controls on resize.
class ControlFlowLayout : public QLayout {
public:
    explicit ControlFlowLayout(QWidget* parent) : QLayout(parent) { setContentsMargins(4, 4, 4, 4); setSpacing(6); }
    ~ControlFlowLayout() override { while (auto* item = takeAt(0)) delete item; }
    void addItem(QLayoutItem* item) override { items_.append(item); invalidate(); }
    int count() const override { return items_.size(); }
    QLayoutItem* itemAt(int i) const override { return items_.value(i); }
    QLayoutItem* takeAt(int i) override { return i >= 0 && i < items_.size() ? items_.takeAt(i) : nullptr; }
    Qt::Orientations expandingDirections() const override { return {}; }
    bool hasHeightForWidth() const override { return true; }
    int heightForWidth(int width) const override { return arrange(QRect(0, 0, width, 0), false); }
    QSize sizeHint() const override { return QSize(600, heightForWidth(600)); }
    QSize minimumSize() const override {
        QSize size;
        for (auto* item : items_) if (!item->isEmpty()) size = size.expandedTo(item->minimumSize());
        const auto m = contentsMargins();
        return size + QSize(m.left() + m.right(), m.top() + m.bottom());
    }
    void setGeometry(const QRect& rect) override { QLayout::setGeometry(rect); arrange(rect, true); }
private:
    int arrange(const QRect& rect, bool apply) const {
        const auto m = contentsMargins();
        const QRect inner = rect.adjusted(m.left(), m.top(), -m.right(), -m.bottom());
        int x = inner.x(), y = inner.y(), rowHeight = 0;
        QList<QPair<QLayoutItem*, QRect>> row;
        const auto placeRow = [&]() {
            if (apply) for (const auto& entry : row) {
                QRect geometry = entry.second;
                geometry.moveTop(y + (rowHeight - geometry.height()) / 2);
                entry.first->setGeometry(geometry);
            }
            row.clear();
        };
        for (auto* item : items_) {
            if (item->isEmpty()) continue;
            QSize size = item->sizeHint();
            size.setWidth(std::min(size.width(), std::max(1, inner.width())));
            if (x > inner.x() && x + size.width() > inner.right() + 1) {
                placeRow(); x = inner.x(); y += rowHeight + spacing(); rowHeight = 0;
            }
            row.append({item, QRect(QPoint(x, y), size)});
            x += size.width() + spacing();
            rowHeight = std::max(rowHeight, size.height());
        }
        placeRow();
        return y + rowHeight - rect.y() + m.bottom();
    }
    QList<QLayoutItem*> items_;
};

// QLayout::totalMinimumSize folds height-for-width at the narrowest width
// into QWidget's default hint. That height is not a minimum at wider widths.
// Keep region minima independent; normal layout still evaluates actual HFW.
class ControlBar : public QWidget {
public:
    explicit ControlBar(QWidget* parent = nullptr) : QWidget(parent), flow_(new ControlFlowLayout(this)) {
        // Allow shrinking below the 600 px sizeHint's wrapped height. The
        // layout still requests the correct height for the actual width.
        setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Preferred);
    }
    // Minimum dimensions describe one usable row, independently of a stale
    // narrow width. The parent evaluates heightForWidth for its actual width.
    QSize minimumSizeHint() const override {
        QSize result = flow_->minimumSize();
        if (wrappedMinimum_ && isVisible()) result.setHeight(std::max(result.height(), flow_->heightForWidth(width())));
        return result;
    }
    // Top-level bars must contribute all their wrapped rows to the window
    // minimum, otherwise a tall analysis result can overlap the playback bar.
    void setWrappedMinimum(bool enabled) { wrappedMinimum_ = enabled; updateGeometry(); }
    void addWidget(QWidget* widget) { flow_->addWidget(widget); }
    void addPair(QWidget* label, QWidget* control) {
        auto* group = new QWidget(this);
        auto* row = new QHBoxLayout(group);
        row->setContentsMargins(0, 0, 0, 0);
        row->setSpacing(4);
        row->addWidget(label);
        row->addWidget(control);
        addWidget(group);
    }
    void addSeparator() {} // Spacing is provided by layout; no dangling separators on wrap.
    QAction* addAction(const QString& text) { return addAction(QIcon(), text); }
    QAction* addAction(const QIcon& icon, const QString& text) {
        auto* action = new QAction(icon, text, this);
        QWidget::addAction(action);
        auto* button = new QToolButton(this);
        button->setProperty("controlBarAction", true);
        button->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
        button->setDefaultAction(action);
        button->setMinimumHeight(28);
        button->setFocusPolicy(Qt::StrongFocus);
        addWidget(button);
        return action;
    }
protected:
    bool event(QEvent* event) override {
        const bool handled = QWidget::event(event);
        if (event->type() == QEvent::LayoutRequest) refreshWrappedMinimum();
        return handled;
    }
    void resizeEvent(QResizeEvent* event) override { QWidget::resizeEvent(event); refreshWrappedMinimum(); }
    void showEvent(QShowEvent* event) override { QWidget::showEvent(event); refreshWrappedMinimum(); }
private:
    void refreshWrappedMinimum() {
        if (!wrappedMinimum_ || !isVisible()) return;
        const int required = flow_->heightForWidth(width());
        if (reportedHeight_ != required) { reportedHeight_ = required; updateGeometry(); }
    }
    bool wrappedMinimum_ = false;
    int reportedHeight_ = -1;
    ControlFlowLayout* flow_;
};
} // namespace bf
