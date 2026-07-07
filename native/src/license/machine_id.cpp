#include "license/machine_id.h"

#include <QCryptographicHash>
#include <QStringList>

namespace bf {

QString machineIdFromInputs(const QString& machineGuid, const QString& macAddress) {
    const QByteArray seed = (machineGuid + macAddress).toUtf8();
    const QByteArray digest = QCryptographicHash::hash(seed, QCryptographicHash::Sha256).toHex().toUpper();
    const QString first32 = QString::fromLatin1(digest.left(32));
    QStringList groups;
    for (int i = 0; i < first32.size(); i += 8) {
        groups.push_back(first32.mid(i, 8));
    }
    return groups.join("-");
}

}  // namespace bf
