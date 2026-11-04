#include "core/version.h"
#include "i18n/i18n_service.h"
#include "license/license_manager.h"
#include "license/machine_id.h"
#include "ui/activation_dialog.h"
#include "ui/collapsible_panel.h"
#include "ui/fluent_controls.h"

#include <QApplication>
#include <QAbstractButton>
#include <QClipboard>
#include <QDateTime>
#include <QElapsedTimer>
#include <QFile>
#include <QFontMetrics>
#include <QJsonDocument>
#include <QLabel>
#include <QMessageBox>
#include <QNetworkAccessManager>
#include <QTemporaryDir>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTextEdit>
#include <QThread>
#include <QTimer>

#ifndef Q_OS_WIN
#include <openssl/evp.h>
#include <openssl/pem.h>
#include <openssl/rsa.h>
#endif

#include <functional>
#include <iostream>

namespace {
using InfoBar = fluent::status_info::InfoBar;
int failures = 0;
void check(bool condition, const char* message) {
    if (!condition) { std::cerr << "FAIL: " << message << '\n'; ++failures; }
}

bool waitFor(const std::function<bool()>& condition, int timeoutMs = 3000) {
    QElapsedTimer timer;
    timer.start();
    while (!condition() && timer.elapsed() < timeoutMs) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
        QThread::msleep(2);
    }
    QCoreApplication::processEvents();
    return condition();
}

class TestServer {
public:
    TestServer() {
        QObject::connect(&server, &QTcpServer::newConnection, &server, [this] {
            while (auto* socket = server.nextPendingConnection()) {
                QObject::connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
                QObject::connect(socket, &QTcpSocket::readyRead, socket, [this, socket] {
                    QByteArray data = socket->property("request").toByteArray() + socket->readAll();
                    const int headerEnd = data.indexOf("\r\n\r\n");
                    if (headerEnd < 0) { socket->setProperty("request", data); return; }
                    int length = 0;
                    for (const auto& line : data.left(headerEnd).split('\n'))
                        if (line.toLower().startsWith("content-length:")) length = line.mid(15).trimmed().toInt();
                    if (data.size() < headerEnd + 4 + length) { socket->setProperty("request", data); return; }
                    const QByteArray path = data.left(data.indexOf("\r\n")).split(' ').value(1);
                    int code = 200;
                    QJsonObject response;
                    if (path.startsWith("/api/v1/status?")) {
                        ++statusCount;
                        if (holdStatus) return;
                        if (status == QLatin1String("rate_limit")) {
                            code = 429;
                            response = {{"error", QJsonObject{{"code", "rate_limit.exceeded"}}}};
                        } else {
                            response = {{"status", status}, {"expires_at", "2099-12-31"}};
                        }
                    } else if (path == "/api/v1/register") {
                        ++registrationCount;
                        registration = QJsonDocument::fromJson(data.mid(headerEnd + 4, length)).object();
                        status = QStringLiteral("pending_approval");
                        if (alreadyExists) {
                            code = 409;
                            response = {{"error", QJsonObject{{"code", "activation.already_exists"}}}};
                        } else {
                            code = 202;
                            response = {{"request_id", "test-request"}};
                        }
                    } else if (path == "/api/v1/activate") {
                        ++activationCount;
                        response = {{"license_key", license}, {"expires_at", "2099-12-31"}};
                    } else {
                        code = 404;
                    }
                    const QByteArray body = QJsonDocument(response).toJson(QJsonDocument::Compact);
                    socket->write("HTTP/1.1 " + QByteArray::number(code) + " OK\r\nContent-Type: application/json\r\nContent-Length: " + QByteArray::number(body.size()) + "\r\nConnection: close\r\n\r\n" + body);
                    socket->disconnectFromHost();
                });
            }
        });
    }
    bool listen() { return server.listen(QHostAddress::LocalHost); }
    QTcpServer server;
    QString status = QStringLiteral("not_found");
    QString license;
    QJsonObject registration;
    bool alreadyExists = false;
    bool holdStatus = false;
    int statusCount = 0;
    int registrationCount = 0;
    int activationCount = 0;
};

