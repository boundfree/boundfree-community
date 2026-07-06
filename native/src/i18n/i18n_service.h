#pragma once

#include <QHash>
#include <QJsonObject>
#include <QString>
#include <QStringList>

namespace bf {

class I18nService {
public:
    explicit I18nService(QString repoRoot);

    bool loadLocale(const QString& locale, QString* errorMessage = nullptr);
    QString locale() const;
    QString text(const QString& key, const QJsonObject& params = {}) const;
    QStringList supportedLocales() const;

private:
    QString repoRoot_;
    QString locale_ = "zh_CN";
    QHash<QString, QString> messages_;
};

}  // namespace bf
