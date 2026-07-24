// CollapsiblePanel self-test.
// Style references m3_dsp_test.cpp: custom main returning non-zero on failure.
// Runs headless via QT_QPA_PLATFORM=offscreen; creates a QApplication because
// QWidget/QToolButton require one to construct and lay out.
//
// Covers:
//   (a) default state is expanded and content visible
//   (b) setExpanded(false) hides content, flips arrow, isExpanded()==false
//   (c) toggling twice returns to the original state
//   (d) setTitle/title round-trip

#include "ui/collapsible_panel.h"

#include <QApplication>
#include <QLabel>
#include <QToolButton>
#include <QVBoxLayout>

#include <iostream>

namespace {

int failures = 0;

void check(bool ok, const char* label) {
    if (ok) {
        std::cout << "  ok: " << label << "\n";
    } else {
        std::cerr << "  FAIL: " << label << "\n";
        ++failures;
    }
}


}  // namespace

int main(int argc, char** argv) {
    QApplication app(argc, argv);

    bf::CollapsiblePanel panel(QString("Channels"));
    auto* child = new QLabel(QStringLiteral("body"));
    panel.addWidget(child);
    panel.show();

    // (a) default state is expanded and content visible.
    check(panel.isExpanded(), "default isExpanded() == true");
    check(child->isVisible(), "default content visible");

    if (auto* btn = panel.findChild<QToolButton*>()) {
        check(btn->arrowType() == Qt::DownArrow, "expanded arrow == Qt::DownArrow");
    } else {
        check(false, "header QToolButton exists");
    }

    // (b) setExpanded(false) hides content, flips arrow, isExpanded()==false.
    panel.setExpanded(false);
    check(!panel.isExpanded(), "after collapse isExpanded() == false");
    check(!child->isVisible(), "after collapse content hidden");
    if (auto* btn = panel.findChild<QToolButton*>()) {
        check(btn->arrowType() == Qt::RightArrow, "collapsed arrow == Qt::RightArrow");
    }

    // (c) toggling twice returns to the original state.
    const bool before = panel.isExpanded();
    panel.setExpanded(!before);
    panel.setExpanded(before);
    check(panel.isExpanded() == before, "double toggle returns to original state");
    check(!panel.isExpanded(), "double toggle preserves collapsed state");

    // Click the header to drive toggled() signal emission.
    int toggledCount = 0;
    bool lastToggledValue = false;
    QObject::connect(&panel, &bf::CollapsiblePanel::toggled,
        [&](bool expanded) {
            ++toggledCount;
            lastToggledValue = expanded;
        });
    // Start collapsed (from step b). Click header -> expand.
    if (auto* btn = panel.findChild<QToolButton*>()) {
        btn->click();
    }
    check(toggledCount == 1, "header click emits toggled once");
    check(lastToggledValue == true, "header click emits toggled(true) on expand");
    check(panel.isExpanded(), "header click expanded the panel");
    check(child->isVisible(), "header click made content visible");
    // Click again -> collapse.
    if (auto* btn = panel.findChild<QToolButton*>()) {
        btn->click();
    }
    check(toggledCount == 2, "second header click emits toggled again");
    check(lastToggledValue == false, "second header click emits toggled(false) on collapse");
    check(!panel.isExpanded(), "second header click collapsed the panel");
    check(!child->isVisible(), "second header click hid content");

    // (d) setTitle/title round-trip.
    const QString newTitle = QStringLiteral("Filters");
    panel.setTitle(newTitle);
    check(panel.title() == newTitle, "setTitle/title round-trip");

    // setContentLayout: replace default layout with a custom one.
    auto* customLayout = new QVBoxLayout;
    auto* customChild = new QLabel(QStringLiteral("custom"));
    customLayout->addWidget(customChild);
    panel.setContentLayout(customLayout);
    panel.setExpanded(true);
    check(customChild->isVisible(), "child in setContentLayout visible after expand");
    panel.setExpanded(false);
    check(!customChild->isVisible(), "child in setContentLayout hidden after collapse");

    if (failures == 0) {
        std::cout << "collapsible-panel-test-ok failures=0\n";
        return 0;
    }
    std::cerr << "collapsible-panel-test-failed failures=" << failures << "\n";
    return 1;
}
