#include "license/machine_id.h"

#include <QByteArray>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QProcessEnvironment>
#include <QString>
#include <QStringList>
#include <QTextStream>

#ifdef Q_OS_WIN
// winsock2 must come before windows.h so iphlpapi sees the socket types.
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <winreg.h>
#include <iphlpapi.h>
#pragma comment(lib, "iphlpapi.lib")
#pragma comment(lib, "ws2_32.lib")
#endif

namespace bf {
namespace {

// Legacy machine fingerprint: SHA-256(machineGuid.utf8)[:32].toUpper().
// MAC address is NOT part of the machine id (matches legacy Python
// get_machine_id). The macAddress parameter is retained for source
// compatibility but deliberately ignored.
QByteArray canonicalDigest(const QString& machineGuid) {
    const QByteArray seed = machineGuid.toUtf8();
    const QByteArray digest = QCryptographicHash::hash(seed, QCryptographicHash::Sha256).toHex().toUpper();
    return digest.left(32);
}

#ifdef Q_OS_WIN

QString readMachineGuidWin() {
    HKEY key = nullptr;
    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, L"SOFTWARE\\Microsoft\\Cryptography",
                      0, KEY_READ | KEY_WOW64_64KEY, &key) != ERROR_SUCCESS) {
        return QString();
    }
    wchar_t buffer[128] = {};
    DWORD bufferSize = sizeof(buffer);
    DWORD type = 0;
    LONG result = RegQueryValueExW(key, L"MachineGuid", nullptr, &type,
                                   reinterpret_cast<LPBYTE>(buffer), &bufferSize);
    RegCloseKey(key);
    if (result != ERROR_SUCCESS || type != REG_SZ) {
        return QString();
    }
    // bufferSize includes the trailing null on REG_SZ.
    const int len = static_cast<int>(bufferSize / sizeof(wchar_t));
    return QString::fromWCharArray(buffer, len > 0 ? len - 1 : 0);
}

QString readPrimaryMacAddressWin() {
    ULONG bufLen = 16 * 1024;
    QByteArray storage(static_cast<int>(bufLen), '\0');
    PIP_ADAPTER_ADDRESSES addresses = reinterpret_cast<PIP_ADAPTER_ADDRESSES>(storage.data());
    ULONG flags = GAA_FLAG_SKIP_ANYCAST | GAA_FLAG_SKIP_MULTICAST | GAA_FLAG_SKIP_DNS_SERVER;
    ULONG result = GetAdaptersAddresses(AF_UNSPEC, flags, nullptr, addresses, &bufLen);
    if (result == ERROR_BUFFER_OVERFLOW) {
        storage.resize(static_cast<int>(bufLen));
        addresses = reinterpret_cast<PIP_ADAPTER_ADDRESSES>(storage.data());
        result = GetAdaptersAddresses(AF_UNSPEC, flags, nullptr, addresses, &bufLen);
    }
    if (result != NO_ERROR) {
        return QString();
    }
    for (PIP_ADAPTER_ADDRESSES addr = addresses; addr != nullptr; addr = addr->Next) {
        if (addr->OperStatus != IfOperStatusUp) {
            continue;
        }
        if (addr->IfType == IF_TYPE_SOFTWARE_LOOPBACK) {
            continue;
        }
        if (addr->PhysicalAddressLength != 6) {
            continue;
        }
        QString mac;
        for (ULONG i = 0; i < addr->PhysicalAddressLength; ++i) {
            mac += QString("%1:").arg(addr->PhysicalAddress[i], 2, 16, QLatin1Char('0')).toUpper();
        }
        if (!mac.isEmpty()) {
            mac.chop(1);
        }
        return mac;
    }
    return QString();
}

#else  // Non-Windows (Linux/WSL2 dev)

QString readMachineGuidPosix() {
    if (qEnvironmentVariableIsSet("BF_MOCK_MACHINE_GUID")) {
        return qEnvironmentVariable("BF_MOCK_MACHINE_GUID").trimmed();
    }
    return readMachineGuidFromFiles({QStringLiteral("/etc/machine-id"),
                                     QStringLiteral("/var/lib/dbus/machine-id")});
}

QString readPrimaryMacAddressPosix() {
    const QString mock = QProcessEnvironment::systemEnvironment().value("BF_MOCK_MAC");
    if (!mock.isEmpty()) {
        return mock;
    }
    QDir netDir(QStringLiteral("/sys/class/net"));
    const QStringList entries = netDir.entryList(QDir::Dirs | QDir::NoDotAndDotDot);
    for (const QString& iface : entries) {
        if (iface == QLatin1String("lo")) {
            continue;
        }
        QFile addrFile(netDir.filePath(iface + QStringLiteral("/address")));
        if (!addrFile.open(QIODevice::ReadOnly | QIODevice::Text)) {
            continue;
        }
        QString addr = QString::fromLatin1(addrFile.readAll().trimmed());
        if (addr.isEmpty() || addr == QLatin1String("00:00:00:00:00:00")) {
            continue;
        }
        return addr;
    }
    return QString();
}

#endif

}  // namespace

QString machineIdCanonical(const QString& machineGuid) {
    if (machineGuid.trimmed().isEmpty()) {
        return {};
    }
    return QString::fromLatin1(canonicalDigest(machineGuid));
}

QString readMachineGuidFromFiles(const QStringList& paths) {
    for (const QString& path : paths) {
        QFile file(path);
        if (file.open(QIODevice::ReadOnly | QIODevice::Text)) {
            const QString guid = QString::fromUtf8(file.readAll()).trimmed();
            if (!guid.isEmpty()) {
                return guid;
            }
        }
    }
    return {};
}

QString machineIdCanonicalFromFormatted(const QString& formattedMachineId) {
    QString compact = formattedMachineId;
    compact.remove('-').remove(' ').remove('\n');
    return compact.toUpper();
}

QString machineIdFromGuid(const QString& machineGuid) {
    const QString canonical = machineIdCanonical(machineGuid);
    QStringList groups;
    for (int i = 0; i < canonical.size(); i += 8) {
        groups.push_back(canonical.mid(i, 8));
    }
    return groups.join('-');
}

// Source-compat overload: macAddress is deliberately ignored. The legacy
// machine id is SHA-256(MachineGuid) only; MAC never participates.
QString machineIdFromInputs(const QString& machineGuid, const QString& /*macAddress*/) {
    return machineIdFromGuid(machineGuid);
}

QString readMachineGuid() {
#ifdef Q_OS_WIN
    return readMachineGuidWin();
#else
    return readMachineGuidPosix();
#endif
}

QString readPrimaryMacAddress() {
#ifdef Q_OS_WIN
    return readPrimaryMacAddressWin();
#else
    return readPrimaryMacAddressPosix();
#endif
}

QString currentMachineId() {
    return machineIdFromGuid(readMachineGuid());
}

}  // namespace bf
