#include "ui/collapsible_panel.h"

#include <QVBoxLayout>

namespace bf {

CollapsiblePanel::CollapsiblePanel(const QString& title, QWidget* parent)
    : QWidget(parent) {
    auto* outer = new QVBoxLayout(this);
    outer->setContentsMargins(0, 0, 0, 0);
    outer->setSpacing(0);

    headerButton_ = new QToolButton(this);
    headerButton_->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    headerButton_->setText(title);
    headerButton_->setArrowType(Qt::DownArrow);
    headerButton_->setCheckable(false);
    headerButton_->setAutoRaise(true);
    outer->addWidget(headerButton_);

    content_ = new QWidget(this);
    contentLayout_ = new QVBoxLayout(content_);
    contentLayout_->setContentsMargins(0, 0, 0, 0);
    contentLayout_->setSpacing(0);
    outer->addWidget(content_);

    connect(headerButton_, &QToolButton::clicked, this, &CollapsiblePanel::onHeaderClicked);

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
    if (content_ != nullptr) {
        content_->setVisible(expanded);
    }
    if (headerButton_ != nullptr) {
        headerButton_->setArrowType(expanded ? Qt::DownArrow : Qt::RightArrow);
    }
}

bool CollapsiblePanel::isExpanded() const {
    return expanded_;
}

void CollapsiblePanel::setTitle(const QString& title) {
    if (headerButton_ != nullptr) {
        headerButton_->setText(title);
    }
}

QString CollapsiblePanel::title() const {
    return headerButton_ != nullptr ? headerButton_->text() : QString();
}

void CollapsiblePanel::onHeaderClicked() {
    const bool expanded = !isExpanded();
    setExpanded(expanded);
    emit toggled(expanded);
}

}  // namespace bf
