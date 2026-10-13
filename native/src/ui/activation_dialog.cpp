#include "ui/activation_dialog.h"

#include "core/version.h"
#include "i18n/i18n_service.h"
#include "license/license_manager.h"
#include "license/machine_id.h"
#include "license/online_activation_client.h"

#include <QApplication>
#include <QClipboard>
#include <QEventLoop>
#include <QFile>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QMessageBox>
#include <QNetworkProxy>
#include <QPlainTextEdit>
#include <QToolButton>
#include <QUrl>
#include <QVBoxLayout>

namespace bf {

namespace {

QString statusText(I18nService* i18n, const QString& status) {
    if (status == QStringLiteral("pending_approval")) {
        return i18n->text("app.activation.status_pending_approval");
    }
    if (status == QStringLiteral("active")) {
        return i18n->text("app.activation.status_active");
    }
    if (status == QStringLiteral("expired")) {
        return i18n->text("app.activation.status_expired");
    }
    if (status == QStringLiteral("revoked")) {
        return i18n->text("app.activation.status_revoked");
    }
    if (status == QStringLiteral("rejected")) {
        return i18n->text("app.activation.status_rejected");
    }
    if (status == QStringLiteral("not_found")) {
        return i18n->text("app.activation.status_not_found");
    }
    return i18n->text("app.activation.request_failed");
}

QString activationErrorText(I18nService* i18n, const QString& errorCode,
                            const QJsonObject& params) {
    if (errorCode == QStringLiteral("activation.machine_id_required") ||
        errorCode == QStringLiteral("activation.invalid_machine_id")) {
        return i18n->text("app.activation.error_invalid_machine_id");
    }
    if (errorCode == QStringLiteral("activation.not_found")) {
        return i18n->text("app.activation.status_not_found");
    }
    if (errorCode == QStringLiteral("activation.expired_or_revoked")) {
        return statusText(i18n, params.value("status").toString());
    }
    if (errorCode == QStringLiteral("activation.machine_already_bound")) {
        return i18n->text("app.activation.status_machine_already_bound");
    }
    if (errorCode == QStringLiteral("activation.already_exists")) {
        const QString status = params.value("current_status").toString();
        return i18n->text("app.activation.error_already_exists", {{"status", statusText(i18n, status)}});
    }
    if (errorCode == QStringLiteral("activation.invalid_invite_code")) {
        return i18n->text("app.activation.error_invalid_invite_code");
    }
    if (errorCode == QStringLiteral("activation.invite_revoked")) {
        return i18n->text("app.activation.error_invite_revoked");
    }
    if (errorCode == QStringLiteral("activation.invite_already_used")) {
        return i18n->text("app.activation.error_invite_already_used");
    }
    if (errorCode == QStringLiteral("activation.signing_failed")) {
        return i18n->text("app.activation.error_signing_failed");
    }
    if (errorCode == QStringLiteral("rate_limit.exceeded")) {
        return i18n->text("app.activation.error_rate_limited", {
            {"retry_after", QString::number(params.value("retry_after").toInt())},
        });
    }
    return i18n->text("app.activation.request_failed");
}

QString licenseErrorText(I18nService* i18n, const QString& errorCode,
                         const QJsonObject& params) {
    // Structured license time-watermark / save errors. These are the only
    // license codes whose params are user-displayable (rollback timestamps);
    // path/reason params never reach the UI (contract: no local paths or
    // internal diagnostics shown to users).
    if (errorCode == QStringLiteral("license.clock_rollback")) {
        return i18n->text("error.license.clock_rollback",
                          {{"last_seen_utc", params.value("last_seen_utc").toString()},
                           {"current_utc", params.value("current_utc").toString()}});
    }
    if (errorCode == QStringLiteral("license.clock_state_missing")) {
        return i18n->text("error.license.clock_state_missing");
    }
    if (errorCode == QStringLiteral("license.clock_state_corrupted")) {
        return i18n->text("error.license.clock_state_corrupted");
    }
    if (errorCode == QStringLiteral("license.clock_state_write_failed")) {
        return i18n->text("error.license.clock_state_write_failed");
    }
    if (errorCode == QStringLiteral("license.save_failed")) {
        return i18n->text("error.license.save_failed");
    }
    // Historical results (no error.code, or legacy license.* codes carrying
    // non-displayable path/reason params) fall back to the generic text.
    return i18n->text("app.activation.invalid_key");
}

}  // namespace

ActivationDialog::ActivationDialog(LicenseManager* manager, const VersionInfo& version,
                                   I18nService* i18n, const LicenseResult& initialLicenseResult,
                                   QWidget* parent)
    : QDialog(parent),
      manager_(manager),
      i18n_(i18n),
      productVersion_(version.version),
      canonicalMachineId_(machineIdCanonical(readMachineGuid())),
      displayMachineId_(currentMachineId()),
      machineIdEdit_(nullptr),
      copyBtn_(nullptr),
      requestBtn_(nullptr),
      inviteBtn_(nullptr),
      inviteCodeEdit_(nullptr),
      offlineKeyEdit_(nullptr),
      offlineActivateBtn_(nullptr),
      statusLabel_(nullptr),
      refreshBtn_(nullptr),
      nam_(new QNetworkAccessManager(this)),
      client_(new OnlineActivationClient(
          nam_, QUrl(qEnvironmentVariable("BF_ACTIVATION_SERVER_URL", "https://license-activate.unitc.cn")), this)),
      pollTimer_(new QTimer(this)) {
    // 激活网络请求必须直连，绕过用户本机的系统代理（Clash/VPN等）
    // 否则 FlClash fake-ip / HTTP代理规则会导致连接失败
    nam_->setProxy(QNetworkProxy(QNetworkProxy::NoProxy));

    connect(client_, &OnlineActivationClient::registrationSubmitted,
            this, &ActivationDialog::onRegistrationSubmitted);
    connect(client_, &OnlineActivationClient::registrationAlreadyExists,
            this, &ActivationDialog::onRegistrationAlreadyExists);
    connect(client_, &OnlineActivationClient::activationSuccess,
            this, &ActivationDialog::onActivationSuccess);
    connect(client_, &OnlineActivationClient::activationFailed,
            this, &ActivationDialog::onActivationFailed);
    connect(client_, &OnlineActivationClient::statusReceived,
            this, &ActivationDialog::onStatusReceived);
    connect(client_, &OnlineActivationClient::networkError,
            this, &ActivationDialog::onNetworkError);

    buildUi();
    pollTimer_->setInterval(30'000);
    connect(pollTimer_, &QTimer::timeout, this, &ActivationDialog::onRefreshStatus);

    // Startup license check failure is surfaced immediately in the status
    // bar (structured error.code + params → localized text). The reference
    // is consumed here and never stored; a healthy result keeps the bar
    // hidden (revocation-reopened dialogs already informed the user).
    if (!initialLicenseResult.ok()) {
        setState(licenseErrorText(i18n_, initialLicenseResult.error.code,
                                  initialLicenseResult.error.params),
                 true);
        // buildUi() sized the dialog while the status bar was hidden. Account
        // for a localized startup error now so it is not clipped and the
        // offline-panel collapse restores a stable content-driven height.
        if (QLayout* rootLayout = layout()) {
            rootLayout->activate();
            resize(width(), qMax(minimumHeight(), rootLayout->sizeHint().height()));
        }
    }
}

bool ActivationDialog::requiresOnlineStatusCheck(const LicenseResult& savedLicense) {
    if (!savedLicense.ok()) {
        return false;
    }
    const QString mode = savedLicense.payload.value("mode").toString();
    return mode == QLatin1String("online") || mode == QLatin1String("managed_offline");
}

bool ActivationDialog::checkAndHandleRevocation(
    LicenseManager* manager, const VersionInfo& version, I18nService* i18n) {
    Q_UNUSED(version);
    QNetworkAccessManager nam;
    nam.setProxy(QNetworkProxy(QNetworkProxy::NoProxy));
    OnlineActivationClient client(
        &nam,
        QUrl(qEnvironmentVariable("BF_ACTIVATION_SERVER_URL",
                                  "https://license-activate.unitc.cn")));

    const QString machineId = machineIdCanonical(readMachineGuid());
    bool invalidated = false;

    QEventLoop loop;
    QTimer timeout;
    timeout.setSingleShot(true);
    timeout.setInterval(8000); // 8 秒超时 → fail-open

    QObject::connect(&client, &OnlineActivationClient::statusReceived,
        [&](const QString& status, const QDate&) {
            if (status == QLatin1String("revoked") ||
                status == QLatin1String("rejected") ||
                status == QLatin1String("not_found")) {
                invalidated = true;
            }
            loop.quit();
        });
    QObject::connect(&client, &OnlineActivationClient::networkError,
        [&](const QString&) { loop.quit(); }); // fail-open
    QObject::connect(&client, &OnlineActivationClient::activationFailed,
        [&](const QString&, const QJsonObject&) { loop.quit(); }); // fail-open
    QObject::connect(&timeout, &QTimer::timeout, &loop, &QEventLoop::quit);

    client.checkStatus(machineId);
    timeout.start();
    loop.exec();

    if (invalidated) {
        QFile::remove(manager->licenseFilePath());
        QMessageBox::warning(nullptr,
            i18n->text("app.activation.title"),
            i18n->text("app.activation.revoked_on_server"));
        return false;
    }
    return true;
}

void ActivationDialog::buildUi() {
    setWindowTitle(i18n_->text("app.activation.title"));
    setMinimumSize(560, 520);

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(28, 28, 28, 24);
    layout->setSpacing(12);

    auto* title = new QLabel(i18n_->text("app.activation.title"), this);
    title->setObjectName("activationTitle");
    layout->addWidget(title);

    auto* machinePanel = new QFrame(this);
    machinePanel->setObjectName("machinePanel");
    auto* machineLayout = new QHBoxLayout(machinePanel);
    machineLayout->setContentsMargins(14, 12, 14, 12);
    auto* machineLabel = new QLabel(i18n_->text("app.activation.machine_id_label"), machinePanel);
    machineIdEdit_ = new QLineEdit(displayMachineId_, machinePanel);
    machineIdEdit_->setObjectName("machineIdEdit");
    machineIdEdit_->setReadOnly(true);
    machineIdEdit_->setMinimumHeight(34);
    copyBtn_ = new QPushButton(i18n_->text("app.activation.copy"), machinePanel);
    copyBtn_->setObjectName("secondaryAction");
    machineLayout->addWidget(machineLabel);
    machineLayout->addWidget(machineIdEdit_, 1);
    machineLayout->addWidget(copyBtn_);
    layout->addWidget(machinePanel);

    auto* onlineTitle = new QLabel(i18n_->text("app.activation.online_section"), this);
    onlineTitle->setObjectName("sectionTitle");
    layout->addWidget(onlineTitle);
    auto* onlineHint = new QLabel(i18n_->text("app.activation.online_hint"), this);
    onlineHint->setObjectName("sectionHint");
    onlineHint->setWordWrap(true);
    layout->addWidget(onlineHint);

    requestBtn_ = new QPushButton(i18n_->text("app.activation.request"), this);
    requestBtn_->setObjectName("primaryAction");
    requestBtn_->setToolTip(i18n_->text("app.activation.request_hint"));
    layout->addWidget(requestBtn_);
    auto* requestHint = new QLabel(i18n_->text("app.activation.request_hint"), this);
    requestHint->setObjectName("actionHint");
    requestHint->setWordWrap(true);
    layout->addWidget(requestHint);

    auto* separator = new QFrame(this);
    separator->setFrameShape(QFrame::HLine);
    separator->setObjectName("sectionSeparator");
    layout->addWidget(separator);

    auto* inviteTitle = new QLabel(i18n_->text("app.activation.invite_code_section"), this);
    inviteTitle->setObjectName("sectionTitle");
    layout->addWidget(inviteTitle);
    auto* inviteHint = new QLabel(i18n_->text("app.activation.invite_code_hint"), this);
    inviteHint->setObjectName("sectionHint");
    inviteHint->setWordWrap(true);
    layout->addWidget(inviteHint);
    auto* inviteRow = new QHBoxLayout;
    inviteCodeEdit_ = new QLineEdit(this);
    inviteCodeEdit_->setObjectName("inviteCodeEdit");
    inviteCodeEdit_->setMinimumHeight(34);
    inviteCodeEdit_->setPlaceholderText(i18n_->text("app.activation.invite_code_placeholder"));
    inviteBtn_ = new QPushButton(i18n_->text("app.activation.invite_code"), this);
    inviteBtn_->setObjectName("secondaryAction");
    inviteRow->addWidget(inviteCodeEdit_, 1);
    inviteRow->addWidget(inviteBtn_);
    layout->addLayout(inviteRow);

    auto* offlineToggle = new QToolButton(this);
    offlineToggle->setObjectName("offlineToggle");
    offlineToggle->setText(i18n_->text("app.activation.offline_section"));
    offlineToggle->setCheckable(true);
    offlineToggle->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    offlineToggle->setArrowType(Qt::RightArrow);
    offlineToggle->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    layout->addWidget(offlineToggle);

    auto* offlinePanel = new QFrame(this);
    offlinePanel->setObjectName("offlinePanel");
    auto* offlineLayout = new QVBoxLayout(offlinePanel);
    offlineLayout->setContentsMargins(14, 12, 14, 12);
    offlineLayout->setSpacing(8);
    auto* offlineHint = new QLabel(i18n_->text("app.activation.offline_description"), offlinePanel);
    offlineHint->setObjectName("sectionHint");
    offlineHint->setWordWrap(true);
    offlineLayout->addWidget(offlineHint);
    auto* offlineLabel = new QLabel(i18n_->text("app.activation.offline_license_label"), offlinePanel);
    offlineLayout->addWidget(offlineLabel);
    offlineKeyEdit_ = new QPlainTextEdit(offlinePanel);
    offlineKeyEdit_->setPlaceholderText(i18n_->text("app.activation.paste_placeholder"));
    offlineKeyEdit_->setMinimumHeight(96);
    offlineLayout->addWidget(offlineKeyEdit_);
    offlineActivateBtn_ = new QPushButton(i18n_->text("app.activation.activate_btn"), offlinePanel);
    offlineActivateBtn_->setObjectName("secondaryAction");
    offlineLayout->addWidget(offlineActivateBtn_, 0, Qt::AlignRight);
    offlinePanel->hide();
    layout->addWidget(offlinePanel);

    auto* statusLayout = new QHBoxLayout;
    statusLabel_ = new QLabel(this);
    statusLabel_->setObjectName("statusLabel");
    statusLabel_->setWordWrap(true);
    refreshBtn_ = new QPushButton(i18n_->text("app.activation.status_refresh"), this);
    refreshBtn_->setObjectName("secondaryAction");
    statusLayout->addWidget(statusLabel_, 1);
    statusLayout->addWidget(refreshBtn_, 0, Qt::AlignTop);
    layout->addLayout(statusLayout);
    statusLabel_->hide();
    refreshBtn_->hide();
    layout->addStretch();

    setStyleSheet(
        "QLabel#activationTitle { font-size: 22px; font-weight: 600; }"
        "QLabel#sectionTitle { font-size: 15px; font-weight: 600; }"
        "QLabel#sectionHint, QLabel#actionHint { color: palette(mid); }"
        "QFrame#machinePanel, QFrame#offlinePanel { border: 1px solid palette(mid); border-radius: 8px; }"
        "QFrame#sectionSeparator { color: palette(mid); }"
        "QPushButton, QToolButton { min-height: 34px; padding: 0 14px; border: 1px solid palette(mid); border-radius: 4px; background: palette(button); color: palette(button-text); }"
        "QLineEdit { min-height: 34px; padding: 0 8px; border: 1px solid palette(mid); border-radius: 4px; background: palette(base); color: palette(text); }"
        "QPlainTextEdit { padding: 6px 8px; border: 1px solid palette(mid); border-radius: 4px; background: palette(base); color: palette(text); }"
        "QPushButton:hover, QToolButton:hover { background: palette(light); }"
        "QPushButton:pressed, QToolButton:pressed { background: palette(mid); }"
        "QPushButton:disabled, QToolButton:disabled { color: palette(mid); }"
        "QPushButton#primaryAction { font-weight: 600; background: palette(highlight); color: palette(highlighted-text); border-color: palette(highlight); }"
        "QPushButton#primaryAction:hover { background: palette(dark); border-color: palette(dark); }"
        "QToolButton#offlineToggle { text-align: left; padding: 0 10px; }"
        "QLabel#statusLabel { padding: 6px 0; }"
    );

    connect(copyBtn_, &QPushButton::clicked, this, &ActivationDialog::onCopyMachineId);
    connect(requestBtn_, &QPushButton::clicked, this, &ActivationDialog::onRequestActivation);
    connect(inviteBtn_, &QPushButton::clicked, this, &ActivationDialog::onInviteCode);
    connect(offlineActivateBtn_, &QPushButton::clicked, this, &ActivationDialog::onOfflineActivate);
    connect(refreshBtn_, &QPushButton::clicked, this, &ActivationDialog::onRefreshStatus);
    connect(offlineToggle, &QToolButton::toggled, this, [this, offlineToggle, offlinePanel, layout](bool expanded) {
        offlineToggle->setArrowType(expanded ? Qt::DownArrow : Qt::RightArrow);
        offlinePanel->setVisible(expanded);
        layout->activate();
        const int requiredHeight = qMax(minimumHeight(), layout->sizeHint().height());
        if (expanded) {
            if (height() < requiredHeight) {
                resize(width(), requiredHeight);
            }
        } else if (height() > requiredHeight) {
            resize(width(), requiredHeight);
        }
    });

    // 初始窗口高度按折叠态内容所需，避免硬编码高度留下大块空白。
    layout->activate();
    resize(600, qMax(minimumHeight(), layout->sizeHint().height()));
}

void ActivationDialog::setState(const QString& stateLabel, bool showStatusBar) {
    statusLabel_->setText(stateLabel);
    statusLabel_->setVisible(showStatusBar);
    refreshBtn_->setVisible(showStatusBar);

    const bool onlineRequestPending = state_ == REGISTERING || state_ == ACTIVATING;
    const bool waitingApproval = state_ == WAITING_APPROVAL;
    requestBtn_->setEnabled(!onlineRequestPending && !waitingApproval);
    inviteBtn_->setEnabled(!onlineRequestPending && !waitingApproval);
    inviteCodeEdit_->setEnabled(!onlineRequestPending && !waitingApproval);
    refreshBtn_->setEnabled(!onlineRequestPending);
    offlineActivateBtn_->setEnabled(state_ != VERIFYING);
}

void ActivationDialog::startPolling() {
    if (!pollTimer_->isActive()) {
        pollTimer_->start();
    }
}

void ActivationDialog::stopPolling() {
    pollTimer_->stop();
}

void ActivationDialog::onRequestActivation() {
    state_ = REGISTERING;
    setState(i18n_->text("app.activation.registering"), true);
    client_->registerDevice(canonicalMachineId_, productVersion_);
}

void ActivationDialog::onInviteCode() {
    const QString inviteCode = inviteCodeEdit_->text().trimmed();
    if (inviteCode.isEmpty()) {
        setState(i18n_->text("app.activation.invalid_key"), true);
        return;
    }

    state_ = ACTIVATING;
    setState(i18n_->text("app.activation.activating"), true);
    client_->activateWithCode(canonicalMachineId_, inviteCode, productVersion_);
}

void ActivationDialog::onOfflineActivate() {
    const QString key = offlineKeyEdit_->toPlainText().trimmed();
    if (key.isEmpty()) {
        QMessageBox::warning(this, i18n_->text("app.activation.title"),
                             i18n_->text("app.activation.invalid_key"));
        return;
    }

    const LicenseResult saved = manager_->saveLicense(key);
    if (!saved.ok()) {
        state_ = IDLE;
        setState(licenseErrorText(i18n_, saved.error.code, saved.error.params), true);
        return;
    }

    stopPolling();
    QMessageBox::information(this, i18n_->text("app.activation.title"),
                             i18n_->text("app.activation.activated"));
    accept();
}

void ActivationDialog::onCopyMachineId() {
    QApplication::clipboard()->setText(displayMachineId_);
    setState(i18n_->text("app.activation.copied"), true);
}

void ActivationDialog::onRefreshStatus() {
    client_->checkStatus(canonicalMachineId_);
}

void ActivationDialog::onRegistrationSubmitted(const QString& requestId) {
    Q_UNUSED(requestId);
    state_ = WAITING_APPROVAL;
    setState(i18n_->text("app.activation.status_waiting"), true);
    startPolling();
}
void ActivationDialog::onRegistrationAlreadyExists() {
    state_ = WAITING_APPROVAL;
    setState(i18n_->text("app.activation.status_waiting"), true);
    startPolling();
    onRefreshStatus();
}

void ActivationDialog::onActivationSuccess(const QString& licenseKey, const QDate& expiresAt) {
    Q_UNUSED(expiresAt);
    // Server granted this machine a license: persist via the online path,
    // the only one allowed to reset a rolled-back/missing watermark.
    const LicenseResult saved = manager_->saveOnlineLicense(licenseKey);
    if (!saved.ok()) {
        state_ = IDLE;
        setState(licenseErrorText(i18n_, saved.error.code, saved.error.params), true);
        return;
    }

    stopPolling();
    QMessageBox::information(this, i18n_->text("app.activation.title"),
                             i18n_->text("app.activation.activated"));
    inviteCodeEdit_->clear();
    accept();
}

void ActivationDialog::onActivationFailed(const QString& errorCode, const QJsonObject& params) {
    stopPolling();
    state_ = IDLE;
    setState(activationErrorText(i18n_, errorCode, params), true);
}

void ActivationDialog::onStatusReceived(const QString& status, const QDate& expiresAt) {
    Q_UNUSED(expiresAt);
    if (status == QStringLiteral("active")) {
        state_ = ACTIVATING;
        setState(i18n_->text("app.activation.activating"), true);
        client_->fetchLicense(canonicalMachineId_, productVersion_);
        return;
    }

    if (status == QStringLiteral("pending_approval")) {
        state_ = WAITING_APPROVAL;
        setState(i18n_->text("app.activation.status_waiting"), true);
        startPolling();
        return;
    }

    state_ = IDLE;
    stopPolling();
    setState(statusText(i18n_, status), true);
}

void ActivationDialog::onNetworkError(const QString& detail) {
    stopPolling();
    state_ = IDLE;
    const QString base = i18n_->text("app.activation.network_unavailable");
    const QString text = detail.isEmpty() ? base
                                          : base + QStringLiteral(" (") + detail + QStringLiteral(")");
    setState(text, true);
}

}  // namespace bf