#ifndef Q_OS_WIN
QString makeLicense(const QString& root, const QString& machineId) {
    EVP_PKEY_CTX* gen = EVP_PKEY_CTX_new_id(EVP_PKEY_RSA, nullptr);
    EVP_PKEY* key = nullptr;
    const bool generated = gen && EVP_PKEY_keygen_init(gen) > 0 && EVP_PKEY_CTX_set_rsa_keygen_bits(gen, 2048) > 0 && EVP_PKEY_keygen(gen, &key) > 0;
    EVP_PKEY_CTX_free(gen);
    if (!generated) return {};
    BIO* bio = BIO_new(BIO_s_mem());
    const bool exported = bio && PEM_write_bio_PUBKEY(bio, key) == 1;
    char* pem = nullptr;
    const long pemSize = exported ? BIO_get_mem_data(bio, &pem) : 0;
    QFile publicFile(QDir(root).filePath("license_public.pem"));
    const bool saved = publicFile.open(QIODevice::WriteOnly) && publicFile.write(pem, pemSize) == pemSize;
    publicFile.close();
    BIO_free(bio);
    const QByteArray payload = QJsonDocument(QJsonObject{{"machine_id", machineId}, {"expiry", "2099-12-31"},
                                                       {"version", "0.2.2-beta.1"}, {"mode", "online"}}).toJson(QJsonDocument::Compact);
    EVP_MD_CTX* md = EVP_MD_CTX_new();
    EVP_PKEY_CTX* sign = nullptr;
    bool ok = saved && md && EVP_DigestSignInit(md, &sign, EVP_sha256(), nullptr, key) > 0 &&
        EVP_PKEY_CTX_set_rsa_padding(sign, RSA_PKCS1_PSS_PADDING) > 0 &&
        EVP_PKEY_CTX_set_rsa_pss_saltlen(sign, RSA_PSS_SALTLEN_MAX) > 0 &&
        EVP_DigestSignUpdate(md, payload.constData(), static_cast<size_t>(payload.size())) > 0;
    size_t signatureSize = 0;
    ok = ok && EVP_DigestSignFinal(md, nullptr, &signatureSize) > 0;
    QByteArray signature(static_cast<int>(signatureSize), '\0');
    ok = ok && EVP_DigestSignFinal(md, reinterpret_cast<unsigned char*>(signature.data()), &signatureSize) > 0;
    EVP_MD_CTX_free(md);
    EVP_PKEY_free(key);
    if (!ok) return {};
    QByteArray envelope;
    for (int shift = 24; shift >= 0; shift -= 8) envelope.append(static_cast<char>((payload.size() >> shift) & 0xff));
    envelope += payload + signature;
    return QString::fromLatin1(envelope.toBase64(QByteArray::Base64UrlEncoding | QByteArray::OmitTrailingEquals));
}
#endif

bf::LicenseResult missing() {
    bf::LicenseResult result;
    result.status = bf::LicenseStatus::Missing;
    return result;
}

InfoBar* bar(bf::ActivationDialog& dialog) { return dialog.findChild<InfoBar*>("statusLabel"); }
QString statusText(InfoBar* info) { return info->title() + info->message(); }
bool statusMatches(InfoBar* info, QString expected) {
    return statusText(info).simplified().remove(' ') == expected.simplified().remove(' ');
}
fluent::basicinput::Button* button(bf::ActivationDialog& dialog, const char* name) {
    return dialog.findChild<fluent::basicinput::Button*>(name);
}

bool fitsParents(QWidget* widget, QWidget* root) {
    QRect bounds = widget->rect();
    while (widget != root && widget->parentWidget()) {
        bounds.moveTopLeft(widget->mapToParent(bounds.topLeft()));
        widget = widget->parentWidget();
        if (!widget->rect().contains(bounds)) return false;
    }
    return widget == root;
}

