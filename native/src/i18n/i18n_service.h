#pragma once

#include <QHash>
#include <QJsonObject>
#include <QLocale>
#include <QString>
#include <QStringList>
#include <QDateTime>

namespace bf {

class I18nService {
public:
    explicit I18nService(QString repoRoot);

    bool loadLocale(const QString& locale, QString* errorMessage = nullptr);
    QString locale() const;
    QString text(const QString& key, const QJsonObject& params = {}) const;
    QStringList supportedLocales() const;

    // Locale-aware formatting API. All numeric/date output flows through the
    // stored QLocale so decimal separators, grouping and date layout follow
    // the active locale (e.g. zh_CN vs en_US). Unit symbols stay ASCII.
    QString formatNumber(double value, int precision = 2) const;
    QString formatInteger(qlonglong value) const;
    QString formatUnit(double value, const QString& unit, int precision = 2) const;
    QString formatDuration(double seconds, int precision = 3) const;
    QString formatDateTime(const QDateTime& dt) const;
    QLocale qlocale() const;

private:
    QString repoRoot_;
    QString locale_ = "zh_CN";
    QLocale qlocale_;
    QHash<QString, QString> messages_;
};

}  // namespace bf
