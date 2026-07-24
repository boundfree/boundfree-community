// I18nService locale-aware formatting + load/set/persist/signal self-test.
// Style references m3_dsp_test.cpp / spike_dsp_test.cpp: custom main, returns
// non-zero on failure, prints PASS/FAIL lines. Derives expected strings from
// QLocale in-test so expectations are not fragile across Qt versions.
//
// Coverage:
//   - formatNumber / formatInteger / formatUnit / formatDuration / formatDateTime
//   - en_US grouping + decimal point
//   - zh_CN decimal separator + locale switch actually changes qlocale()
//   - unit symbols (uV, Hz, s, dB) stay ASCII while the numeric part is localized
//   - loadLocale success / failure atomicity (locale unchanged on failure)
//   - setLocale persistence (writes QSettings) + failure atomicity
//   - languageChanged signal: same-locale no emit, change emits once, failure no emit
//   - loadLocale rejects non-stable/non-pseudo locale before .qm lookup
//   - resolveStartupLocale: CLI stable override, non-stable CLI returned as-is, saved stable, fallback
//   - text() param substitution + unknown key fallback to key
//   - supportedLocales == {zh_CN, en_US} (stable menu only two items)

#include "i18n/i18n_service.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QJsonObject>
#include <QLocale>
#include <QSettings>
#include <QString>
#include <QStringList>
#include <QTemporaryDir>

#include <iostream>

namespace {

// RAII guard: swap QCoreApplication org/app names to an isolated namespace so
// QSettings writes go to a test-only location, then restore on destruction.
// This prevents polluting real user settings across the full test suite.
struct SettingsIsolation {
    QString prevOrg;
    QString prevApp;
    QString isoOrg;
    QString isoAppName;

    explicit SettingsIsolation(const char* org, const char* appBase)
        : prevOrg(QCoreApplication::organizationName()),
          prevApp(QCoreApplication::applicationName()),
          isoOrg(QString::fromLatin1(org)),
          isoAppName(QString::fromLatin1(appBase) + QStringLiteral("_pid%1").arg(QCoreApplication::applicationPid())) {
        QCoreApplication::setOrganizationName(isoOrg);
        QCoreApplication::setApplicationName(isoAppName);
    }