void checkVisibleText(bf::ActivationDialog& dialog, const QString& context) {
    QCoreApplication::processEvents();
    check(dialog.width() <= 720, qPrintable(context + ": window width exceeds 720 pixels"));
    for (auto* label : dialog.findChildren<QLabel*>()) {
        if (!label->isVisible()) continue;
        auto* fluentLabel = qobject_cast<fluent::textfields::Label*>(label);
        const QString text = fluentLabel ? fluentLabel->text() : label->text();
        if (text.isEmpty()) continue;
        const QRect content = label->contentsRect();
        const QFontMetrics metrics(label->font());
        const QRect bounds = label->wordWrap()
            ? metrics.boundingRect(QRect(0, 0, content.width(), 10000), Qt::TextWordWrap, text)
            : QRect(0, 0, metrics.horizontalAdvance(text), metrics.height());
        const QString description = QString("%1: label %2 [%3], text %4x%5, content %6x%7, elided=%8, contained=%9")
            .arg(context, label->objectName(), text).arg(bounds.width()).arg(bounds.height())
            .arg(content.width()).arg(content.height()).arg(fluentLabel && fluentLabel->isTextElided())
            .arg(fitsParents(label, &dialog));
        check((!fluentLabel || !fluentLabel->isTextElided()) && bounds.width() <= content.width() &&
              bounds.height() <= content.height() && fitsParents(label, &dialog), qPrintable(description));
    }
    for (auto* action : dialog.findChildren<QAbstractButton*>()) {
        if (!action->isVisible() || action->text().isEmpty()) continue;
        const QString description = context + ": button " + action->objectName() + " [" + action->text() + "]";
        check(action->width() >= action->sizeHint().width() && action->height() >= action->sizeHint().height() &&
              fitsParents(action, &dialog), qPrintable(description));
    }
    auto* machine = dialog.findChild<fluent::textfields::LineEdit*>("machineIdEdit");
    check(machine->width() >= QFontMetrics(machine->font()).horizontalAdvance(machine->text()) + 16,
          qPrintable(context + ": machine ID is clipped"));
    if (!bar(dialog)->isHidden()) {
        auto* title = bar(dialog)->findChild<fluent::textfields::Label*>("InfoBarTitleLabel");
        auto* label = bar(dialog)->findChild<fluent::textfields::Label*>("InfoBarMessageLabel");
        check(title && title->text() == bar(dialog)->title() && title->isVisible() &&
              !title->isTextElided() && title->height() > 0 && fitsParents(title, &dialog),
              "screenshot first message line is fully visible");
        if (!bar(dialog)->message().isEmpty())
            check(label && label->text() == bar(dialog)->message() && label->isVisible() &&
                  label->height() > 0 && fitsParents(label, &dialog), "screenshot remaining message is fully visible");
        if (!bar(dialog)->singleLine() && !bar(dialog)->actionWidget() && label)
            check(bar(dialog)->height() - label->geometry().bottom() - 1 <= bar(dialog)->contentMargins().bottom() + 1,
                  qPrintable(context + ": multiline message has excess bottom space"));
    }
    auto* offline = dialog.findChild<bf::CollapsiblePanel*>("offlinePanel");
    if (offline->isExpanded()) {
        auto* edit = dialog.findChild<fluent::textfields::TextEdit*>("offlineKeyEdit");
        check(edit->height() >= 100 && fitsParents(edit, &dialog), "screenshot offline editor is fully visible");
        auto* inner = edit->findChild<QTextEdit*>();
        if (inner && edit->toPlainText().isEmpty())
            check(QFontMetrics(inner->font()).horizontalAdvance(edit->placeholderText()) <= inner->viewport()->width(),
                  qPrintable(context + ": offline placeholder is clipped"));
        else
            check(inner != nullptr, qPrintable(context + ": offline editor is missing"));
    }
}

void screenshot(bf::ActivationDialog& dialog, const QString& dir, const QString& context) {
    checkVisibleText(dialog, context);
    if (!dir.isEmpty()) check(dialog.grab().save(QDir(dir).filePath(context + ".png")), "save screenshot");
}
}  // namespace

