#include "i18n/i18n_service.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QRegularExpression>
#include <QSettings>
#include <QTranslator>

namespace bf {

namespace {

// QSettings 中保存语言的键名。
constexpr const char* kSettingsLanguageKey = "language";

// pseudo 仅测试用，不进 stable 但可被 loadLocale 显式加载。
constexpr const char* kPseudoLocale = "pseudo";

// QSettings 兜底 org/app：优先复用 QCoreApplication 元数据，否则用常量。
QString settingsOrganization() {
    const QString org = QCoreApplication::organizationName();
    return org.isEmpty() ? QString::fromUtf8(I18nService::kSettingsOrganization) : org;
}

QString settingsApplication() {
    const QString app = QCoreApplication::applicationName();
    return app.isEmpty() ? QString::fromUtf8(I18nService::kSettingsApplication) : app;
}

}  // namespace

const QStringList I18nService::kStableLocales = {
    QStringLiteral("zh_CN"),
    QStringLiteral("en_US"),
};

I18nService::I18nService(QString repoRoot, QObject* parent)
    : QObject(parent), repoRoot_(std::move(repoRoot)) {}

I18nService::~I18nService() {
    resetTranslators();
}

QStringList I18nService::supportedLocales() const {
    return kStableLocales;
}

bool I18nService::isStableLocale(const QString& locale) const {
    return kStableLocales.contains(locale);
}

QStringList I18nService::resolveQmCandidates(const QString& locale) const {
    const QString fileName = QStringLiteral("bf_app_%1.qm").arg(locale);
    const QString rel = QStringLiteral("i18n/%1").arg(fileName);
    QStringList candidates;

    // 1. BF_RESOURCE_ROOT
    const QString envRoot = qEnvironmentVariable("BF_RESOURCE_ROOT");
    if (!envRoot.isEmpty()) {
        candidates.append(QDir(envRoot).filePath(rel));
    }

    // 2. 应用可执行目录
    const QString appDir = QCoreApplication::applicationDirPath();
    if (!appDir.isEmpty()) {
        candidates.append(QDir(appDir).filePath(rel));
    }

    // 3. repoRoot/native/i18n（开发构建产物，与 CMake lrelease 输出一致）
    // 4. repoRoot/i18n（向后兼容布局）
    if (!repoRoot_.isEmpty()) {
        candidates.append(QDir(repoRoot_).filePath(QStringLiteral("native/%1").arg(rel)));
        candidates.append(QDir(repoRoot_).filePath(rel));
    }

    return candidates;
}

bool I18nService::loadTranslatorFile(const QString& locale, QTranslator* outTranslator,
                                      QString* errorMessage) const {
    const QStringList candidates = resolveQmCandidates(locale);
    for (const QString& path : candidates) {
        if (QFile::exists(path) && outTranslator->load(path)) {
            return true;
        }
    }
    if (errorMessage != nullptr) {
        *errorMessage = QStringLiteral("missing .qm for locale '%1' (searched: %2)")
                            .arg(locale, candidates.join(QStringLiteral("; ")));
    }
    return false;
}

void I18nService::resetTranslators() {
    if (appTranslator_) {
        QCoreApplication::removeTranslator(appTranslator_.get());
        appTranslator_.reset();
    }
    if (qtTranslator_) {
        QCoreApplication::removeTranslator(qtTranslator_.get());
        qtTranslator_.reset();
    }
}

bool I18nService::loadLocale(const QString& locale, QString* errorMessage) {
    // pseudo 显式允许；其余必须属于 stable 才允许通过 loadLocale（测试路径）。
    const bool allowed = isStableLocale(locale) || (locale == QLatin1String(kPseudoLocale));
    if (!allowed) {
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("locale '%1' is not loadable (not stable/pseudo)").arg(locale);
        }
        return false;
    }

    // 原子性：先在临时 translator 上加载，成功后再替换当前状态。
    auto newApp = std::make_unique<QTranslator>();
    if (!loadTranslatorFile(locale, newApp.get(), errorMessage)) {
        return false;
    }

    // Qt 基础翻译：同目录下 qtbase_<locale>.qm 可用即安装。pseudo 不安装 Qt 基础翻译。
    std::unique_ptr<QTranslator> newQt;
    if (locale != QLatin1String(kPseudoLocale)) {
        auto probe = std::make_unique<QTranslator>();
        const QString qtFile = QStringLiteral("qtbase_%1.qm").arg(locale);
        QStringList qtCandidates;
        const QString envRoot = qEnvironmentVariable("BF_RESOURCE_ROOT");
        if (!envRoot.isEmpty()) {
            qtCandidates.append(QDir(envRoot).filePath(QStringLiteral("i18n/%1").arg(qtFile)));
        }
        const QString appDir = QCoreApplication::applicationDirPath();
        if (!appDir.isEmpty()) {
            qtCandidates.append(QDir(appDir).filePath(QStringLiteral("i18n/%1").arg(qtFile)));
            qtCandidates.append(QDir(appDir).filePath(QStringLiteral("translations/%1").arg(qtFile)));
        }
        if (!repoRoot_.isEmpty()) {
            qtCandidates.append(QDir(repoRoot_).filePath(QStringLiteral("native/i18n/%1").arg(qtFile)));
            qtCandidates.append(QDir(repoRoot_).filePath(QStringLiteral("i18n/%1").arg(qtFile)));
        }
        for (const QString& path : qtCandidates) {
            if (QFile::exists(path) && probe->load(path)) {
                newQt = std::move(probe);
                break;
            }
        }
    }

