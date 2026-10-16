#pragma once
#include "dsp/spectral.h"
#include "i18n/i18n_service.h"
#include <QColor>
#include <QStringList>
#include <vector>

namespace bf {
// Keep scientific values aligned at the application's normal font size. Units
// belong in the header; each band has one row with its range and result.
inline QString analysisBandTable(I18nService* i18n, const QString& valueHeader, const std::vector<double>& values) {
    QString table = QString("<table width='100%' cellspacing='0' cellpadding='4'>"
        "<tr><th align='left'>%1</th><th align='left'>%2</th><th align='right'>%3</th></tr>")
        .arg(i18n->text("app.analysis.band").toHtmlEscaped(),
             i18n->text("app.analysis.frequency").toHtmlEscaped(), valueHeader.toHtmlEscaped());
    int index = 0;
    for (const auto& band : dsp::eegBands()) {
        const QColor color(band.rgba[0], band.rgba[1], band.rgba[2]);
        const QString range = i18n->formatNumber(band.low, 1) + QString::fromUtf8("–") + i18n->formatNumber(band.high, 1);
        table += QString("<tr><td><span style='background-color:%1'>&nbsp;&nbsp;</span>&nbsp;%2</td>"
                         "<td>%3</td><td align='right'>%4</td></tr>")
            .arg(color.name(), QString::fromLatin1(band.key).toHtmlEscaped(), range.toHtmlEscaped(),
                 i18n->formatNumber(values.at(index++), 2).toHtmlEscaped());
    }
    return table + "</table>";
}
} // namespace bf
