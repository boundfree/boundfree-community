#pragma once

#include <QEvent>
#include <QHideEvent>
#include <QLayout>
#include <QResizeEvent>
#include <QShowEvent>
#include <QSplitter>
#include <QStackedWidget>
#include <QTabBar>
#include <QTabWidget>
#include <QStyle>
#include <QTimer>
#include <QWidget>
#include <QLabel>
#include <QTextDocument>
#include <QtMath>

namespace bf {

// QLabel's cached rich-text hint can underestimate tables after a translated
// header wraps. Measure the complete document at its actual rendering width.
class AnalysisSummaryLabel final : public QLabel {
public:
    using QLabel::QLabel;
    int heightForWidth(int width) const override {
        if (text().isEmpty()) return QLabel::heightForWidth(width);
        QTextDocument document;
        document.setDocumentMargin(0);
        document.setDefaultFont(font());
        document.setHtml(text());
        document.setTextWidth(qMax(1, width - 2 * margin()));
        return qCeil(document.size().height()) + 2 * margin() + 4;
    }
};

// Reserve the actual wrapped text height of the current analysis page. Hidden
// pages must not inflate the window using their provisional, narrow geometry.
class AnalysisProperties final : public QWidget {
public:
    explicit AnalysisProperties(QWidget* parent = nullptr) : QWidget(parent) {
        QSizePolicy policy(QSizePolicy::Preferred, QSizePolicy::Maximum);
        policy.setHeightForWidth(true);
        setSizePolicy(policy);
    }
    QSize minimumSizeHint() const override { return QSize(0, 0); }
    QSize sizeHint() const override { return QSize(320, heightForWidth(320)); }
    int heightForWidth(int width) const override {
        if (!layout()) return 0;
        const int height = layout()->totalHeightForWidth(width);
        return height < 0 ? layout()->sizeHint().height() : height;
    }
protected:
    bool event(QEvent* event) override {
        const bool handled = QWidget::event(event);
        if (event->type() == QEvent::LayoutRequest) queueHeightUpdate();
        return handled;
    }
    void resizeEvent(QResizeEvent* event) override {
        QWidget::resizeEvent(event);
        queueHeightUpdate();
    }
    void showEvent(QShowEvent* event) override {
        QWidget::showEvent(event);
        queueHeightUpdate();
    }
    void hideEvent(QHideEvent* event) override {
        QWidget::hideEvent(event);
        setMinimumHeight(0);
        if (parentWidget()) parentWidget()->setMinimumHeight(0);
        queueHeightUpdate();
    }
private:
    void refreshOwnerGeometry() {
        for (QWidget* owner = parentWidget(); owner; owner = owner->parentWidget()) {
            // The tab stack can be squeezed below its visible page's wrapped
            // minimum when the dock also has a heading and input context.
            // Reserve only the active page; hidden translated pages must not
            // retain a taller minimum after changing tabs or languages.
            if (auto* stack = qobject_cast<QStackedWidget*>(owner)) {
                auto* page = stack->currentWidget();
                stack->setMinimumHeight(page ? page->minimumHeight() : 0);
            }
            if (auto* tabs = qobject_cast<QTabWidget*>(owner)) {
                auto* page = tabs->currentWidget();
                const int chrome = tabs->tabBar()->sizeHint().height()
                    + 2 * tabs->style()->pixelMetric(QStyle::PM_DefaultFrameWidth, nullptr, tabs);
                tabs->setMinimumHeight((page ? page->minimumHeight() : 0) + chrome);
            }
            if (owner->layout()) {
                owner->layout()->invalidate();
                owner->layout()->activate();
            }
            if (auto* splitter = qobject_cast<QSplitter*>(owner)) {
                splitter->refresh();
                // A splitter can otherwise be allocated less than the sum of
                // its children's minima by the enclosing horizontal splitter.
                splitter->setMinimumHeight(splitter->minimumSizeHint().height());
            }
            owner->updateGeometry();
        }
    }
    void queueHeightUpdate() {
        if (heightUpdateQueued_) return;
        heightUpdateQueued_ = true;
        QTimer::singleShot(0, this, [this]() {
            heightUpdateQueued_ = false;
            if (!layout()) return;
            if (!isVisible()) { refreshOwnerGeometry(); return; }
            const int required = heightForWidth(width());
            bool geometryChanged = minimumHeight() != required;
            if (geometryChanged) setMinimumHeight(required);
            // QTabWidget ignores the page's actual height-for-width. Also
            // account for translated controls wrapping above the plot, even
            // when the footer itself happens to retain the same height.
            if (QWidget* page = parentWidget()) {
                const int pageHeight = page->heightForWidth(page->width());
                if (pageHeight >= 0 && page->minimumHeight() != pageHeight) {
                    page->setMinimumHeight(pageHeight);
                    geometryChanged = true;
                }
            }
            if (geometryChanged) refreshOwnerGeometry();
        });
    }
    bool heightUpdateQueued_ = false;
};
} // namespace bf