    const QString previousLocale = locale_;
    const std::unique_ptr<QTranslator> oldApp = std::move(appTranslator_);
    const std::unique_ptr<QTranslator> oldQt = std::move(qtTranslator_);
    if (oldApp) {
        QCoreApplication::removeTranslator(oldApp.get());
    }
    if (oldQt) {
        QCoreApplication::removeTranslator(oldQt.get());
    }

    QCoreApplication::installTranslator(newApp.get());
    if (newQt) {
        QCoreApplication::installTranslator(newQt.get());
    }

    appTranslator_ = std::move(newApp);
    qtTranslator_ = std::move(newQt);
    locale_ = locale;
    qlocale_ = (locale == QLatin1String(kPseudoLocale))
                   ? QLocale(QStringLiteral("en_US"))
                   : QLocale(locale);
    // Qt 自带控件和后续创建的 locale-aware 控件使用相同默认 locale。
    QLocale::setDefault(qlocale_);

    // 仅当 locale 实际变化时 emit。
    if (locale_ != previousLocale) {
        emit languageChanged();
    }
    return true;
}

bool I18nService::setLocale(const QString& locale, QString* errorMessage) {
    if (!isStableLocale(locale)) {
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("locale '%1' is not a stable locale; setLocale only accepts %2")
                                .arg(locale, kStableLocales.join(QStringLiteral(", ")));
        }
        return false;
    }

    if (!loadLocale(locale, errorMessage)) {
        return false;  // loadLocale 保持原子性，settings 尚未写入。
    }

    QSettings settings(settingsOrganization(), settingsApplication());
    settings.setValue(kSettingsLanguageKey, locale);
    return true;
}

QString I18nService::resolveStartupLocale(const QString& cliOverride) {
    // 1. CLI 覆盖（不持久化）。显式值原样返回，由 loadLocale 严格校验；
    // 不支持的 locale 必须明确失败，不能静默降级到 saved/system。
    if (!cliOverride.isEmpty()) {
        return cliOverride;
    }

    // 2. saved settings（仅当属于 stable）。
    QSettings settings(settingsOrganization(), settingsApplication());
    const QString saved = settings.value(kSettingsLanguageKey).toString();
    if (!saved.isEmpty() && kStableLocales.contains(saved)) {
        return saved;
    }

    // 3. 系统 locale 最佳匹配 stable（不受 QLocale::setDefault 影响）。
    const QString matched = bestMatchForSystemLocale();
    if (!matched.isEmpty()) {
        return matched;
    }

    // 4. 兜底。
    return QStringLiteral("zh_CN");
}

QString I18nService::bestMatchForSystemLocale() {
    const QLocale sys = QLocale::system();
    const QString name = sys.name();  // e.g. "zh_CN", "en_US", "de_DE"
    if (name.isEmpty()) {
        return {};
    }
    // 精确匹配。
    if (kStableLocales.contains(name)) {
        return name;
    }
    // 语言部分匹配：zh* -> zh_CN, en* -> en_US。
    const QString lang = name.section(QLatin1Char('_'), 0, 0).toLower();
    if (lang == QLatin1String("zh")) {
        return QStringLiteral("zh_CN");
    }
    if (lang == QLatin1String("en")) {
        return QStringLiteral("en_US");
    }
    return {};
}

QString I18nService::locale() const {
    return locale_;
}

QLocale I18nService::qlocale() const {
    return qlocale_;
}

QString I18nService::text(const QString& key, const QJsonObject& params) const {
    // 直接对 appTranslator 调用 translate，避免依赖全局安装顺序。
    // QTranslator::translate 在未加载时返回空串，此时回退到 key 本身。
    QString value;
    if (appTranslator_) {
        value = appTranslator_->translate(kTranslationContext, key.toUtf8().constData());
    }
    if (value.isEmpty()) {
        value = key;
    }

    // {name} 参数替换。JSON 数字按当前 locale 格式化，避免错误参数绕过统一格式化层。
    for (auto it = params.begin(); it != params.end(); ++it) {
        QString replacement;
        if (it.value().isDouble()) {
            const double number = it.value().toDouble();
            const qlonglong integer = static_cast<qlonglong>(number);
            replacement = static_cast<double>(integer) == number
                              ? qlocale_.toString(integer)
                              : qlocale_.toString(number, 'g', 15);
        } else {
            replacement = it.value().toVariant().toString();
        }
        value.replace(QStringLiteral("{%1}").arg(it.key()), replacement);
    }

    // 可诊断路径：替换后仍残留 {...} 占位符时产生告警（非致命）。
    static const QRegularExpression placeholderPattern(QStringLiteral("\\{[^{}]+\\}"));
    if (placeholderPattern.match(value).hasMatch()) {
        qWarning("I18nService: unresolved placeholder(s) in key '%s': \"%s\"",
                 qUtf8Printable(key), qUtf8Printable(value));
    }

    return value;
}

QString I18nService::formatNumber(double value, int precision) const {
    return qlocale_.toString(value, 'f', precision);
}

QString I18nService::formatInteger(qlonglong value) const {
    return qlocale_.toString(value);
}

// 单个 ASCII 空格分隔 locale 格式化数值与单位符号。符号保持原样（uV/Hz/s/dB 为 ASCII），
// 数值部分遵循当前 locale 的小数/分组规则。
QString I18nService::formatUnit(double value, const QString& unit, int precision) const {
    return qlocale_.toString(value, 'f', precision) + QLatin1Char(' ') + unit;
}

QString I18nService::formatDuration(double seconds, int precision) const {
    return formatUnit(seconds, QStringLiteral("s"), precision);
}

QString I18nService::formatDateTime(const QDateTime& dt) const {
    return qlocale_.toString(dt, QLocale::ShortFormat);
}

}  // namespace bf
