#include "ui/fluent_controls.h"
#include "ui/collapsible_panel.h"

#include <QVBoxLayout>

namespace bf {

CollapsiblePanel::CollapsiblePanel(const QString& title, QWidget* parent)
    : QWidget(parent) {
    fluent::initializeResources();
    auto* outer = new QVBoxLayout(this);
    outer->setContentsMargins(0, 0, 0, 0);
    outer->setSpacing(0);
    expander_ = new fluent::layout::Expander(this);
    expander_->setHeaderText(title);
    expander_->headerButton()->setAccessibleName(title);
    expander_->setAnimationEnabled(false); // Numeric/analysis controls resize atomically.
    expander_->setBorderVisible(false);
    content_ = new QWidget(this);
    contentLayout_ = new QVBoxLayout(content_);
    contentLayout_->setContentsMargins(0, 0, 0, 0);
    contentLayout_->setSpacing(6);
    expander_->setContentWidget(content_, fluent::WidgetOwnership::Owned);
    outer->addWidget(expander_, 0, Qt::AlignTop);
    connect(expander_, &fluent::layout::Expander::expandedChanged, this, [this](bool expanded) {
        if (expanded_ == expanded) return;
        expanded_ = expanded;
        content_->setVisible(expanded);
        emit toggled(expanded);
    });
    setExpanded(true);
}

void CollapsiblePanel::setContentLayout(QLayout* layout) {
    if (content_ == nullptr || layout == nullptr) {
        return;
    }
    // Move every item from the caller's layout into the persistent content
    // layout, then delete the now-empty caller layout. This avoids the
    // "widget already has a layout" warning and keeps a single stable layout
    // on the content widget across the panel's lifetime.
    while (auto* item = layout->takeAt(0)) {
        if (auto* w = item->widget()) {
            contentLayout_->addWidget(w);
        } else if (auto* sub = item->layout()) {
            contentLayout_->addLayout(sub);
        } else if (auto* sp = item->spacerItem()) {
            contentLayout_->addItem(sp);
        }
        delete item;
    }
    delete layout;
}

void CollapsiblePanel::addWidget(QWidget* w) {
    if (contentLayout_ == nullptr || w == nullptr) {
        return;
    }
    contentLayout_->addWidget(w);
}

void CollapsiblePanel::setExpanded(bool expanded) {
    expanded_ = expanded;
    content_->setVisible(expanded);
    if (headerVisible_) expander_->setExpandedAnimated(expanded, false);
    else content_->setVisible(expanded);
}

void CollapsiblePanel::setHeaderVisible(bool visible) {
    if (headerVisible_ == visible) return;
    headerVisible_ = visible;
    auto* outer = static_cast<QVBoxLayout*>(layout());
    if (!visible) {
        expander_->takeContentWidget();
        // Expander forces Fixed for its clipping animation. A headerless tab
        // must give that space back to its scroll area/table.
        content_->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Expanding);
        outer->addWidget(content_, 1);
        expander_->hide();
        setExpanded(true);
    } else {
        outer->removeWidget(content_);
        expander_->setContentWidget(content_, fluent::WidgetOwnership::Owned);
        expander_->show();
        setExpanded(expanded_);
    }
}
bool CollapsiblePanel::isExpanded() const { return expanded_; }
void CollapsiblePanel::setTitle(const QString& title) {
    expander_->setHeaderText(title);
    expander_->headerButton()->setAccessibleName(title);
}
QString CollapsiblePanel::title() const { return expander_->headerText(); }
QAbstractButton* CollapsiblePanel::headerButton() const { return expander_->headerButton(); }
void CollapsiblePanel::onHeaderClicked() { expander_->toggleExpanded(); }

} // namespace bf
