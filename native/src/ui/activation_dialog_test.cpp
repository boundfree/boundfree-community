#include "core/version.h"
#include "i18n/i18n_service.h"
#include "license/license_manager.h"
#include "ui/activation_dialog.h"

#include <QApplication>
#include <QFrame>
#include <QLayout>
#include <QLineEdit>
#include <QMetaObject>
#include <QPushButton>
#include <QToolButton>

#include <iostream>

namespace {

bool expect(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "activation-dialog-test: FAIL: " << message << '\n';
    }
    return condition;
}

}  // namespace

int main(int argc, char** argv) {
    if (!qEnvironmentVariableIsSet("BF_QT_PLATFORM")) {
        qputenv("QT_QPA_PLATFORM", "offscreen");
    }
    QApplication app(argc, argv);

    bf::I18nService i18n(QStringLiteral(BF_REPO_ROOT));
    QString i18nError;
    if (!expect(i18n.loadLocale(QStringLiteral("en_US"), &i18nError), "failed to load test locale")) {
        return 1;
    }

    bf::VersionInfo version;
    version.version = QStringLiteral("0.1.61-dev");
    bf::LicenseManager manager(version.version, QStringLiteral(BF_REPO_ROOT));
    bf::ActivationDialog dialog(&manager, version, &i18n);

    auto* offlineToggle = dialog.findChild<QToolButton*>(QStringLiteral("offlineToggle"));
    auto* offlinePanel = dialog.findChild<QFrame*>(QStringLiteral("offlinePanel"));
    auto* machineIdEdit = dialog.findChild<QLineEdit*>(QStringLiteral("machineIdEdit"));
    auto* inviteEdit = dialog.findChild<QLineEdit*>(QStringLiteral("inviteCodeEdit"));
    QPushButton* inviteButton = nullptr;
    for (QPushButton* button : dialog.findChildren<QPushButton*>()) {
        if (button->text() == i18n.text("app.activation.invite_code")) {
            inviteButton = button;
            break;
        }
    }

    bool passed = expect(offlineToggle != nullptr, "offline toggle is missing") &&
                  expect(offlinePanel != nullptr, "offline panel is missing") &&
                  expect(machineIdEdit != nullptr, "machine ID input is missing") &&
                  expect(inviteEdit != nullptr, "invite input is missing") &&
                  expect(inviteButton != nullptr, "invite button is missing");
    if (passed) {
        dialog.show();
        app.processEvents();
        passed = expect(machineIdEdit->height() == inviteEdit->height(),
                        "machine ID and invite inputs are misaligned") && passed;
        passed = expect(inviteButton->height() == inviteEdit->height(),
                        "invite button and input are misaligned") && passed;
    }
    if (passed) {
        passed = expect(offlinePanel->isHidden(), "offline panel is visible by default");
        const int initialHeight = dialog.height();
        offlineToggle->click();
        app.processEvents();
        passed = expect(!offlinePanel->isHidden(), "offline panel did not expand") && passed;
        passed = expect(offlinePanel->geometry().bottom() <= dialog.rect().bottom(),
                        "offline panel is clipped at the default window size") && passed;
        passed = expect(dialog.height() >= initialHeight,
                        "dialog did not grow when expanding") && passed;
        offlineToggle->click();
        app.processEvents();
        passed = expect(offlinePanel->isHidden(), "offline panel did not collapse") && passed;
        passed = expect(dialog.height() <= initialHeight,
                        "dialog did not shrink back after collapsing") && passed;
        passed = expect(QMetaObject::invokeMethod(
                            &dialog, "onRegistrationSubmitted", Qt::DirectConnection,
                            Q_ARG(QString, QStringLiteral("request-id"))),
                        "registration state transition failed") && passed;
        passed = expect(!inviteEdit->isEnabled(), "invite input remained enabled after request") && passed;
        passed = expect(!inviteButton->isEnabled(), "invite button remained enabled after request") && passed;
    }

    std::cout << (passed ? "activation-dialog-test: PASS\n" : "activation-dialog-test: FAIL\n");
    return passed ? 0 : 1;
}
