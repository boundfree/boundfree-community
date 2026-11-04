#pragma once

#include <QKeyEvent>
#include <QPainter>
#include <QSplitter>
#include <QSplitterHandle>
#include <QVariant>

namespace bf {
// Share the separation workspace's line and three-dot grip across workspaces.
// Native QSplitter still owns dragging, limits, and pane sizes.
class WorkspaceSplitterHandle final : public QSplitterHandle {
public:
    WorkspaceSplitterHandle(Qt::Orientation orientation, QSplitter* parent)
        : QSplitterHandle(orientation, parent) {
        setCursor(orientation == Qt::Horizontal ? Qt::SplitHCursor : Qt::SplitVCursor);
        setFocusPolicy(Qt::StrongFocus);
        setAttribute(Qt::WA_Hover);
    }
protected:
    void paintEvent(QPaintEvent*) override {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        const auto colors = palette();
        const bool horizontal = orientation() == Qt::Horizontal;
        const bool active = underMouse() || hasFocus();
        painter.fillRect(rect(), colors.color(active ? QPalette::Midlight : QPalette::Window));
        const QPointF center = QRectF(rect()).center();
        painter.setPen(QPen(colors.color(hasFocus() ? QPalette::Highlight : QPalette::Mid), 1));
        if (horizontal)
            painter.drawLine(QPointF(center.x(), 6), QPointF(center.x(), height() - 6));
        else
            painter.drawLine(QPointF(6, center.y()), QPointF(width() - 6, center.y()));
        const QRectF grip = horizontal
            ? QRectF(1, center.y() - 25, width() - 2, 50)
            : QRectF(center.x() - 25, 1, 50, height() - 2);
        painter.setBrush(colors.color(QPalette::Button));
        painter.drawRoundedRect(grip, 3, 3);
        painter.setBrush(colors.color(QPalette::Text));
        painter.setPen(Qt::NoPen);
        for (int offset : {-8, 0, 8})
            painter.drawEllipse(center + (horizontal ? QPointF(0, offset) : QPointF(offset, 0)), 1.5, 1.5);
    }
    bool event(QEvent* event) override {
        switch (event->type()) {
        case QEvent::Enter: case QEvent::Leave:
        case QEvent::HoverEnter: case QEvent::HoverLeave:
        case QEvent::FocusIn: case QEvent::FocusOut:
            update();
            break;
        default: break;
        }
        return QSplitterHandle::event(event);
    }
    void keyPressEvent(QKeyEvent* event) override {
        const bool horizontal = orientation() == Qt::Horizontal;
        const int backward = horizontal ? Qt::Key_Left : Qt::Key_Up;
        const int forward = horizontal ? Qt::Key_Right : Qt::Key_Down;
        if (event->key() == backward || event->key() == forward) {
            const int position = horizontal ? pos().x() : pos().y();
            moveSplitter(position + (event->key() == backward ? -20 : 20));
            event->accept();
            return;
        }
        QSplitterHandle::keyPressEvent(event);
    }
};

class WorkspaceSplitter final : public QSplitter {
public:
    explicit WorkspaceSplitter(QWidget* parent = nullptr) : WorkspaceSplitter(Qt::Horizontal, parent) {}
    WorkspaceSplitter(Qt::Orientation orientation, QWidget* parent) : QSplitter(orientation, parent) {
        setHandleWidth(12);
        setChildrenCollapsible(false);
        setProperty("workspaceSplitter", true);
    }
protected:
    QSplitterHandle* createHandle() override {
        return new WorkspaceSplitterHandle(orientation(), this);
    }
};
} // namespace bf
