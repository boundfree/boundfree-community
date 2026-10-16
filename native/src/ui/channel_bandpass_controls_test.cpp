#include "ui/channel_bandpass_controls.h"
#include <QApplication>
#include <iostream>

int main(int argc, char** argv) {
    QApplication app(argc, argv);
    bf::I18nService i18n(BF_REPO_ROOT);
    QString error;
    if (!i18n.loadLocale("en_US", &error)) return 1;
    bf::ChannelBandpassControls controls(&i18n);
    bf::ProcessingViewState state;
    QStringList names;
    for (int index = 0; index < 64; ++index) names << QString("channel %1").arg(index);
    controls.refresh(names.mid(0, 1), 200, state);
    controls.show();
    app.processEvents();
    const int singleHeight = controls.sizeHint().height();
    controls.refresh(names, 200, state);
    app.processEvents();
    const auto fail = [](const char* reason) { std::cerr << reason << '\n'; return 1; };
    auto* selector = controls.findChild<QComboBox*>("eegBandpassChannel");
    if (!selector || selector->count() != 64 || controls.sizeHint().height() != singleHeight)
        return fail("channel count expands editor height");
    int committed = 0;
    controls.changed = [&](int channel, std::optional<bf::ChannelBandpassUi> setting) {
        ++committed;
        if (setting) state.setChannelBandpass(channel, *setting);
        else state.clearChannelBandpass(channel);
        controls.refresh(names, 200, state);
    };
    selector->setCurrentIndex(63);
    auto* first = controls.findChild<QWidget*>("eegBandpassRow_0");
    auto* last = controls.findChild<QWidget*>("eegBandpassRow_63");
    if (!first->isHidden() || last->isHidden()) return fail("selector does not isolate row");
    auto* mode = controls.findChild<QComboBox*>("eegBandpassMode_63");
    auto* low = controls.findChild<QDoubleSpinBox*>("eegBandpassLow_63");
    auto* high = controls.findChild<QDoubleSpinBox*>("eegBandpassHigh_63");
    mode->setCurrentIndex(2);
    low->setValue(5); high->setValue(20);
    QMetaObject::invokeMethod(high, "editingFinished", Qt::DirectConnection);
    if (state.perChannelBandpass.count(63) != 1 || state.perChannelBandpass.at(63).high != 20)
        return fail("selected channel override lost");
    high->setValue(2);
    QMetaObject::invokeMethod(high, "editingFinished", Qt::DirectConnection);
    const int beforeSwitch = committed;
    selector->setCurrentIndex(0);
    controls.refresh(names, 200, state);
    selector->setCurrentIndex(63);
    if (committed != beforeSwitch || high->value() != 2 || state.perChannelBandpass.at(63).high != 20)
        return fail("switch commits invalid draft or loses draft");
    high->setValue(30);
    QMetaObject::invokeMethod(high, "editingFinished", Qt::DirectConnection);
    if (state.perChannelBandpass.at(63).high != 30) return fail("draft cannot be corrected");
    if (!selector->itemText(63).contains(i18n.text("app.processing.enabled"))
        || !selector->itemText(0).contains(i18n.text("app.processing.inherit")))
        return fail("selector does not expose committed mode");
    names.append("new channel");
    controls.refresh(names, 200, state);
    if (selector->currentIndex() != 63) return fail("display mode suffix breaks selected channel restoration");
    controls.clearDrafts();
    controls.refresh({}, 0, {});
    if (selector->isEnabled() || selector->count() != 0) return fail("empty channel state");
    std::cout << "channel-bandpass-editor-ok compact=true drafts=true isolation=true\n";
    return 0;
}
