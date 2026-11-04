#include "ui/activation_dialog.h"

#include "core/version.h"
#include "i18n/i18n_service.h"
#include "license/license_manager.h"
#include "license/machine_id.h"
#include "license/online_activation_client.h"
#include "ui/collapsible_panel.h"
#include "ui/fluent_controls.h"

#include <QApplication>
#include <QClipboard>
#include <QDebug>
#include <QDateTime>
#include <QFile>
#include <QFontMetrics>
#include <QHBoxLayout>
#include <QLabel>
#include <QMessageBox>
#include <QNetworkAccessManager>
#include <QNetworkProxy>
#include <QSysInfo>
#include <QTextLayout>
#include <QTimer>
#include <QVBoxLayout>

namespace bf {
namespace {
using InfoBar = fluent::status_info::InfoBar;

InfoBar::InfoBarSeverity licenseSeverity(LicenseStatus status) {
    return status == LicenseStatus::Expired ? InfoBar::Warning : InfoBar::Error;
}

QUrl serverUrl() {
    return QUrl(qEnvironmentVariable("BF_ACTIVATION_SERVER_URL", "https://license-activate.unitc.cn"));
}

QString licenseErrorText(I18nService* i18n, const LicenseResult& result) {
    switch (result.status) {
    case LicenseStatus::Valid:
    case LicenseStatus::Missing:
        return {};
    case LicenseStatus::Expired:
        return i18n->text("app.activation.local_expired", {{"expires_at", result.error.params.value("expires_at")}});
    case LicenseStatus::WrongMachine:
        return i18n->text("app.activation.wrong_machine");
    case LicenseStatus::ClockRollback: {
        QJsonObject params = result.error.params;
        for (const char* key : {"last_seen_utc", "current_utc"}) {
            const QDateTime time = QDateTime::fromString(params.value(key).toString(), Qt::ISODateWithMs);
            params.insert(key, time.toLocalTime().toString("yyyy-MM-dd HH:mm"));
        }
        return i18n->text("error.license.clock_rollback", params);
    }
    case LicenseStatus::ClockStateMissing:
        return i18n->text("error.license.clock_state_missing");
    case LicenseStatus::ClockStateCorrupted:
        return i18n->text("error.license.clock_state_corrupted");
    case LicenseStatus::ClockStateWriteFailure:
        return i18n->text("error.license.clock_state_write_failed");
    case LicenseStatus::SaveFailed:
        return i18n->text("error.license.save_failed");
    default:
        return i18n->text("app.activation.invalid_key");
    }
}

QString activationErrorText(I18nService* i18n, const QString& code, const QJsonObject& params) {
    if (code == QLatin1String("activation.machine_id_required") || code == QLatin1String("activation.invalid_machine_id"))
        return i18n->text("app.activation.error_invalid_machine_id");
    if (code == QLatin1String("activation.machine_already_bound"))
        return i18n->text("app.activation.status_machine_already_bound");
    if (code == QLatin1String("activation.signing_failed"))
        return i18n->text("app.activation.error_signing_failed");
    if (code == QLatin1String("rate_limit.exceeded"))
        return i18n->text("app.activation.error_rate_limited", {{"retry_after", QString::number(params.value("retry_after").toInt())}});
    return i18n->text("app.activation.request_failed");
}
}  // namespace

ActivationDialog::ActivationDialog(LicenseManager* manager, const VersionInfo& version,
                                   I18nService* i18n, const LicenseResult& initialLicenseResult, QWidget* parent)
    : QDialog(parent), manager_(manager), i18n_(i18n), productVersion_(version.version),
      canonicalMachineId_(machineIdCanonical(readMachineGuid())), displayMachineId_(currentMachineId()),
      client_(nullptr), pollTimer_(new QTimer(this)) {
    auto* nam = new QNetworkAccessManager(this);
    nam->setProxy(QNetworkProxy(QNetworkProxy::NoProxy));
    client_ = new OnlineActivationClient(nam, serverUrl(), this);
    connect(client_, &OnlineActivationClient::registrationSubmitted, this, &ActivationDialog::onRegistrationSubmitted);
    connect(client_, &OnlineActivationClient::registrationAlreadyExists, this, &ActivationDialog::onRegistrationAlreadyExists);
    connect(client_, &OnlineActivationClient::activationSuccess, this, &ActivationDialog::onActivationSuccess);
    connect(client_, &OnlineActivationClient::activationFailed, this, &ActivationDialog::onActivationFailed);
    connect(client_, &OnlineActivationClient::statusReceived, this, &ActivationDialog::onStatusReceived);
    connect(client_, &OnlineActivationClient::networkError, this, &ActivationDialog::onNetworkError);
    pollTimer_->setInterval(30'000);
    connect(pollTimer_, &QTimer::timeout, this, &ActivationDialog::onRefreshStatus);
    buildUi();

    const QString message = canonicalMachineId_.isEmpty() ? i18n_->text("app.activation.machine_id_unavailable")
                                                          : licenseErrorText(i18n_, initialLicenseResult);
    setStatus(message, canonicalMachineId_.isEmpty() ? InfoBar::Error : licenseSeverity(initialLicenseResult.status));
    setBusy(false);
    if (!canonicalMachineId_.isEmpty()) onRefreshStatus();
}

bool ActivationDialog::requiresOnlineStatusCheck(const LicenseResult& savedLicense) {
    return savedLicense.ok() && savedLicense.payload.value("mode").toString() == QLatin1String("online");
}

void ActivationDialog::checkAndHandleRevocation(LicenseManager* manager, const VersionInfo& version,
                                                  I18nService* i18n, QWidget* parent) {
    auto* nam = new QNetworkAccessManager(parent);
    nam->setProxy(QNetworkProxy(QNetworkProxy::NoProxy));
    auto* client = new OnlineActivationClient(nam, serverUrl(), nam);
    connect(client, &OnlineActivationClient::statusReceived, nam,
            [manager, version, i18n, parent, nam](const QString& status, const QDate&) {
        if (status == QLatin1String("revoked") || status == QLatin1String("rejected") || status == QLatin1String("not_found")) {
            QFile::remove(manager->licenseFilePath());
            QMessageBox::warning(parent, i18n->text("app.activation.title"), i18n->text("app.activation.revoked_on_server"));
            LicenseResult missing;
            missing.status = LicenseStatus::Missing;
            ActivationDialog dialog(manager, version, i18n, missing, parent);
            if (dialog.exec() != QDialog::Accepted) parent->close();
        }
        nam->deleteLater();
    });
    connect(client, &OnlineActivationClient::networkError, nam, &QObject::deleteLater);
    connect(client, &OnlineActivationClient::activationFailed, nam, &QObject::deleteLater);
    client->checkStatus(machineIdCanonical(readMachineGuid()));
}

void ActivationDialog::buildUi() {
    setWindowTitle(i18n_->text("app.activation.title"));
    setStyleSheet(nativeFluentStyle());
    setPalette(fluentPalette());
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(28, 28, 28, 24);
    root->setSpacing(12);
    root->setSizeConstraint(QLayout::SetFixedSize);
    auto* title = new fluent::textfields::Label(i18n_->text("app.activation.title"), this);
    title->setFluentTypography(Typography::FontRole::Title);
    root->addWidget(title);
    auto* machineLabel = new QLabel(i18n_->text("app.activation.machine_id_label"), this);
    root->addWidget(machineLabel);
    auto* machineRow = new QHBoxLayout;
    machineIdEdit_ = new TextInput(displayMachineId_, this);
    machineIdEdit_->setObjectName("machineIdEdit");
    machineIdEdit_->setReadOnly(true);
    machineLabel->setBuddy(machineIdEdit_);
    copyBtn_ = new ActionButton(i18n_->text("app.activation.copy"), this);
    copyBtn_->setObjectName("copyButton");
    machineRow->addWidget(machineIdEdit_, 1);
    machineRow->addWidget(copyBtn_);
    root->addLayout(machineRow);

    requestPanel_ = new QWidget(this);
    requestPanel_->setObjectName("requestPanel");
    auto* requestLayout = new QVBoxLayout(requestPanel_);
    requestLayout->setContentsMargins(0, 0, 0, 0);
    requestLayout->setSpacing(8);
    auto* orgLabel = new QLabel(i18n_->text("app.activation.org_name"), requestPanel_);
    orgNameEdit_ = new TextInput(requestPanel_);
    orgNameEdit_->setObjectName("orgNameEdit");
    orgNameEdit_->setMaxLength(128);
    orgLabel->setBuddy(orgNameEdit_);
    requestLayout->addWidget(orgLabel);
    requestBtn_ = new ActionButton(i18n_->text("app.activation.request"), requestPanel_);
    requestBtn_->setObjectName("requestButton");
    requestBtn_->setFluentStyle(fluent::basicinput::Button::Accent);
    auto* requestRow = new QHBoxLayout;
    requestRow->addWidget(orgNameEdit_, 1);
    requestRow->addWidget(requestBtn_);
    requestLayout->addLayout(requestRow);
    auto* hint = new fluent::textfields::Label(i18n_->text("app.activation.approval_hint"), requestPanel_);
    hint->setObjectName("approvalHint");
    hint->setTextColorRole(fluent::textfields::Label::TextColorRole::Secondary);
    hint->setWordWrap(true);
    requestLayout->addWidget(hint);
    root->addWidget(requestPanel_);

    statusBar_ = new InfoBar(this);
    statusBar_->setObjectName("statusLabel");
    statusBar_->setIsClosable(false);
    statusBar_->setTitleFontRole(Typography::FontRole::Body);
    statusBar_->setMultiLineMinHeight(statusBar_->singleLineHeight());
    statusBar_->setMultiLineActionMinHeight(90);
    QSizePolicy statusPolicy(QSizePolicy::Expanding, QSizePolicy::Minimum);
    statusPolicy.setRetainSizeWhenHidden(true);
    statusBar_->setSizePolicy(statusPolicy);
    refreshBtn_ = new ActionButton(this);
    refreshBtn_->setObjectName("refreshButton");
    root->addWidget(statusBar_);

    auto* offlinePanel = new CollapsiblePanel(i18n_->text("app.activation.offline_section"), this);
    offlinePanel->setObjectName("offlinePanel");
    offlinePanel->headerButton()->setObjectName("offlineToggle");
    offlineKeyEdit_ = new fluent::textfields::TextEdit(offlinePanel);
    offlineKeyEdit_->setObjectName("offlineKeyEdit");
    offlineKeyEdit_->setPlaceholderText(i18n_->text("app.activation.paste_placeholder"));
    offlineKeyEdit_->setAccessibleName(i18n_->text("app.activation.offline_license_label"));
    offlineKeyEdit_->setAutoResizeEnabled(false);
    offlineKeyEdit_->setFixedHeight(100);
    offlineActivateBtn_ = new ActionButton(i18n_->text("app.activation.activate_btn"), offlinePanel);
    offlineActivateBtn_->setObjectName("offlineActivateButton");
    auto* offlineLayout = new QVBoxLayout;
    offlineLayout->setContentsMargins(14, 12, 14, 12);
    offlineLayout->setSpacing(8);
    offlineLayout->addWidget(offlineKeyEdit_);
    offlineLayout->addWidget(offlineActivateBtn_, 0, Qt::AlignRight);
    offlinePanel->setContentLayout(offlineLayout);
    offlinePanel->setExpanded(false);
    root->addWidget(offlinePanel);

    for (auto* button : {copyBtn_, requestBtn_, offlineActivateBtn_, refreshBtn_})
        button->setFluentSize(fluent::basicinput::Button::StandardSize);
    connect(copyBtn_, &QPushButton::clicked, this, &ActivationDialog::onCopyMachineId);
    connect(requestBtn_, &QPushButton::clicked, this, &ActivationDialog::onRequestActivation);
    connect(offlineActivateBtn_, &QPushButton::clicked, this, &ActivationDialog::onOfflineActivate);
    connect(refreshBtn_, &QPushButton::clicked, this, &ActivationDialog::onRefreshStatus);
}

void ActivationDialog::setStatus(const QString& text, InfoBar::InfoBarSeverity severity, const QString& action) {
    refreshBtn_->setText(action);
    const QFont font = statusBar_->themeFont(Typography::FontRole::Body).toQFont();
    const QMargins margins = statusBar_->contentMargins();
    const int textWidth = statusBar_->preferredWidth() - margins.left() - margins.right() -
                          statusBar_->severityIconSize() - statusBar_->iconTextSpacing();
    QTextLayout lines(text, font);
    QTextOption option;
    option.setWrapMode(QTextOption::WrapAtWordBoundaryOrAnywhere);
    lines.setTextOption(option);
    lines.beginLayout();
    QTextLine firstLine = lines.createLine();
    if (firstLine.isValid()) firstLine.setLineWidth(textWidth);
    const int firstLength = firstLine.isValid() ? firstLine.textLength() : 0;
    lines.endLayout();
    const QString remainder = text.mid(firstLength).trimmed();
    const bool fitsAction = action.isEmpty() || QFontMetrics(font).horizontalAdvance(text) +
                            refreshBtn_->sizeHint().width() + 8 <= textWidth;
    statusBar_->setSeverity(severity);
    // Preserve the bar's natural width when hidden, without reserving its height.
    QSizePolicy policy = statusBar_->sizePolicy();
    policy.setVerticalPolicy(text.isEmpty() ? QSizePolicy::Ignored : QSizePolicy::Minimum);
    statusBar_->setSizePolicy(policy);
    statusBar_->setVisible(!text.isEmpty());
    statusBar_->setSingleLine(remainder.isEmpty() && fitsAction);
    statusBar_->setTitle(text.left(firstLength).trimmed());
    statusBar_->setMessage(remainder);
    if (action.isEmpty()) {
        statusBar_->setActionWidget(nullptr);
        refreshBtn_->setParent(this);
        refreshBtn_->hide();
    } else {
        statusBar_->setActionWidget(refreshBtn_);
    }
}

void ActivationDialog::setBusy(bool busy) {
    busy_ = busy;
    const bool available = !canonicalMachineId_.isEmpty();
    requestBtn_->setEnabled(available && !busy);
    orgNameEdit_->setEnabled(available && !busy);
    refreshBtn_->setEnabled(available && !busy);
    copyBtn_->setEnabled(available);
    offlineActivateBtn_->setEnabled(available);
    offlineKeyEdit_->setEnabled(available);
}

void ActivationDialog::onRequestActivation() {
    setBusy(true);
    setStatus(i18n_->text("app.activation.registering"), InfoBar::Informational);
    client_->registerDevice(canonicalMachineId_, productVersion_, QSysInfo::machineHostName(),
                            QSysInfo::prettyProductName(), orgNameEdit_->text().trimmed());
}

void ActivationDialog::onOfflineActivate() {
    const QString key = offlineKeyEdit_->toPlainText().trimmed();
    if (key.isEmpty()) {
        setStatus(i18n_->text("app.activation.empty_license"), InfoBar::Error);
        return;
    }
    const LicenseResult saved = manager_->saveLicense(key);
    if (!saved.ok()) {
        setStatus(licenseErrorText(i18n_, saved), licenseSeverity(saved.status));
        return;
    }
    finishActivation(QDate::fromString(saved.payload.value("expiry").toString(), Qt::ISODate));
}

void ActivationDialog::onCopyMachineId() {
    QApplication::clipboard()->setText(displayMachineId_);
    copyBtn_->setText(i18n_->text("app.activation.copied"));
    QTimer::singleShot(2000, copyBtn_, [this] { copyBtn_->setText(i18n_->text("app.activation.copy")); });
}

void ActivationDialog::onRefreshStatus() {
    if (busy_) return;
    setBusy(true);
    if (statusBar_->isHidden()) setStatus(i18n_->text("app.activation.checking"), InfoBar::Informational);
    client_->checkStatus(canonicalMachineId_);
}

void ActivationDialog::onRegistrationSubmitted(const QString&) {
    setBusy(false);
    requestPanel_->hide();
    setStatus(i18n_->text("app.activation.status_waiting"), InfoBar::Informational, i18n_->text("app.activation.status_refresh"));
    pollTimer_->start();
    onRefreshStatus();
}
void ActivationDialog::onRegistrationAlreadyExists() { onRegistrationSubmitted({}); }

void ActivationDialog::finishActivation(const QDate& expiresAt) {
    pollTimer_->stop();
    client_->disconnect(this);
    QMessageBox::information(this, i18n_->text("app.activation.title"),
                             i18n_->text("app.activation.activated", {{"expires_at", expiresAt.toString(Qt::ISODate)}}));
    accept();
}

void ActivationDialog::onActivationSuccess(const QString& licenseKey, const QDate& expiresAt) {
    const LicenseResult saved = manager_->saveOnlineLicense(licenseKey);
    if (!saved.ok()) {
        setBusy(false);
        setStatus(licenseErrorText(i18n_, saved), licenseSeverity(saved.status),
                  i18n_->text("app.activation.status_refresh"));
        return;
    }
    finishActivation(expiresAt);
}

void ActivationDialog::onActivationFailed(const QString& errorCode, const QJsonObject& params) {
    setBusy(false);
    if (errorCode == QLatin1String("activation.expired_or_revoked")) {
        onStatusReceived(params.value("status").toString(), {});
        return;
    }
    const auto severity = errorCode == QLatin1String("activation.invalid_machine_id") ||
                          errorCode == QLatin1String("activation.machine_id_required") ? InfoBar::Error : InfoBar::Warning;
    setStatus(activationErrorText(i18n_, errorCode, params), severity, i18n_->text("app.activation.retry"));
}

void ActivationDialog::onStatusReceived(const QString& status, const QDate&) {
    setBusy(false);
    if (status == QLatin1String("active")) {
        pollTimer_->stop();
        requestPanel_->hide();
        setBusy(true);
        setStatus(i18n_->text("app.activation.activating"), InfoBar::Informational);
        client_->fetchLicense(canonicalMachineId_, productVersion_);
    } else if (status == QLatin1String("pending_approval")) {
        requestPanel_->hide();
        if (!pollTimer_->isActive()) pollTimer_->start();
        setStatus(i18n_->text("app.activation.status_waiting"), InfoBar::Informational, i18n_->text("app.activation.status_refresh"));
    } else if (status == QLatin1String("not_found")) {
        pollTimer_->stop();
        requestPanel_->show();
        setStatus({}, InfoBar::Informational);
    } else if (status == QLatin1String("expired") || status == QLatin1String("revoked") || status == QLatin1String("rejected")) {
        pollTimer_->stop();
        requestPanel_->hide();
        const char* key = status == QLatin1String("expired") ? "app.activation.status_expired"
            : status == QLatin1String("revoked") ? "app.activation.status_revoked" : "app.activation.status_rejected";
        setStatus(i18n_->text(key), InfoBar::Warning, i18n_->text("app.activation.status_refresh"));
    } else {
        requestPanel_->show();
        setStatus(i18n_->text("app.activation.request_failed"), InfoBar::Warning, i18n_->text("app.activation.retry"));
    }
}

void ActivationDialog::onNetworkError(const QString& detail) {
    qWarning() << "Activation network error:" << detail;
    setBusy(false);
    requestPanel_->show();
    setStatus(i18n_->text("app.activation.network_unavailable"), InfoBar::Warning, i18n_->text("app.activation.retry"));
}

}  // namespace bf