int main(int argc, char** argv) {
    if (!qEnvironmentVariableIsSet("QT_QPA_PLATFORM")) qputenv("QT_QPA_PLATFORM", "offscreen");
    QTemporaryDir temp;
    qputenv("XDG_DATA_HOME", temp.path().toUtf8());
    qputenv("APPDATA", temp.path().toUtf8());
    qunsetenv("BF_RESOURCE_ROOT");
#ifndef Q_OS_WIN
    qputenv("BF_MOCK_MACHINE_GUID", "ACTIVATION-TEST");
#endif
    QApplication app(argc, argv);
    check(temp.isValid(), "temporary test directory");
    TestServer server;
    check(server.listen(), "listen on local test server");
    qputenv("BF_ACTIVATION_SERVER_URL", QString("http://127.0.0.1:%1").arg(server.server.serverPort()).toUtf8());
    bf::VersionInfo version;
    version.version = QStringLiteral("0.2.2-beta.1");
#ifndef Q_OS_WIN
    server.license = makeLicense(temp.path(), bf::machineIdCanonical(bf::readMachineGuid()));
    check(!server.license.isEmpty(), "create signed test license in memory");
#endif
    bf::LicenseManager manager(version.version, temp.path());
    bf::I18nService i18n(QStringLiteral(BF_REPO_ROOT));
    QString error;
    check(i18n.loadLocale("en_US", &error), "load translations");
    bf::LicenseResult mode;
    mode.status = bf::LicenseStatus::Valid;
    check(!bf::ActivationDialog::requiresOnlineStatusCheck(mode), "legacy mode is offline");
    mode.payload["mode"] = "managed_offline";
    check(!bf::ActivationDialog::requiresOnlineStatusCheck(mode), "managed offline mode is offline");
    mode.payload["mode"] = "online";
    check(bf::ActivationDialog::requiresOnlineStatusCheck(mode), "online mode is checked");
    mode.status = bf::LicenseStatus::Expired;
    check(!bf::ActivationDialog::requiresOnlineStatusCheck(mode), "invalid local license is not checked at startup");

    // Construction performs a status query; the form is disabled until it returns.
    {
        server.status = "not_found";
        const int before = server.statusCount;
        bf::ActivationDialog dialog(&manager, version, &i18n, missing());
        dialog.show();
        auto* info = bar(dialog);
        auto* form = dialog.findChild<QWidget*>("requestPanel");
        auto* panel = dialog.findChild<bf::CollapsiblePanel*>("offlinePanel");
        check(info && form && panel && !panel->isExpanded(), "Fluent controls and collapsed offline panel");
        check(dialog.findChildren<InfoBar*>().size() == 1 && !info->isClosable(), "one non-closable InfoBar");
        check(!button(dialog, "requestButton")->isEnabled() && statusMatches(info, i18n.text("app.activation.checking")), "initial check disables request");
        check(!dialog.findChild<QWidget*>("inviteCodeEdit"), "no invite input");
        check(waitFor([&] { return server.statusCount > before && info->isHidden(); }), "automatic status query on open");
        check(button(dialog, "requestButton")->isEnabled() && form->isVisible(), "not_found displays request form");
        auto* machine = dialog.findChild<fluent::textfields::LineEdit*>("machineIdEdit");
        auto* org = dialog.findChild<fluent::textfields::LineEdit*>("orgNameEdit");
        auto* hint = dialog.findChild<fluent::textfields::Label*>("approvalHint");
        check(machine->width() >= QFontMetrics(machine->font()).horizontalAdvance(machine->text()) + 16,
              "machine ID fits in the default window");
        check(button(dialog, "copyButton")->height() == machine->height() &&
              button(dialog, "requestButton")->height() == org->height(), "buttons align with input heights");
        check(button(dialog, "requestButton")->y() == org->y() && button(dialog, "requestButton")->x() > org->x(),
              "organization input and request button share a row");
        check(hint && hint->textColorRole() == fluent::textfields::Label::TextColorRole::Secondary,
              "approval hint uses secondary text color");
        for (const char* name : {"copyButton", "requestButton", "refreshButton", "offlineActivateButton"})
            check(button(dialog, name)->fluentSize() == fluent::basicinput::Button::StandardSize,
                  "activation button uses standard size");
        check(button(dialog, "requestButton")->fluentStyle() == fluent::basicinput::Button::Accent &&
              button(dialog, "offlineActivateButton")->fluentStyle() == fluent::basicinput::Button::Standard, "only request button uses accent");
        const int collapsedHeight = dialog.height();
        panel->setExpanded(true);
        check(waitFor([&] { return dialog.height() > collapsedHeight; }), "expanding offline panel grows window");
        auto* edit = dialog.findChild<fluent::textfields::TextEdit*>("offlineKeyEdit");
        check(edit && edit->height() >= 100 && fitsParents(edit, &dialog), "offline input is fully visible");
        const QPoint editPos = edit->mapTo(panel, QPoint());
        const QPoint activatePos = button(dialog, "offlineActivateButton")->mapTo(panel, QPoint());
        check(editPos.x() >= 14 && panel->width() - editPos.x() - edit->width() >= 14 &&
              panel->width() - activatePos.x() - button(dialog, "offlineActivateButton")->width() >= 14,
              "offline editor and action retain content margins");
        button(dialog, "offlineActivateButton")->click();
        check(statusMatches(info, i18n.text("app.activation.empty_license")) && info->severity() == InfoBar::Error, "empty license prompt");
        edit->setPlainText("invalid-license");
        button(dialog, "offlineActivateButton")->click();
        check(statusMatches(info, i18n.text("app.activation.invalid_key")) && info->severity() == InfoBar::Error, "invalid license prompt");
        QCoreApplication::processEvents();
        auto* messageLabel = info->findChild<fluent::textfields::Label*>("InfoBarTitleLabel");
        check(messageLabel && messageLabel->text() == info->title() && messageLabel->isVisible() &&
              !messageLabel->isTextElided() && messageLabel->height() > 0 && fitsParents(messageLabel, &dialog),
              "invalid license message is fully visible");
        panel->setExpanded(false);
        check(waitFor([&] { return dialog.height() < collapsedHeight + 100; }), "collapsing offline panel shrinks window");
        button(dialog, "copyButton")->click();
        check(button(dialog, "copyButton")->text() == i18n.text("app.activation.copied"), "copy button acknowledges copy");
        check(QApplication::clipboard()->text() == machine->text(), "copy button copies machine ID");
        check(waitFor([&] { return button(dialog, "copyButton")->text() == i18n.text("app.activation.copy"); }, 2500), "copy button resets");
    }

    for (const auto& entry : {qMakePair(QString("pending_approval"), QString("app.activation.status_waiting")),
                               qMakePair(QString("expired"), QString("app.activation.status_expired")),
                               qMakePair(QString("revoked"), QString("app.activation.status_revoked")),
                               qMakePair(QString("rejected"), QString("app.activation.status_rejected"))}) {
        server.status = entry.first;
        bf::ActivationDialog dialog(&manager, version, &i18n, missing());
        dialog.show();
        check(waitFor([&] { return statusMatches(bar(dialog), i18n.text(entry.second)); }), "server state shown in InfoBar");
        check(dialog.findChild<QWidget*>("requestPanel")->isHidden(), "server state hides request form");
        check(bar(dialog)->actionWidget() == button(dialog, "refreshButton"), "InfoBar contains check action");
        auto* action = button(dialog, "refreshButton");
        check(bar(dialog)->rect().contains(action->geometry()), "status action fits inside InfoBar");
        if (!bar(dialog)->singleLine())
            check(bar(dialog)->height() - action->geometry().bottom() <= 20, "multiline status has no excess blank area");
        check(bar(dialog)->severity() == (entry.first == "pending_approval" ? InfoBar::Informational : InfoBar::Warning), "server state severity");
        if (entry.first == "pending_approval") {
            const auto timers = dialog.findChildren<QTimer*>();
            check(!timers.isEmpty() && timers.first()->isActive() && timers.first()->interval() == 30000, "pending approval polls every 30 seconds");
        }
    }

    for (bool alreadyExists : {false, true}) {
        server.status = "not_found";
        server.alreadyExists = alreadyExists;
        bf::ActivationDialog dialog(&manager, version, &i18n, missing());
        dialog.show();
        check(waitFor([&] { return bar(dialog)->isHidden(); }), "request form ready");
        dialog.findChild<fluent::textfields::LineEdit*>("orgNameEdit")->setText("Test organization");
        const int before = server.statusCount;
        button(dialog, "requestButton")->click();
        check(waitFor([&] { return server.statusCount > before && statusMatches(bar(dialog), i18n.text("app.activation.status_waiting")); }), "registration immediately checks status");
        check(server.registration.value("org_name").toString() == "Test organization" &&
              !server.registration.value("hostname").toString().isEmpty() && !server.registration.value("os").toString().isEmpty(), "registration includes host, OS, and organization");
    }

    for (const auto& entry : {qMakePair(bf::LicenseStatus::Expired, QString("app.activation.local_expired")),
                               qMakePair(bf::LicenseStatus::WrongMachine, QString("app.activation.wrong_machine")),
                               qMakePair(bf::LicenseStatus::InvalidSignature, QString("app.activation.invalid_key")),
                               qMakePair(bf::LicenseStatus::KeyFormatError, QString("app.activation.invalid_key"))}) {
        bf::LicenseResult initial;
        initial.status = entry.first;
        initial.error.params = {{"expires_at", "2025-01-01"}};
        bf::ActivationDialog dialog(&manager, version, &i18n, initial);
        const QString expected = entry.first == bf::LicenseStatus::Expired ? i18n.text(entry.second, {{"expires_at", "2025-01-01"}}) : i18n.text(entry.second);
        check(statusMatches(bar(dialog), expected), "local license failure is shown during query");
        check(bar(dialog)->severity() == (entry.first == bf::LicenseStatus::Expired ? InfoBar::Warning : InfoBar::Error), "local failure severity");
    }
    {
        bf::LicenseResult clock;
        clock.status = bf::LicenseStatus::ClockRollback;
        clock.error.params = {{"last_seen_utc", "2026-11-04T08:00:00.000Z"},
                              {"current_utc", "2026-11-04T07:30:00.000Z"}};
        bf::ActivationDialog dialog(&manager, version, &i18n, clock);
        const auto localTime = [](const QString& time) {
            return QDateTime::fromString(time, Qt::ISODateWithMs).toLocalTime().toString("yyyy-MM-dd HH:mm");
        };
        const QString expected = i18n.text("error.license.clock_rollback",
            {{"last_seen_utc", localTime("2026-11-04T08:00:00.000Z")},
             {"current_utc", localTime("2026-11-04T07:30:00.000Z")}});
        check(statusMatches(bar(dialog), expected), "clock rollback displays both timestamps in local time");
        check(!statusText(bar(dialog)).contains("T08:00:00.000Z"), "clock rollback omits raw UTC timestamp");
    }
    {
        server.status = "expired";
        bf::LicenseResult expired;
        expired.status = bf::LicenseStatus::Expired;
        expired.error.params = {{"expires_at", "2025-01-01"}};
        bf::ActivationDialog dialog(&manager, version, &i18n, expired);
        dialog.show();
        check(statusText(bar(dialog)).contains("2025-01-01"), "local expiry shown during initial check");
        check(waitFor([&] { return statusMatches(bar(dialog), i18n.text("app.activation.status_expired")); }),
              "server expiry replaces local message");
        check(dialog.findChildren<InfoBar*>().size() == 1, "expiry messages do not accumulate");
    }
#ifndef Q_OS_WIN
    {
        qputenv("BF_MOCK_MACHINE_GUID", "");
        const int before = server.statusCount;
        bf::LicenseResult expired;
        expired.status = bf::LicenseStatus::Expired;
        bf::ActivationDialog dialog(&manager, version, &i18n, expired);
        dialog.show();
        check(statusMatches(bar(dialog), i18n.text("app.activation.machine_id_unavailable")) && bar(dialog)->severity() == InfoBar::Error, "missing machine ID message");
        check(!button(dialog, "requestButton")->isEnabled() && !button(dialog, "offlineActivateButton")->isEnabled(), "missing machine ID disables activation");
        QCoreApplication::processEvents();
        check(server.statusCount == before, "missing machine ID skips network");
        qputenv("BF_MOCK_MACHINE_GUID", "ACTIVATION-TEST");
    }
#endif
    {
        server.status = "not_found";
        bf::ActivationDialog dialog(&manager, version, &i18n, missing());
        dialog.show();
        check(waitFor([&] { return bar(dialog)->isHidden(); }), "network error test ready");
        QMetaObject::invokeMethod(&dialog, "onNetworkError", Qt::DirectConnection, Q_ARG(QString, QStringLiteral("raw network detail")));
        check(statusMatches(bar(dialog), i18n.text("app.activation.network_unavailable")) &&
              !statusText(bar(dialog)).contains("raw network detail"), "network error hides technical details");
        check(bar(dialog)->severity() == InfoBar::Warning && button(dialog, "refreshButton")->text() == i18n.text("app.activation.retry"), "network error retry action");
        QMetaObject::invokeMethod(&dialog, "onActivationFailed", Qt::DirectConnection,
                                  Q_ARG(QString, QStringLiteral("activation.invalid_machine_id")), Q_ARG(QJsonObject, QJsonObject()));
        check(bar(dialog)->severity() == InfoBar::Error, "invalid machine ID is an error");
    }

#ifndef Q_OS_WIN
    {
        check(manager.saveOnlineLicense(server.license).ok(), "seed license for network failure");
        qputenv("BF_ACTIVATION_SERVER_URL", "unsupported://license-server");
        QWidget parent;
        parent.show();
        bf::ActivationDialog::checkAndHandleRevocation(&manager, version, &i18n, &parent);
        check(waitFor([&] { return parent.findChildren<QNetworkAccessManager*>().isEmpty(); }),
              "network failure completes background check");
        check(parent.isVisible() && manager.checkSavedLicense().ok(), "network failure silently retains license");
        qputenv("BF_ACTIVATION_SERVER_URL", QString("http://127.0.0.1:%1").arg(server.server.serverPort()).toUtf8());
    }
    {
        server.status = "rate_limit";
        check(manager.saveOnlineLicense(server.license).ok(), "seed license for fail-open check");
        const int before = server.statusCount;
        QWidget parent;
        parent.show();
        bf::ActivationDialog::checkAndHandleRevocation(&manager, version, &i18n, &parent);
        check(waitFor([&] { return server.statusCount > before; }), "background check reaches server");
        QCoreApplication::processEvents();
        check(parent.isVisible() && manager.checkSavedLicense().ok(), "server failure silently retains license");
    }
    // Real signed license exercises automatic renewal and saving, including its success date.
    {
        server.status = "active";
        bf::LicenseResult expired;
        expired.status = bf::LicenseStatus::Expired;
        expired.error.params = {{"expires_at", "2025-01-01"}};
        bf::ActivationDialog dialog(&manager, version, &i18n, expired);
        dialog.show();
        bool successMessage = false;
        QTimer dismiss;
        QObject::connect(&dismiss, &QTimer::timeout, &app, [&] {
            for (auto* widget : QApplication::topLevelWidgets())
                if (auto* box = qobject_cast<QMessageBox*>(widget); box && box->isVisible()) {
                    successMessage = box->text().contains("2099-12-31");
                    box->accept();
                }
        });
        dismiss.start(10);
        check(waitFor([&] { return dialog.result() == QDialog::Accepted; }), "active server automatically fetches and saves license");
        check(successMessage && manager.checkSavedLicense().ok(), "renewal displays expiry and saves valid license");
    }
    {
        server.status = "not_found";
        bf::ActivationDialog dialog(&manager, version, &i18n, missing());
        dialog.show();
        check(waitFor([&] { return bar(dialog)->isHidden(); }), "offline activation form ready");
        dialog.findChild<bf::CollapsiblePanel*>("offlinePanel")->setExpanded(true);
        dialog.findChild<fluent::textfields::TextEdit*>("offlineKeyEdit")->setPlainText(server.license);
        bool successMessage = false;
        QTimer dismiss;
        QObject::connect(&dismiss, &QTimer::timeout, &app, [&] {
            for (auto* widget : QApplication::topLevelWidgets())
                if (auto* box = qobject_cast<QMessageBox*>(widget); box && box->isVisible()) {
                    successMessage = box->text().contains("2099-12-31");
                    box->accept();
                }
        });
        dismiss.start(10);
        button(dialog, "offlineActivateButton")->click();
        check(dialog.result() == QDialog::Accepted && successMessage && manager.checkSavedLicense().ok(),
              "offline activation saves license and shows expiry");
    }
    for (const QString& revokedStatus : {QString("revoked"), QString("rejected"), QString("not_found")}) {
        server.status = revokedStatus;
        QWidget parent;
        parent.show();
        bool warning = false, gate = false, removed = false;
        QTimer dismiss;
        QObject::connect(&dismiss, &QTimer::timeout, &app, [&] {
            for (auto* widget : QApplication::topLevelWidgets()) {
                if (auto* box = qobject_cast<QMessageBox*>(widget); box && box->isVisible()) {
                    warning = box->isModal() && box->parentWidget() == &parent;
                    removed = !QFile::exists(manager.licenseFilePath());
                    server.status = "not_found";
                    box->accept();
                } else if (auto* dialog = qobject_cast<bf::ActivationDialog*>(widget); dialog && dialog->isVisible()) {
                    gate = dialog->isModal() && dialog->parentWidget() == &parent;
                    dialog->reject();
                }
            }
        });
        check(manager.saveOnlineLicense(server.license).ok(), "seed local online license");
        dismiss.start(10);
        bf::ActivationDialog::checkAndHandleRevocation(&manager, version, &i18n, &parent);
        check(parent.isVisible(), "background check does not block startup");
        check(waitFor([&] { return !parent.isVisible(); }), "canceling reactivation closes main window");
        check(warning && gate && removed, "revocation removes license and opens modal warning and gate");
    }
    {
        server.status = "revoked";
        check(manager.saveOnlineLicense(server.license).ok(), "seed license for reactivation");
        QWidget parent;
        parent.show();
        bool warning = false, success = false;
        QTimer dismiss;
        QObject::connect(&dismiss, &QTimer::timeout, &app, [&] {
            for (auto* widget : QApplication::topLevelWidgets())
                if (auto* box = qobject_cast<QMessageBox*>(widget); box && box->isVisible()) {
                    if (!warning) { warning = true; server.status = "active"; }
                    else success = box->text().contains("2099-12-31");
                    box->accept();
                }
        });
        dismiss.start(10);
        bf::ActivationDialog::checkAndHandleRevocation(&manager, version, &i18n, &parent);
        check(waitFor([&] { return success && manager.checkSavedLicense().ok(); }), "successful reactivation restores license");
        check(parent.isVisible(), "successful reactivation keeps main window open");
    }
#endif

    const QString screenshotDir = qEnvironmentVariable("BF_ACTIVATION_SCREENSHOT_DIR");
    if (!screenshotDir.isEmpty()) check(QDir().mkpath(screenshotDir), "create screenshot directory");
    const QStringList screenshotStates = {"default", "waiting", "offline", "expired", "invalid"};
    for (const QString& locale : {QString("zh_CN"), QString("zh_TW"), QString("ja_JP"), QString("en_US"), QString("pseudo")}) {
        check(i18n.loadLocale(locale, &error), "load layout test locale");
        for (const QString& state : {QString("default"), QString("waiting"), QString("offline"), QString("expired"),
                                     QString("invalid"), QString("empty"), QString("revoked"), QString("rejected"),
                                     QString("copied"), QString("network"), QString("request-failed"), QString("rate-limited"),
                                     QString("signing-failed"), QString("invalid-machine"), QString("already-bound"),
                                     QString("clock-rollback"), QString("clock-missing"), QString("clock-corrupted"),
                                     QString("clock-write-failed"), QString("save-failed"), QString("local-expired"),
                                     QString("wrong-machine"), QString("checking")}) {
            server.status = state == "waiting" ? "pending_approval" :
                (state == "expired" || state == "revoked" || state == "rejected") ? state :
                state == "request-failed" ? "unknown" : state == "rate-limited" ? "rate_limit" : "not_found";
            bf::LicenseResult initial = missing();
            if (state == "clock-rollback") {
                initial.status = bf::LicenseStatus::ClockRollback;
                initial.error.params = {{"last_seen_utc", "2026-11-04T08:00:00.000Z"},
                                        {"current_utc", "2026-11-04T07:30:00.000Z"}};
            } else if (state == "clock-missing") initial.status = bf::LicenseStatus::ClockStateMissing;
            else if (state == "clock-corrupted") initial.status = bf::LicenseStatus::ClockStateCorrupted;
            else if (state == "clock-write-failed") initial.status = bf::LicenseStatus::ClockStateWriteFailure;
            else if (state == "save-failed") initial.status = bf::LicenseStatus::SaveFailed;
            else if (state == "local-expired") {
                initial.status = bf::LicenseStatus::Expired;
                initial.error.params = {{"expires_at", "2025-01-01"}};
            } else if (state == "wrong-machine") initial.status = bf::LicenseStatus::WrongMachine;
            server.holdStatus = initial.status != bf::LicenseStatus::Missing || state == "checking";
            {
                const int before = server.statusCount;
                bf::ActivationDialog dialog(&manager, version, &i18n, initial);
                dialog.show();
                if (!server.holdStatus) {
                    check(waitFor([&] {
                        if (server.statusCount <= before) return false;
                        if (state == "waiting") return statusMatches(bar(dialog), i18n.text("app.activation.status_waiting"));
                        if (state == "expired" || state == "revoked" || state == "rejected") {
                            const char* key = state == "expired" ? "app.activation.status_expired" :
                                state == "revoked" ? "app.activation.status_revoked" : "app.activation.status_rejected";
                            return statusMatches(bar(dialog), i18n.text(key));
                        }
                        if (state == "request-failed") return statusMatches(bar(dialog), i18n.text("app.activation.request_failed"));
                        if (state == "rate-limited") return statusMatches(bar(dialog), i18n.text("app.activation.error_rate_limited", {{"retry_after", "0"}}));
                        return bar(dialog)->isHidden();
                    }), qPrintable(locale + "-" + state + ": state ready"));
                } else {
                    check(!bar(dialog)->isHidden() && !statusText(bar(dialog)).isEmpty(),
                          qPrintable(locale + "-" + state + ": local status shown"));
                }
                if (state == "offline" || state == "invalid" || state == "empty") {
                    dialog.findChild<bf::CollapsiblePanel*>("offlinePanel")->setExpanded(true);
                    QCoreApplication::processEvents();
                    if (state == "invalid") {
                        dialog.findChild<fluent::textfields::TextEdit*>("offlineKeyEdit")->setPlainText("invalid-license");
                    }
                    if (state == "invalid" || state == "empty") {
                        button(dialog, "offlineActivateButton")->click();
                        check(statusMatches(bar(dialog), i18n.text(state == "invalid" ? "app.activation.invalid_key" : "app.activation.empty_license")),
                              qPrintable(locale + "-" + state + ": offline error shown"));
                    }
                } else if (state == "copied") {
                    button(dialog, "copyButton")->click();
                    check(button(dialog, "copyButton")->text() == i18n.text("app.activation.copied"), "localized copied state");
                } else if (state == "network") {
                    check(QMetaObject::invokeMethod(&dialog, "onNetworkError", Q_ARG(QString, QString("test"))) &&
                          statusMatches(bar(dialog), i18n.text("app.activation.network_unavailable")), "localized network error");
                } else if (state == "signing-failed" || state == "invalid-machine" || state == "already-bound") {
                    const QString code = state == "signing-failed" ? "activation.signing_failed" :
                        state == "invalid-machine" ? "activation.invalid_machine_id" : "activation.machine_already_bound";
                    const char* key = state == "signing-failed" ? "app.activation.error_signing_failed" :
                        state == "invalid-machine" ? "app.activation.error_invalid_machine_id" : "app.activation.status_machine_already_bound";
                    check(QMetaObject::invokeMethod(&dialog, "onActivationFailed", Q_ARG(QString, code), Q_ARG(QJsonObject, QJsonObject())) &&
                          statusMatches(bar(dialog), i18n.text(key)), "localized activation error");
                }
                const QString context = locale + "-" + state;
                screenshot(dialog, screenshotStates.contains(state) ? screenshotDir : QString(), context);
            }
            server.holdStatus = false;
        }
#ifndef Q_OS_WIN
        qputenv("BF_MOCK_MACHINE_GUID", "");
        {
            bf::ActivationDialog dialog(&manager, version, &i18n, missing());
            dialog.show();
            screenshot(dialog, screenshotDir, locale + "-machine-id-unavailable");
        }
        qputenv("BF_MOCK_MACHINE_GUID", "ACTIVATION-TEST");
#endif
    }
    std::cout << (failures ? "activation-dialog-test: FAIL\n" : "activation-dialog-test: PASS\n");
    return failures ? 1 : 0;
}
