#include "core/version.h"
#include "i18n/i18n_service.h"
#include "license/license_manager.h"
#include "ui/activation_dialog.h"

#include <QApplication>
#include <QFrame>
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
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QApplication app(argc, argv);

    bf::I18nService i18n(QStringLiteral(BF_REPO_ROOT));
    QString i18nError;
    if (!expect(i18n.loadLocale(QStringLiteral("en_US"), &i18nError), "failed to load test locale")) {
        return 1;
    }

    bf::VersionInfo version;
    version.version = QStringLiteral("0.1.60-dev");
    bf::LicenseManager manager(version.version, QStringLiteral(BF_REPO_ROOT));
    bf::ActivationDialog dialog(&manager, version, &i18n);

    auto* offlineToggle = dialog.findChild<QToolButton*>(QStringLiteral("offlineToggle"));
    auto* offlinePanel = dialog.findChild<QFrame*>(QStringLiteral("offlinePanel"));
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
                  expect(inviteEdit != nullptr, "invite input is missing") &&
                  expect(inviteButton != nullptr, "invite button is missing");
    if (passed) {
        passed = expect(offlinePanel->isHidden(), "offline panel is visible by default");
        offlineToggle->click();
        passed = expect(!offlinePanel->isHidden(), "offline panel did not expand") && passed;
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
