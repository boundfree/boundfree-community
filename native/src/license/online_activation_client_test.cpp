#include "license/online_activation_client.h"

#include <QCoreApplication>
#include <QList>
#include <QNetworkAccessManager>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTimer>
#include <QUrl>

#include <iostream>

namespace {

struct Response {
    QByteArray expectedPath;
    int statusCode;
    QByteArray body;
};

class TestServer {
public:
    explicit TestServer(QObject* parent) {
        QObject::connect(&server_, &QTcpServer::newConnection, parent, [this] {
            QTcpSocket* socket = server_.nextPendingConnection();
            QObject::connect(socket, &QTcpSocket::readyRead, socket, [this, socket] {
                const QByteArray request = socket->readAll();
                const int lineEnd = request.indexOf("\r\n");
                if (lineEnd < 0 || responses_.isEmpty()) {
                    return;
                }
                const QList<QByteArray> requestLine = request.left(lineEnd).split(' ');
                const Response response = responses_.takeFirst();
                if (requestLine.size() < 2 || requestLine[1] != response.expectedPath) {
                    valid_ = false;
                }
                const QByteArray statusText = response.statusCode == 200 ? "OK"
                    : response.statusCode == 202 ? "Accepted" : "Conflict";
                const QByteArray wire = "HTTP/1.1 " + QByteArray::number(response.statusCode) + " " + statusText + "\r\n"
                    "Content-Type: application/json\r\n"
                    "Content-Length: " + QByteArray::number(response.body.size()) + "\r\n"
                    "Connection: close\r\n\r\n" + response.body;
                socket->write(wire);
                socket->disconnectFromHost();
            });
        });
    }

    bool listen() { return server_.listen(QHostAddress::LocalHost); }
    quint16 port() const { return server_.serverPort(); }
    void enqueue(Response response) { responses_.append(std::move(response)); }
    bool valid() const { return valid_ && responses_.isEmpty(); }

private:
    QTcpServer server_;
    QList<Response> responses_;
    bool valid_ = true;
};

}  // namespace

int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    TestServer server(&app);
    if (!server.listen()) {
        std::cerr << "failed to start test server\n";
        return 1;
    }

    server.enqueue({"/api/v1/register", 409,
                    R"({"error":{"code":"activation.already_exists","params":{"current_status":"pending_approval"}}})"});
    server.enqueue({"/api/v1/activate", 202,
                    R"({"error":{"code":"activation.pending_approval","params":{}}})"});
    server.enqueue({"/api/v1/status?machine_id=00112233445566778899AABBCCDDEEFF", 200,
                    R"({"machine_id":"00112233445566778899AABBCCDDEEFF","status":"rejected","reason":"institution_not_authorized","expires_at":null})"});

    QNetworkAccessManager manager;
    bf::OnlineActivationClient client(
        &manager, QUrl(QStringLiteral("http://127.0.0.1:%1").arg(server.port())));

    bool registrationRecovered = false;
    bool pendingReceived = false;
    bool statusReceived = false;
    bool unexpectedFailure = false;

    QObject::connect(&client, &bf::OnlineActivationClient::registrationAlreadyExists, &app, [&] {
        registrationRecovered = true;
        client.fetchLicense(QStringLiteral("00112233445566778899AABBCCDDEEFF"), QStringLiteral("0.1.50-dev"));
    });
    QObject::connect(&client, &bf::OnlineActivationClient::statusReceived, &app,
                     [&](const QString& status, const QString& reason, const QDate&) {
        if (status == QStringLiteral("pending_approval")) {
            pendingReceived = true;
            client.checkStatus(QStringLiteral("00112233445566778899AABBCCDDEEFF"));
            return;
        }
        statusReceived = status == QStringLiteral("rejected") &&
                         reason == QStringLiteral("institution_not_authorized");
        app.quit();
    });
    QObject::connect(&client, &bf::OnlineActivationClient::activationFailed, &app,
                     [&](const QString&, const QString&) { unexpectedFailure = true; app.quit(); });
    QObject::connect(&client, &bf::OnlineActivationClient::networkError, &app,
                     [&](const QString&) { unexpectedFailure = true; app.quit(); });

    QTimer::singleShot(5000, &app, [&] { unexpectedFailure = true; app.quit(); });
    client.registerDevice(QStringLiteral("00112233445566778899AABBCCDDEEFF"), QStringLiteral("0.1.50-dev"));
    app.exec();

    const bool passed = registrationRecovered && pendingReceived && statusReceived &&
                        !unexpectedFailure && server.valid();
    std::cout << (passed ? "online-activation-client-test: PASS\n"
                         : "online-activation-client-test: FAIL\n");
    return passed ? 0 : 1;
}
