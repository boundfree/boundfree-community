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
#include <QScrollArea>
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

    if (auto* btn = panel.headerButton()) {
        check(btn->accessibleName() == panel.title(), "header has an accessible title");
    } else {
        check(false, "header button exists");
    }

    // (b) setExpanded(false) hides content, flips arrow, isExpanded()==false.
    panel.setExpanded(false);
    check(!panel.isExpanded(), "after collapse isExpanded() == false");
    check(!child->isVisible(), "after collapse content hidden");
    if (auto* btn = panel.headerButton()) {
        check(btn->isEnabled(), "collapsed header stays operable");
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
    if (auto* btn = panel.headerButton()) {
        btn->click();
    }
    check(toggledCount == 1, "header click emits toggled once");
    check(lastToggledValue == true, "header click emits toggled(true) on expand");
    check(panel.isExpanded(), "header click expanded the panel");
    check(child->isVisible(), "header click made content visible");
    // Click again -> collapse.
    if (auto* btn = panel.headerButton()) {
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

    panel.setExpanded(true);
    panel.setHeaderVisible(false);
    check(customChild->isVisible(), "headerless embedding retains content");
    panel.setHeaderVisible(true);
    check(customChild->isVisible(), "restoring header retains content ownership");
    panel.headerButton()->click();
    check(!customChild->isVisible(), "restored header still collapses content");

    // A headerless tab must fill tall windows, rather than center a fixed
    // natural-height fragment left behind by Expander's clipping contract.
    bf::CollapsiblePanel channels(QStringLiteral("Channels"));
    auto* navigation = new QLabel(QStringLiteral("First channel"));
    auto* scroll = new QScrollArea;
    scroll->setMinimumHeight(60);
    channels.addWidget(navigation);
    channels.addWidget(scroll);
    channels.setHeaderVisible(false);
    channels.resize(240, 900);
    channels.show();
    app.processEvents();
    check(navigation->mapTo(&channels, QPoint()).y() < 12, "headerless controls align to top");
    check(scroll->height() > 800, "headerless channel scroll fills available height");
    channels.setHeaderVisible(true);
    channels.setHeaderVisible(false);
    channels.resize(240, 600);
    app.processEvents();
    check(scroll->height() > 500, "header restore and resize preserve expanding content");

    if (failures == 0) {
        std::cout << "collapsible-panel-test-ok failures=0\n";
        return 0;
    }
    std::cerr << "collapsible-panel-test-failed failures=" << failures << "\n";
    return 1;
}
