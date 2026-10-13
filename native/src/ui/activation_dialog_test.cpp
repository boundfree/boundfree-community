#include "core/version.h"
#include "i18n/i18n_service.h"
#include "license/license_manager.h"
#include "ui/activation_dialog.h"

#include <QApplication>
#include <QFrame>
#include <QLabel>
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

    // Startup check failure (clock rollback) must reach the dialog status bar
    // as localized text with both UTC timestamps.
    const QString lastSeenUtc = QStringLiteral("2026-08-01T10:00:00.000Z");
    const QString currentUtc = QStringLiteral("2026-08-12T08:00:00.000Z");
    bf::LicenseResult startupResult;
    startupResult.status = bf::LicenseStatus::ClockRollback;
    startupResult.error =
        bf::makeError("license.clock_rollback",
                      {{"last_seen_utc", lastSeenUtc}, {"current_utc", currentUtc}});

    bf::ActivationDialog dialog(&manager, version, &i18n, startupResult);

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
    bf::LicenseResult legacyLicense;
    legacyLicense.status = bf::LicenseStatus::Valid;
    passed = expect(!bf::ActivationDialog::requiresOnlineStatusCheck(legacyLicense),
                    "legacy offline license must not query server status") && passed;

    bf::LicenseResult onlineLicense;
    onlineLicense.status = bf::LicenseStatus::Valid;
    onlineLicense.payload.insert(QStringLiteral("mode"), QStringLiteral("online"));
    passed = expect(bf::ActivationDialog::requiresOnlineStatusCheck(onlineLicense),
                    "online license must query server status") && passed;

    bf::LicenseResult managedOfflineLicense;
    managedOfflineLicense.status = bf::LicenseStatus::Valid;
    managedOfflineLicense.payload.insert(QStringLiteral("mode"), QStringLiteral("managed_offline"));
    passed = expect(bf::ActivationDialog::requiresOnlineStatusCheck(managedOfflineLicense),
                    "managed offline license must query server status") && passed;
    if (passed) {
        dialog.show();
        app.processEvents();
        passed = expect(machineIdEdit->height() == inviteEdit->height(),
                        "machine ID and invite inputs are misaligned") && passed;
        passed = expect(inviteButton->height() == inviteEdit->height(),
                        "invite button and input are misaligned") && passed;

        // The startup clock-rollback failure is shown in the status bar with
        // the en_US translation of both UTC timestamps.
        auto* statusLabel = dialog.findChild<QLabel*>(QStringLiteral("statusLabel"));
        passed = expect(statusLabel != nullptr, "status label is missing") && passed;
        if (statusLabel != nullptr) {
            passed = expect(statusLabel->isVisible(),
                            "status label is hidden for the clock rollback startup failure") && passed;
            const QString expected = i18n.text(
                "error.license.clock_rollback",
                {{"last_seen_utc", lastSeenUtc}, {"current_utc", currentUtc}});
            passed = expect(statusLabel->text() == expected,
                            "status label does not show the localized clock rollback text") && passed;
            passed = expect(statusLabel->text().contains(lastSeenUtc) &&
                                statusLabel->text().contains(currentUtc),
                            "status label does not show both UTC timestamps") && passed;
        }
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
