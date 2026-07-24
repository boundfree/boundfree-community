#pragma once

#include <QDateTime>
#include <QJsonObject>
#include <QLocale>
#include <QObject>
#include <QString>
#include <QStringList>

#include <memory>

class QTranslator;

namespace bf {

// 应用层国际化门面：封装 Qt6 .qm/QTranslator 机制，对外保留稳定的文本/格式化 API。
//
// 资源查找顺序（.qm 必须命中，缺失即失败，不回退 .ts）：
//   1. BF_RESOURCE_ROOT/i18n/bf_app_<locale>.qm
//   2. <应用可执行目录>/i18n/bf_app_<locale>.qm
//   3. <repoRoot>/native/i18n/bf_app_<locale>.qm（开发构建产物）
//   4. <repoRoot>/i18n/bf_app_<locale>.qm（向后兼容布局）
// Qt 基础翻译（qtbase_*.qm）按同一顺序可用即安装，缺失不视为错误。
//
// 持久化策略：QSettings 组织/应用名优先复用 QCoreApplication 元数据，
// 否则回退到常量 kSettingsOrganization / kSettingsApplication。
class I18nService : public QObject {
    Q_OBJECT

public:
    // 稳定用户菜单仅暴露 zh_CN/en_US；pseudo 仅测试用，通过 loadLocale 显式加载。
    static const QStringList kStableLocales;
    // QSettings 兜底常量（当 QCoreApplication 未设置组织/应用名时使用）。
    static constexpr const char* kSettingsOrganization = "BoundFree";
    static constexpr const char* kSettingsApplication = "bf_app";
    // 应用翻译 context 固定为 "app"。
    static constexpr const char* kTranslationContext = "app";

    explicit I18nService(QString repoRoot, QObject* parent = nullptr);
    ~I18nService() override;

    // 非持久化加载：切换 QTranslator + qlocale + 触发 languageChanged，不写 QSettings。
    // 用于 CLI 覆盖、启动初始化、测试与 pseudo。失败时 translator/locale/信号原子不变。
    bool loadLocale(const QString& locale, QString* errorMessage = nullptr);

    // 持久化切换：先调用 loadLocale 成功后写 QSettings。仅接受 stable locales。
    // 失败时 translator/locale/settings 原子不变。用于 UI 语言菜单。
    bool setLocale(const QString& locale, QString* errorMessage = nullptr);

    // 稳定用户菜单集合（zh_CN/en_US）。pseudo/其余 locale 不在其中。
    QStringList supportedLocales() const;

    // 校验 locale 是否属于 stable 集合（大小写敏感，与 .ts/.qm 文件名一致）。
    bool isStableLocale(const QString& locale) const;

    // 启动 locale 解析（不持久化）：CLI 覆盖 > saved settings（仅 stable）>
    // QLocale::system() 最佳匹配 stable > "zh_CN"。不读取 QLocale::setDefault。
    static QString resolveStartupLocale(const QString& cliOverride = {});

    QString locale() const;
    QLocale qlocale() const;

    // 文本查询：固定 context "app"，缺失 key 返回 key；对 params 做 {name} 替换。
    // 替换后仍残留 {...} 占位符时在可诊断路径产生 qWarning（非致命）。
    QString text(const QString& key, const QJsonObject& params = {}) const;

    // Locale-aware 格式化 API（保持原签名）。数值/日期输出走存储的 QLocale。
    QString formatNumber(double value, int precision = 2) const;
    QString formatInteger(qlonglong value) const;
    QString formatUnit(double value, const QString& unit, int precision = 2) const;
    QString formatDuration(double seconds, int precision = 3) const;
    QString formatDateTime(const QDateTime& dt) const;

signals:
    // 仅当 locale 实际成功变化时 emit 一次。
    void languageChanged();

private:
    // 解析 .qm 候选路径列表（顺序见类注释）。
    QStringList resolveQmCandidates(const QString& locale) const;
    // 从候选列表加载首个命中 .qm 到 outTranslator；返回是否成功并写出错误信息。
    bool loadTranslatorFile(const QString& locale, QTranslator* outTranslator, QString* errorMessage) const;
    // 系统 locale 最佳匹配 stable locales（仅返回 stable 或空）。
    static QString bestMatchForSystemLocale();
    // 卸载并释放旧 translator（若已安装）。
    void resetTranslators();

    QString repoRoot_;
    QString locale_ = QStringLiteral("zh_CN");
    QLocale qlocale_;
    // 应用翻译：当前激活的 app 上下文 .qm。text() 直接对其调用 translate("app", key)。
    // 使用 unique_ptr 以支持切换时整体替换（QTranslator 不可拷贝/赋值）。
    std::unique_ptr<QTranslator> appTranslator_;
    // Qt 基础翻译（qtbase 等）：按可用性安装，缺失不报错。
    std::unique_ptr<QTranslator> qtTranslator_;
};

}  // namespace bf
