// I18nService locale-aware formatting self-test.
// Style references m3_dsp_test.cpp / spike_dsp_test.cpp: custom main, returns
// non-zero on failure, prints PASS/FAIL lines. Derives expected strings from
// QLocale in-test so expectations are not fragile across Qt versions.
//
// Coverage:
//   - formatNumber / formatInteger / formatUnit / formatDuration / formatDateTime
//   - en_US grouping + decimal point
//   - zh_CN decimal separator + locale switch actually changes qlocale()
//   - unit symbols (uV, Hz, s, dB) stay ASCII while the numeric part is localized

#include "i18n/i18n_service.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QLocale>
#include <QString>

#include <iostream>

namespace {

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