    ~SettingsIsolation() {
        QCoreApplication::setOrganizationName(prevOrg);
        QCoreApplication::setApplicationName(prevApp);
        // Clean up: remove the test settings so no residue remains.
        QSettings s(isoOrg, isoAppName);
        s.remove(QStringLiteral("language"));
    }
};

// Clear the "language" key from the currently-scoped QSettings so prior test
// state doesn't leak into resolveStartupLocale / setLocale assertions.
void clearSavedLanguage() {
    QSettings s;
    s.remove(QStringLiteral("language"));
}

int runTests(const QString& repoRoot) {
    int failures = 0;

    // ----- en_US ----------------------------------------------------------
    {
        bf::I18nService svc(repoRoot);
        if (!svc.loadLocale(QStringLiteral("en_US"))) {
            std::cerr << "FAIL loadLocale en_US returned false\n";
            return 1;
        }

        const QLocale expectedLocale(QLocale::English, QLocale::UnitedStates);

        // formatNumber(1234.5, 1): en_US -> "1,234.5" (grouping + dot).
        const QString gotNum = svc.formatNumber(1234.5, 1);
        const QString wantNum = expectedLocale.toString(1234.5, 'f', 1);
        if (gotNum != wantNum) {
            std::cerr << "FAIL formatNumber en_US got=\"" << gotNum.toStdString()
                      << "\" want=\"" << wantNum.toStdString() << "\"\n";
            ++failures;
        } else {
            std::cout << "PASS formatNumber en_US -> " << gotNum.toStdString() << "\n";
        }

        // formatInteger: en_US grouping, e.g. "1,234,567".
        const QString gotInt = svc.formatInteger(1234567LL);
        const QString wantInt = expectedLocale.toString(qlonglong(1234567));
        if (gotInt != wantInt) {
            std::cerr << "FAIL formatInteger en_US got=\"" << gotInt.toStdString()
                      << "\" want=\"" << wantInt.toStdString() << "\"\n";
            ++failures;
        } else {
            std::cout << "PASS formatInteger en_US -> " << gotInt.toStdString() << "\n";
        }

        // formatDuration(1.5) ends with "s" and uses locale-formatted number.
        const QString gotDur = svc.formatDuration(1.5);
        const QString wantDur = expectedLocale.toString(1.5, 'f', 3) + QLatin1Char(' ') + QStringLiteral("s");
        if (gotDur != wantDur || !gotDur.endsWith(QLatin1Char('s'))) {
            std::cerr << "FAIL formatDuration en_US got=\"" << gotDur.toStdString()
                      << "\" want=\"" << wantDur.toStdString() << "\"\n";
            ++failures;
        } else {
            std::cout << "PASS formatDuration en_US -> " << gotDur.toStdString() << "\n";
        }

        // qlocale() matches en_US territory.
        if (svc.qlocale().territory() != QLocale::UnitedStates) {
            std::cerr << "FAIL qlocale en_US territory mismatch\n";
            ++failures;
        } else {
            std::cout << "PASS qlocale en_US territory\n";
        }
    }

    // ----- zh_CN ----------------------------------------------------------
    {
        bf::I18nService svc(repoRoot);
        if (!svc.loadLocale(QStringLiteral("zh_CN"))) {
            std::cerr << "FAIL loadLocale zh_CN returned false\n";
            return 1;
        }

        const QLocale expectedLocale(QStringLiteral("zh_CN"));

        const QString gotNum = svc.formatNumber(1234.5, 1);
        const QString wantNum = expectedLocale.toString(1234.5, 'f', 1);
        if (gotNum != wantNum) {
            std::cerr << "FAIL formatNumber zh_CN got=\"" << gotNum.toStdString()
                      << "\" want=\"" << wantNum.toStdString() << "\"\n";
            ++failures;
        } else {
            std::cout << "PASS formatNumber zh_CN -> " << gotNum.toStdString() << "\n";
        }

        // Switching locales must actually change the stored QLocale.
        const QLocale before = svc.qlocale();
        svc.loadLocale(QStringLiteral("en_US"));
        const QLocale after = svc.qlocale();
        if (before == after) {
            std::cerr << "FAIL locale switch did not change qlocale()\n";
            ++failures;
        } else {
            std::cout << "PASS locale switch changes qlocale()\n";
        }
    }

    // ----- unit rendering (ASCII symbol, localized number) ----------------
    {
        bf::I18nService svc(repoRoot);
        svc.loadLocale(QStringLiteral("en_US"));
        const QLocale expectedLocale(QLocale::English, QLocale::UnitedStates);

        // formatUnit(10.0, "Hz", 0) -> "10 Hz" (en_US: no grouping for <1000).
        const QString gotHz = svc.formatUnit(10.0, QStringLiteral("Hz"), 0);
        const QString wantHz = expectedLocale.toString(10.0, 'f', 0) + QLatin1Char(' ') + QStringLiteral("Hz");
        if (gotHz != wantHz || !gotHz.contains(QStringLiteral("Hz"))) {
            std::cerr << "FAIL formatUnit Hz got=\"" << gotHz.toStdString()
                      << "\" want=\"" << wantHz.toStdString() << "\"\n";
            ++failures;
        } else {
            std::cout << "PASS formatUnit Hz -> " << gotHz.toStdString() << "\n";
        }

        // formatUnit with a precision produces localized decimal portion.
        const QString gotUv = svc.formatUnit(1.25, QStringLiteral("uV"), 2);
        const QString wantUv = expectedLocale.toString(1.25, 'f', 2) + QLatin1Char(' ') + QStringLiteral("uV");
        if (gotUv != wantUv) {
            std::cerr << "FAIL formatUnit uV got=\"" << gotUv.toStdString()
                      << "\" want=\"" << wantUv.toStdString() << "\"\n";
            ++failures;
        } else {
            std::cout << "PASS formatUnit uV -> " << gotUv.toStdString() << "\n";
        }

        // formatDateTime: matches QLocale short format for a fixed datetime.
        const QDateTime dt(QDate(2026, 7, 24), QTime(13, 30, 0));
        const QString gotDt = svc.formatDateTime(dt);
        const QString wantDt = expectedLocale.toString(dt, QLocale::ShortFormat);
        if (gotDt != wantDt) {
            std::cerr << "FAIL formatDateTime en_US got=\"" << gotDt.toStdString()
                      << "\" want=\"" << wantDt.toStdString() << "\"\n";
            ++failures;
        } else {
            std::cout << "PASS formatDateTime en_US -> " << gotDt.toStdString() << "\n";
        }
    }

    // ----- supportedLocales: stable menu only {zh_CN, en_US} --------------
    {
        bf::I18nService svc(repoRoot);
        const QStringList locales = svc.supportedLocales();
        if (locales.size() != 2 || !locales.contains(QStringLiteral("zh_CN"))
            || !locales.contains(QStringLiteral("en_US"))) {
            std::cerr << "FAIL supportedLocales expected exactly {zh_CN, en_US}, got: "
                      << locales.join(QStringLiteral(", ")).toStdString() << "\n";
            ++failures;
        } else {
            std::cout << "PASS supportedLocales == {zh_CN, en_US}\n";
        }

        // isStableLocale: zh_CN/en_US true, pseudo/de_DE/fr_FR false.
        if (!svc.isStableLocale(QStringLiteral("zh_CN")) || !svc.isStableLocale(QStringLiteral("en_US"))) {
            std::cerr << "FAIL isStableLocale zh_CN/en_US should be true\n";
            ++failures;
        } else {
            std::cout << "PASS isStableLocale zh_CN/en_US true\n";
        }
        if (svc.isStableLocale(QStringLiteral("pseudo")) || svc.isStableLocale(QStringLiteral("de_DE"))) {
            std::cerr << "FAIL isStableLocale pseudo/de_DE should be false\n";
            ++failures;
        } else {
            std::cout << "PASS isStableLocale pseudo/de_DE false\n";
        }
    }

    // ----- loadLocale failure atomicity -----------------------------------
    // A non-loadable locale (not stable, not pseudo) must fail and leave the
    // service's current locale/translator completely unchanged.
    {
        bf::I18nService svc(repoRoot);
        if (!svc.loadLocale(QStringLiteral("en_US"))) {
            std::cerr << "FAIL setup loadLocale en_US failed\n";
            return 1;
        }
        const QString before = svc.locale();

        QString errMsg;
        if (svc.loadLocale(QStringLiteral("de_DE"), &errMsg)) {
            std::cerr << "FAIL loadLocale de_DE should have failed (not stable/pseudo)\n";
            ++failures;
        } else {
            std::cout << "PASS loadLocale de_DE rejected: " << errMsg.toStdString() << "\n";
        }

        // Atomicity: locale must not have changed.
        if (svc.locale() != before) {
            std::cerr << "FAIL loadLocale de_DE changed locale from \""
                      << before.toStdString() << "\" to \"" << svc.locale().toStdString() << "\"\n";
            ++failures;
        } else {
            std::cout << "PASS loadLocale failure kept locale atomic\n";
        }
    }

    // ----- loadLocale rejects non-allowed locale (no .qm lookup) ----------
    // fr_FR is neither stable nor pseudo: loadLocale must reject it before
    // any .qm lookup, and the error message must reference the locale.
    {
        bf::I18nService svc(repoRoot);
        QString errMsg;
        if (svc.loadLocale(QStringLiteral("fr_FR"), &errMsg)) {
            std::cerr << "FAIL loadLocale fr_FR should have been rejected (not stable/pseudo)\n";
            ++failures;
        } else {
            if (!errMsg.contains(QStringLiteral("fr_FR"))) {
                std::cerr << "FAIL error message should mention fr_FR, got: " << errMsg.toStdString() << "\n";
                ++failures;
            } else {
                std::cout << "PASS loadLocale fr_FR rejected: " << errMsg.toStdString() << "\n";
            }
        }
    }

    // ----- languageChanged signal count ----------------------------------
    // Same locale: no emit. Different locale: exactly one emit. Failed load: no emit.
    {
        bf::I18nService svc(repoRoot);
        svc.loadLocale(QStringLiteral("en_US"));

        int signalCount = 0;
        QObject::connect(&svc, &bf::I18nService::languageChanged, [&]() { ++signalCount; });

        // Reload same locale -> must NOT emit.
        svc.loadLocale(QStringLiteral("en_US"));
        if (signalCount != 0) {
            std::cerr << "FAIL languageChanged emitted " << signalCount
                      << " times on same-locale reload (expected 0)\n";
            ++failures;
        } else {
            std::cout << "PASS languageChanged no emit on same locale\n";
        }

        // Switch to zh_CN -> must emit exactly once.
        svc.loadLocale(QStringLiteral("zh_CN"));
        if (signalCount != 1) {
            std::cerr << "FAIL languageChanged emitted " << signalCount
                      << " times on zh_CN switch (expected 1)\n";
            ++failures;
        } else {
            std::cout << "PASS languageChanged emits once on locale change\n";
        }

        // Failed load (de_DE) -> must NOT emit.
        svc.loadLocale(QStringLiteral("de_DE"));
        if (signalCount != 1) {
            std::cerr << "FAIL languageChanged emitted on failed load (count=" << signalCount << ")\n";
            ++failures;
        } else {
            std::cout << "PASS languageChanged no emit on failed load\n";
        }

        // Switch back to en_US -> exactly one more emit (total 2).
        svc.loadLocale(QStringLiteral("en_US"));
        if (signalCount != 2) {
            std::cerr << "FAIL languageChanged emit count=" << signalCount << " after back-switch (expected 2)\n";
            ++failures;
        } else {
            std::cout << "PASS languageChanged emits once on back-switch\n";
        }
    }

    // ----- setLocale persistence + failure atomicity ----------------------
    // Uses isolated QSettings org/app so no real user settings are touched.
    {
        SettingsIsolation iso("bf_test_i18n_org", "bf_test_i18n_app");
        clearSavedLanguage();

        // setLocale en_US should persist to QSettings.
        bf::I18nService svc(repoRoot);
        if (!svc.setLocale(QStringLiteral("en_US"))) {
            std::cerr << "FAIL setLocale en_US returned false\n";
            return 1;
        }
        {
            QSettings s;
            const QString saved = s.value(QStringLiteral("language")).toString();
            if (saved != QStringLiteral("en_US")) {
                std::cerr << "FAIL setLocale en_US did not persist (saved=\"" << saved.toStdString() << "\")\n";
                ++failures;
            } else {
                std::cout << "PASS setLocale en_US persisted to QSettings\n";
            }
        }

        // loadLocale is non-persistent: switching to zh_CN must NOT overwrite the saved en_US.
        if (!svc.loadLocale(QStringLiteral("zh_CN"))) {
            std::cerr << "FAIL loadLocale zh_CN failed during persistence check\n";
            return 1;
        }
        {
            QSettings s;
            const QString saved = s.value(QStringLiteral("language")).toString();
            if (saved != QStringLiteral("en_US")) {
                std::cerr << "FAIL loadLocale zh_CN overwrote saved language (got=\"" << saved.toStdString() << "\")\n";
                ++failures;
            } else {
                std::cout << "PASS loadLocale zh_CN did not persist (stays en_US)\n";
            }
        }

        // setLocale with non-stable locale (pseudo) must fail and NOT change settings.
        QString errMsg;
        if (svc.setLocale(QStringLiteral("pseudo"), &errMsg)) {
            std::cerr << "FAIL setLocale pseudo should have been rejected\n";
            ++failures;
        } else {
            std::cout << "PASS setLocale pseudo rejected: " << errMsg.toStdString() << "\n";
        }
        {
            QSettings s;
            const QString saved = s.value(QStringLiteral("language")).toString();
            if (saved != QStringLiteral("en_US")) {
                std::cerr << "FAIL setLocale pseudo changed settings (got=\"" << saved.toStdString() << "\")\n";
                ++failures;
            } else {
                std::cout << "PASS setLocale pseudo did not change saved language\n";
            }
        }

        // Clean up the test key.
        clearSavedLanguage();
    }

    // ----- setLocale QSettings AccessError atomicity ----------------------
    // Isolate settings to a non-writable path so sync()/status() -> AccessError.
    // Runtime locale/signal must stay atomic; removing sync/status checks fails this.
    {
        const QString prevOrg = QCoreApplication::organizationName();
        const QString prevApp = QCoreApplication::applicationName();
        const QString isoOrg = QStringLiteral("bf_test_i18n_access_org");
        const QString isoApp =
            QStringLiteral("bf_test_i18n_access_app_pid%1").arg(QCoreApplication::applicationPid());
        QCoreApplication::setOrganizationName(isoOrg);
        QCoreApplication::setApplicationName(isoApp);

        QTemporaryDir tmp;
        if (!tmp.isValid()) {
            std::cerr << "FAIL AccessError test: QTemporaryDir invalid\n";
            QCoreApplication::setOrganizationName(prevOrg);
            QCoreApplication::setApplicationName(prevApp);
            return 1;
        }
        tmp.setAutoRemove(false);
        const QString settingsRoot = tmp.path();
        const QByteArray xdg = qgetenv("XDG_CONFIG_HOME");
        const QString prevSettingsPath = xdg.isEmpty()
                                             ? (QDir::homePath() + QStringLiteral("/.config"))
                                             : QString::fromLocal8Bit(xdg);
        QSettings::setPath(QSettings::NativeFormat, QSettings::UserScope, settingsRoot);

        const QFileDevice::Permissions ro = QFileDevice::ReadOwner | QFileDevice::ExeOwner |
                                            QFileDevice::ReadGroup | QFileDevice::ExeGroup |
                                            QFileDevice::ReadOther | QFileDevice::ExeOther;
        if (!QFile::setPermissions(settingsRoot, ro)) {
            std::cerr << "FAIL AccessError test: chmod failed\n";
            QSettings::setPath(QSettings::NativeFormat, QSettings::UserScope, prevSettingsPath);
            QCoreApplication::setOrganizationName(prevOrg);
            QCoreApplication::setApplicationName(prevApp);
            QDir(settingsRoot).removeRecursively();
            return 1;
        }

        auto restore = [&]() {
            const QFileDevice::Permissions rw = QFileDevice::ReadOwner | QFileDevice::WriteOwner |
                                                QFileDevice::ExeOwner | QFileDevice::ReadGroup |
                                                QFileDevice::ExeGroup | QFileDevice::ReadOther |
                                                QFileDevice::ExeOther;
            QFile::setPermissions(settingsRoot, rw);
            QSettings::setPath(QSettings::NativeFormat, QSettings::UserScope, prevSettingsPath);
            QCoreApplication::setOrganizationName(prevOrg);
            QCoreApplication::setApplicationName(prevApp);
            QDir(settingsRoot).removeRecursively();
        };

        bf::I18nService svc(repoRoot);
        if (!svc.loadLocale(QStringLiteral("en_US"))) {
            std::cerr << "FAIL AccessError setup loadLocale en_US failed\n";
            restore();
            return 1;
        }

        int signalCount = 0;
        QObject::connect(&svc, &bf::I18nService::languageChanged, [&]() { ++signalCount; });

        QString errMsg;
        const bool ok = svc.setLocale(QStringLiteral("zh_CN"), &errMsg);
        if (ok || svc.locale() != QStringLiteral("en_US") || signalCount != 0 || errMsg.isEmpty()) {
            std::cerr << "FAIL setLocale AccessError atomicity ok=" << ok
                      << " locale=\"" << svc.locale().toStdString()
                      << "\" signalCount=" << signalCount
                      << " err=\"" << errMsg.toStdString() << "\"\n";
            ++failures;
        } else {
            std::cout << "PASS setLocale AccessError keeps en_US, no signal, error set\n";
        }

        restore();
    }

    // ----- resolveStartupLocale: CLI stable override ----------------------
    // CLI stable locale must win regardless of saved settings.
    {
        SettingsIsolation iso("bf_test_i18n_org", "bf_test_i18n_app");
        clearSavedLanguage();

        // Pre-seed saved setting to zh_CN; CLI en_US must override.
        QSettings s;
        s.setValue(QStringLiteral("language"), QStringLiteral("zh_CN"));

        const QString resolved = bf::I18nService::resolveStartupLocale(QStringLiteral("en_US"));
        if (resolved != QStringLiteral("en_US")) {
            std::cerr << "FAIL resolveStartupLocale CLI en_US override got=\"" << resolved.toStdString() << "\"\n";
            ++failures;
        } else {
            std::cout << "PASS resolveStartupLocale CLI stable overrides saved\n";
        }

        clearSavedLanguage();
    }

    // ----- resolveStartupLocale: non-stable CLI returned as-is -----------
    // Per contract: a non-stable CLI value is returned as-is (not ignored);
    // loadLocale will then reject it explicitly. It must NOT silently fall
    // back to saved settings.
    {
        SettingsIsolation iso("bf_test_i18n_org", "bf_test_i18n_app");
        clearSavedLanguage();

        QSettings s;
        s.setValue(QStringLiteral("language"), QStringLiteral("en_US"));

        const QString resolved = bf::I18nService::resolveStartupLocale(QStringLiteral("fr_FR"));
        if (resolved != QStringLiteral("fr_FR")) {
            std::cerr << "FAIL resolveStartupLocale non-stable CLI should return as-is, got=\""
                      << resolved.toStdString() << "\"\n";
            ++failures;
        } else {
            std::cout << "PASS resolveStartupLocale non-stable CLI returned as-is\n";
        }

        clearSavedLanguage();
    }

    // ----- resolveStartupLocale: no CLI, no saved -> system/fallback -----
    // With no CLI and no saved setting, result must be a stable locale
    // (either system match or zh_CN fallback). We cannot control system locale
    // deterministically, so we assert it's always in the stable set.
    {
        SettingsIsolation iso("bf_test_i18n_org", "bf_test_i18n_app");
        clearSavedLanguage();

        const QString resolved = bf::I18nService::resolveStartupLocale();
        if (!bf::I18nService::kStableLocales.contains(resolved)) {
            std::cerr << "FAIL resolveStartupLocale fallback returned non-stable \""
                      << resolved.toStdString() << "\"\n";
            ++failures;
        } else {
            std::cout << "PASS resolveStartupLocale no-CLI/no-saved returns stable: "
                      << resolved.toStdString() << "\n";
        }

        clearSavedLanguage();
    }

    // ----- text() param substitution + unknown key fallback --------------
    {
        bf::I18nService svc(repoRoot);
        svc.loadLocale(QStringLiteral("en_US"));

        // Known key with params: "app.status.file_opening" = "Opening {path}".
        const QString got = svc.text(QStringLiteral("app.status.file_opening"),
                                     QJsonObject{{"path", "test.csv"}});
        if (got != QStringLiteral("Opening test.csv")) {
            std::cerr << "FAIL text param substitution got=\"" << got.toStdString()
                      << "\" want=\"Opening test.csv\"\n";
            ++failures;
        } else {
            std::cout << "PASS text param substitution -> " << got.toStdString() << "\n";
        }

        // Unknown key: must return the key itself.
        const QString unknown = svc.text(QStringLiteral("nonexistent.key.12345"));
        if (unknown != QStringLiteral("nonexistent.key.12345")) {
            std::cerr << "FAIL text unknown key got=\"" << unknown.toStdString()
                      << "\" want=\"nonexistent.key.12345\"\n";
            ++failures;
        } else {
            std::cout << "PASS text unknown key returns key\n";
        }

        // Known key with multiple params: "app.status.file_window" template.
        const QString windowed = svc.text(
            QStringLiteral("app.status.file_window"),
            QJsonObject{{"format", "CSV"}, {"t0", "0"}, {"t1", "0.01"},
                        {"amplitude", "200"}, {"n_visible", "2"}});
        if (windowed != QStringLiteral("CSV | 0-0.01 s | +/-200 uV | 2 visible channels")) {
            std::cerr << "FAIL text multi-param got=\"" << windowed.toStdString() << "\"\n";
            ++failures;
        } else {
            std::cout << "PASS text multi-param -> " << windowed.toStdString() << "\n";
        }
    }

    return failures;
}

}  // namespace

int main(int argc, char** argv) {
    // QLocale is usable without a GUI; QCoreApplication satisfies Qt core
    // object ownership expectations and keeps the test headless.
    QCoreApplication app(argc, argv);

    // Repo root: first arg, else BF_REPO_ROOT macro, else ".." relative.
    QString repoRoot = (argc > 1) ? QString::fromLocal8Bit(argv[1]) : QString::fromUtf8(BF_REPO_ROOT);
    if (repoRoot.isEmpty()) {
        repoRoot = QStringLiteral("..");
    }

    const int failures = runTests(repoRoot);
    if (failures == 0) {
        std::cout << "i18n-locale-format-test-ok failures=0\n";
        return 0;
    }
    std::cerr << "i18n-locale-format-test-failed failures=" << failures << "\n";
    return 1;
}
