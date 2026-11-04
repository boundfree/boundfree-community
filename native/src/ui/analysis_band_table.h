#pragma once
#include "dsp/spectral.h"
#include "i18n/i18n_service.h"
#include <QColor>
#include <QStringList>
#include <vector>
#include <cmath>

namespace bf {
// Keep scientific values aligned at the application's normal font size. Units
// belong in the header; each band has one row with its range and result.
inline QString analysisBandTable(I18nService* i18n, const QString& valueHeader, const std::vector<double>& values) {
    QString table = QString("<table width='100%' cellspacing='0' cellpadding='4'>"
        "<tr><th width='25%' align='left'>%1</th><th width='35%' align='left'>%2</th><th width='40%' align='right'>%3</th></tr>")
        .arg(i18n->text("app.analysis.band").toHtmlEscaped(),
             i18n->text("app.analysis.frequency").toHtmlEscaped(), valueHeader.toHtmlEscaped());
    int index = 0;
    for (const auto& band : dsp::eegBands()) {
        const QColor color(band.rgba[0], band.rgba[1], band.rgba[2]);
        const QString range = i18n->formatNumber(band.low, 1) + QString::fromUtf8("–") + i18n->formatNumber(band.high, 1);
        const double value=values.at(index++);
        const auto formatted=std::abs(value)>=1e6?i18n->qlocale().toString(value,'g',5):i18n->formatNumber(value,2);
        table += QString("<tr><td><span style='background-color:%1'>&nbsp;&nbsp;</span>&nbsp;%2</td>"
                         "<td>%3</td><td align='right'>%4</td></tr>")
            .arg(color.name(), QString::fromLatin1(band.key).toHtmlEscaped(), range.toHtmlEscaped(),
                 formatted.toHtmlEscaped());
    }
    return table + "</table>";
}
} // namespace bf
