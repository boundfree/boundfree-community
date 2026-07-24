#include "i18n/i18n_service.h"

#include <QDir>
#include <QFile>
#include <QRegularExpression>
#include <QXmlStreamReader>

namespace bf {

I18nService::I18nService(QString repoRoot) : repoRoot_(std::move(repoRoot)) {}

QStringList I18nService::supportedLocales() const {
    return {"zh_CN", "zh_TW", "en_US", "ja_JP", "ko_KR", "de_DE", "fr_FR", "es_ES", "pseudo"};
}

bool I18nService::loadLocale(const QString& locale, QString* errorMessage) {
    const QString normalized = supportedLocales().contains(locale) ? locale : QStringLiteral("zh_CN");
    QString filePath = QDir(repoRoot_).filePath(QString("native/i18n/bf_app_%1.ts").arg(normalized));
    if (!QFile::exists(filePath)) {
        filePath = QDir(repoRoot_).filePath(QString("i18n/bf_app_%1.ts").arg(normalized));
    }
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        if (errorMessage != nullptr) {
            *errorMessage = file.errorString();
        }
        return false;
    }

    QHash<QString, QString> loaded;
    QXmlStreamReader xml(&file);
    QString currentSource;
    while (!xml.atEnd()) {
        xml.readNext();
        if (xml.isStartElement() && xml.name() == "source") {
            currentSource = xml.readElementText();
        } else if (xml.isStartElement() && xml.name() == "translation") {
            const QString translation = xml.readElementText();
            if (!currentSource.isEmpty()) {
                loaded.insert(currentSource, translation);
                currentSource.clear();
            }
        }
    }

    if (xml.hasError()) {
        if (errorMessage != nullptr) {
            *errorMessage = xml.errorString();
        }
        return false;
    }

    locale_ = normalized;
    qlocale_ = QLocale(normalized);
    messages_ = loaded;
    return true;
}

QString I18nService::locale() const {
    return locale_;
}

QString I18nService::text(const QString& key, const QJsonObject& params) const {
    QString value = messages_.value(key, key);
    for (auto it = params.begin(); it != params.end(); ++it) {
        value.replace(QString("{%1}").arg(it.key()), it.value().toVariant().toString());
    }
    return value;
}

QString I18nService::formatNumber(double value, int precision) const {
    return qlocale_.toString(value, 'f', precision);
}

QString I18nService::formatInteger(qlonglong value) const {
    return qlocale_.toString(value);
}

// A single ASCII space separates the locale-formatted number from the unit
// symbol. This keeps the symbol untouched (uV/Hz/s/dB stay ASCII) while the
// numeric part respects the active locale's decimal/grouping rules. A single
// space is the simplest stable choice and matches existing UI suffix style.
QString I18nService::formatUnit(double value, const QString& unit, int precision) const {
    return qlocale_.toString(value, 'f', precision) + QLatin1Char(' ') + unit;
}

QString I18nService::formatDuration(double seconds, int precision) const {
    return formatUnit(seconds, QStringLiteral("s"), precision);
}

QString I18nService::formatDateTime(const QDateTime& dt) const {
    return qlocale_.toString(dt, QLocale::ShortFormat);
}

QLocale I18nService::qlocale() const {
    return qlocale_;
}

}  // namespace bf
