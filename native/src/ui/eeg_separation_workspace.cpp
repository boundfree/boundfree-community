#include "ui/fluent_controls.h"
#include "ui/control_bar.h"
#include "ui/workspace_splitter.h"
#include "ui/eeg_separation_workspace.h"
#include "dsp/spectral.h"
#include "ui/collapsible_panel.h"
#include <QCheckBox>
#include <QRadioButton>
#include <QButtonGroup>
#include <QComboBox>
#include <QDir>
#include <QDialog>
#include <QTabWidget>
#include <QMouseEvent>
#include <QWheelEvent>
#include <QDoubleSpinBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QFrame>
#include <QFormLayout>
#include <QHeaderView>
#include <QGridLayout>
#include <QResizeEvent>
#include <QHideEvent>
#include <QKeyEvent>
#include <QInputDialog>
#include <QLabel>
#include <QPainter>
#include <QPainterPath>
#include <QPushButton>
#include <QScopedValueRollback>
#include <QScrollArea>
#include <QShortcut>
#include <QSignalBlocker>
#include <QSlider>
#include <QStandardPaths>
#include <QStyle>
#include <QSplitter>
#include <QSplitterHandle>
#include <QTableWidget>
#include <QTimer>
#include <QVBoxLayout>
#include <QVariantList>
#include <algorithm>
#include <cmath>
namespace bf {
class SeparationTraceView final : public QWidget {
  public:
    explicit SeparationTraceView(QWidget *parent = nullptr) : QWidget(parent) {
        setMinimumSize(160, 120);
        setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    }
    std::vector<std::vector<double>> traces;
    QStringList names;
    std::vector<bool> emptyGroups;
    QString emptyMessage, noData, peakToPeakMessage, smallWaveMessage, clippedMessage, cursorMessage, controlsHelp;
    QString xAxisLabel, yAxisLabel, yAxisUnit;
    int plotLeft() const { return fontMetrics().horizontalAdvance(QStringLiteral("-0.00e+00")) + 26; }
    int plotRight() const { return std::max(plotLeft() + 1, width() - 14); }
    QLocale locale;
    double fs = 1.0, offset = 0.0, seconds = 10.0, start = 0.0;
    bool dark = false, frequency = false, sharedAmplitude = false, groupPreview = false;
    double amplitudeZoom = 1.0;
    double cursor = -1.0;
    // Full-interval amplitudes keep the vertical scale stable while time advances.
    std::vector<double> fullPeaks;
    std::vector<double> peaks, peakToPeaks;
    std::vector<bool> smallWaves, clipped;
    // Statistics always describe the actual visible samples, before display gain/clipping.
    void refreshMetrics() {
        peaks.clear(); peakToPeaks.clear(); smallWaves.clear(); clipped.clear();
        fullPeaks.clear();
        for (const auto &trace : traces) {
            double fullPeak = 0;
            for (double value : trace) fullPeak = std::max(fullPeak, std::abs(value));
            fullPeaks.push_back(fullPeak);
            const auto a = frequency ? 0 : std::min(trace.size(), static_cast<std::size_t>(std::max(0.0, offset * fs)));
            const auto b = frequency ? trace.size() : std::min(trace.size(), a + static_cast<std::size_t>(seconds * fs));
            double low = 0, high = 0;
            if (a < b) {
                bool found = false;
                for (auto i = a; i < b; ++i) if (std::isfinite(trace[i])) {
                    if (!found) { low = high = trace[i]; found = true; }
                    else { low = std::min(low, trace[i]); high = std::max(high, trace[i]); }
                }
            }
            peaks.push_back(std::max(std::abs(low), std::abs(high)));
            peakToPeaks.push_back(high - low);
        }
        const double common = fullPeaks.empty() ? 0 : *std::max_element(fullPeaks.begin(), fullPeaks.end());
        QVariantList pps, small, clips, scales;
        QStringList descriptions;
        for (std::size_t r = 0; r < peaks.size(); ++r) {
            const double maximum = sharedAmplitude ? common : fullPeaks[r];
            const double scale = (maximum < 1e-12 ? 1.0 : maximum) / amplitudeZoom;
            const bool empty = r < emptyGroups.size() && emptyGroups[r];
            smallWaves.push_back(!empty && peaks[r] > 1e-12 && peakToPeaks[r] / (2 * scale) < .02);
            clipped.push_back(!empty && peaks[r] > scale * (1 + 1e-9));
            pps << peakToPeaks[r]; small << bool(smallWaves.back()); clips << bool(clipped.back()); scales << scale;
            if (groupPreview) {
                QString description = names.value(static_cast<int>(r)) + QStringLiteral(": ") +
                                      peakToPeakMessage;
                description = description.arg(locale.toString(peakToPeaks[r] < 1e-9 ? 0 : peakToPeaks[r], 'g', 4));
                if (empty) description += QStringLiteral("; ") + emptyMessage;
                if (smallWaves.back()) description += QStringLiteral("; ") + smallWaveMessage;
                if (clipped.back()) description += QStringLiteral("; ") + clippedMessage;
                descriptions << description;
            }
        }
        setProperty("visiblePeakToPeak", pps);
        setProperty("smallWaveGroups", small);
        setProperty("clippedGroups", clips);
        setProperty("visibleScaleLimits", scales);
        setProperty("previewOffsetSeconds", offset);
        setProperty("previewCursorSeconds", cursor);
        if (groupPreview) {
            setAccessibleDescription(descriptions.join(QStringLiteral("\n")));
            setToolTip(descriptions.join(QStringLiteral("\n")) + QStringLiteral("\n\n") + controlsHelp);
        }
    }

  protected:
    void paintEvent(QPaintEvent *) override {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        p.fillRect(rect(), dark ? QColor("#22223a") : QColor(Qt::white));
        if (hasFocus()) {
            p.setPen(QPen(palette().color(QPalette::Highlight), 1));
            p.drawRect(rect().adjusted(1, 1, -2, -2));
        }
        const QColor ink = dark ? QColor("#dde0ff") : QColor("#222222");
        const QColor grid = dark ? QColor("#41415b") : QColor("#e6e6e6");
        const QColor axis = dark ? QColor("#9398b2") : QColor("#747b87");
        p.setPen(ink);
        if (traces.empty()) {
            p.drawText(rect().adjusted(16, 16, -16, -16), Qt::AlignCenter | Qt::TextWordWrap, noData);
            return;
        }
        const auto fm = p.fontMetrics();
        const int line = fm.height() + 3, left = plotLeft(), right = plotRight();
        const int n = int(traces.size());
        const double row = (height() - 2.0 * line - 8) / n;
        const double firstTop = groupPreview ? 39 : 23;
        const double lastBottom = n * row - 6;
        const int intervals = std::clamp((right - left) / 90, 2, 5);
        const double rawStep = seconds > 0 ? seconds / intervals : 1;
        const double decade = std::pow(10., std::floor(std::log10(rawStep)));
        const double fraction = rawStep / decade;
        const double step = decade * (fraction < 2 ? 1 : fraction < 5 ? 2 : 5);
        const int decimals = std::clamp(-int(std::floor(std::log10(step))), 0, 6);
        QVariantList xTicks, yRanges, yTicks;
        const double first = start + offset, last = first + seconds;
        for (double tick = std::ceil(first / step - 1e-9) * step; tick <= last + step * 1e-9; tick += step)
            xTicks << (std::abs(tick) < step * 1e-9 ? 0 : tick);
        setProperty("axisXRange", QVariantList{start + offset, start + offset + seconds});
        setProperty("axisXTickValues", xTicks);
        setProperty("axisXLabel", xAxisLabel);
        setProperty("axisYLabel", yAxisLabel);
        setProperty("plotArea", QRectF(left, firstTop, right - left, std::max(1., lastBottom - firstTop)));
        const std::array<QColor, 5> colors{QColor("#64748b"), QColor("#2563a8"), QColor("#228367"),
                                          QColor("#ab6226"), QColor("#9056a4")};
        const double common = fullPeaks.empty() ? 0 : *std::max_element(fullPeaks.begin(), fullPeaks.end());
        for (int r = 0; r < n; ++r) {
            const auto& values = traces[r];
            const double top = r * row, plotTop = top + firstTop;
            const double plotBottom = std::max(plotTop + 2, (r + 1) * row - 6);
            double maximum = r < int(fullPeaks.size()) ? fullPeaks[r] : 0;
            double scale = (sharedAmplitude ? common : maximum);
            if (scale < 1e-12) scale = 1;
            scale /= amplitudeZoom;
            const double minimum = frequency ? 0 : -scale;
            const double span = scale - minimum;
            const auto ordinate = [&](double value) {
                return plotBottom - (value - minimum) / span * (plotBottom - plotTop);
            };
            yRanges << QVariant(QVariantList{minimum, scale});
            QVariantList ticks;
            const int divisions = plotBottom - plotTop >= 140 ? 4 : plotBottom - plotTop >= 46 ? 2 : 1;
            for (int t = 0; t <= divisions; ++t) {
                const double value = minimum + span * t / divisions;
                ticks << value;
                const double y = ordinate(value);
                p.setPen(grid); p.drawLine(QPointF(left, y), QPointF(right, y));
                p.setPen(axis); p.drawLine(QPointF(left - 4, y), QPointF(left, y));
                p.setPen(ink);
                const auto label = locale.toString(std::abs(value) < 1e-12 ? 0 : value, 'g', 3);
                p.drawText(QRectF(21, std::clamp(y - fm.height() / 2., plotTop, std::max(plotTop, plotBottom - fm.height())),
                                 left - 29, fm.height()), Qt::AlignRight | Qt::AlignVCenter, label);
            }
            yTicks << QVariant(ticks);
            for (const auto& tick : xTicks) {
                const double x = left + (right - left) * (tick.toDouble() - first) / std::max(1e-12, seconds);
                p.setPen(grid); p.drawLine(QPointF(x, plotTop), QPointF(x, plotBottom));
            }
            p.setPen(axis);
            p.drawLine(QPointF(left, plotTop), QPointF(left, plotBottom));
            p.drawLine(QPointF(left, plotBottom), QPointF(right, plotBottom));
            p.setPen(ink);
            p.drawText(QRectF(26, top + 2, width() - 40, 20), Qt::AlignLeft,
                       fm.elidedText(names.value(r), Qt::ElideRight, width() - 40));
            if (groupPreview && r < int(peakToPeaks.size()))
                p.drawText(QRectF(26, top + 20, width() - 40, 18), Qt::AlignLeft,
                    peakToPeakMessage.arg(locale.toString(peakToPeaks[r] < 1e-9 ? 0 : peakToPeaks[r], 'g', 4)));
            if (values.empty()) continue;
            const auto begin = frequency ? size_t(0) : std::min(values.size(), size_t(std::max(0., offset * fs)));
            const auto end = frequency ? values.size() : std::min(values.size(), size_t(std::ceil((offset + seconds) * fs)));
            if (end <= begin) continue;
            QPainterPath path;
            bool penDown = false;
            // Pixel buckets retain extrema; their positions follow the axis domain.
            const double binsPerView = frequency ? std::max<size_t>(1, values.size() - 1) : seconds * fs;
            for (int x = left; x <= right; ++x) {
                const double at = frequency ? 0 : offset * fs;
                const auto a = std::max(begin, size_t(std::max(0., std::ceil(at + binsPerView * (x - left) / (right - left)))));
                const auto b = std::min(end, std::max(a + 1, size_t(std::max(0., std::ceil(at + binsPerView * (x - left + 1) / (right - left))))));
                if (a >= end) break;
                bool finite = true;
                for (auto i = a; i < b; ++i) finite = finite && std::isfinite(values[i]);
                if (!finite) { penDown = false; continue; }
                const auto limits = std::minmax_element(values.begin() + a, values.begin() + b);
                const double y = ordinate(*limits.first), z = ordinate(*limits.second);
                if (!penDown) path.moveTo(x, y); else path.lineTo(x, y);
                path.lineTo(x, z); penDown = true;
            }
            p.save();
            p.setClipRect(QRectF(left, plotTop, right - left + 1, plotBottom - plotTop));
            p.setPen(QPen(colors[r % 5], 1.1)); p.drawPath(path);
            if (!frequency && cursor >= offset && cursor <= offset + seconds && seconds > 0) {
                const double x = left + (right - left) * (cursor - offset) / seconds;
                p.setPen(QPen(dark ? QColor("#e4baf0") : QColor("#68237a"), 1.3, Qt::DashLine));
                p.drawLine(QPointF(x, plotTop), QPointF(x, plotBottom));
            }
            p.restore();
            p.setPen(ink);
            if (r < int(emptyGroups.size()) && emptyGroups[r])
                p.drawText(QRectF(left + 4, plotTop, right - left - 8, plotBottom - plotTop), Qt::AlignCenter, emptyMessage);
            if (groupPreview && r < int(smallWaves.size()) && (smallWaves[r] || clipped[r])) {
                const auto message = clipped[r] ? clippedMessage : smallWaveMessage;
                p.drawText(QRectF(left + 4, plotTop, right - left - 8, fm.height()), Qt::AlignLeft,
                           fm.elidedText(message, Qt::ElideRight, right - left - 8));
            }
        }
        setProperty("axisYRanges", yRanges);
        setProperty("axisYTickValues", yTicks);
        p.setPen(ink);
        for (int t = 0; t < xTicks.size(); ++t) {
            const double x = left + (right - left) * (xTicks[t].toDouble() - first) / std::max(1e-12, seconds);
            p.setPen(axis); p.drawLine(QPointF(x, lastBottom), QPointF(x, lastBottom + 4));
            p.setPen(ink);
            const auto label = locale.toString(xTicks[t].toDouble(), 'f', decimals);
            const int w = fm.horizontalAdvance(label) + 2;
            const double at = std::clamp(x - w / 2., double(left), double(std::max(left, right - w)));
            p.drawText(QRectF(at, lastBottom + 5, w, line), Qt::AlignHCenter, label);
        }
        p.drawText(QRectF(left, height() - line - 2, right - left, line), Qt::AlignLeft, xAxisLabel);
        if (!frequency && cursor >= 0) {
            const auto label = cursorMessage.arg(locale.toString(start + cursor, 'f', 1));
            if (fm.horizontalAdvance(xAxisLabel) + fm.horizontalAdvance(label) + 12 < right - left)
                p.drawText(QRectF(left, height() - line - 2, right - left, line), Qt::AlignRight, label);
        }
        const auto yTitle = fm.horizontalAdvance(yAxisLabel) <= lastBottom - firstTop ? yAxisLabel : yAxisUnit;
        setProperty("displayedYAxisLabel", yTitle);
        p.save(); p.translate(2, lastBottom); p.rotate(-90);
        p.drawText(QRectF(0, 0, std::max(1., lastBottom - firstTop), 18), Qt::AlignCenter,
                   fm.elidedText(yTitle, Qt::ElideRight, std::max(1, int(lastBottom - firstTop))));
        p.restore();
    }

};
static QString separationStyle() {
    const auto tone = surfaceColors();
    const auto cssColor = [](const QColor& color) {
        return QString("rgba(%1,%2,%3,%4)").arg(color.red()).arg(color.green()).arg(color.blue()).arg(color.alpha());
    };
    const auto feedbackStyle = [&](const char* name, const QColor& foreground, const QColor& background) {
        return QString("QLabel[feedbackTone=\"%1\"] { color: %2; background: %3; border-radius: 4px; padding: 8px; }")
            .arg(QString::fromLatin1(name), cssColor(foreground), cssColor(background));
    };
    return nativeFluentStyle() +
        QString("QLabel[workflowStepLabel=\"true\"][currentStep=\"true\"] { background: %1; color: %2; }")
            .arg(cssColor(tone.systemInfoBg), cssColor(tone.textAccentPrimary)) +
        feedbackStyle("error", tone.systemCritical, tone.systemCriticalBg) +
        feedbackStyle("warning", tone.systemCaution, tone.systemCautionBg) +
        feedbackStyle("success", tone.systemSuccess, tone.systemSuccessBg) +
        feedbackStyle("info", tone.textPrimary, tone.systemInfoBg) + QString(R"(
        QScrollBar:vertical { background: %1; width: 12px; margin: 0; border: none; }
        QScrollBar::handle:vertical { background: %2; min-height: 28px; margin: 2px; border-radius: 4px; }
        QScrollBar::handle:vertical:hover { background: %3; }
        QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical { height: 0; }
        QScrollBar::add-page:vertical, QScrollBar::sub-page:vertical { background: none; }
    )").arg(cssColor(tone.bgCanvas), cssColor(tone.textSecondary), cssColor(tone.textPrimary));
}
static void setFeedbackTone(QLabel* label, const char* tone) {
    const auto value = QString::fromLatin1(tone);
    if (label->property("feedbackTone").toString() == value) return;
    label->setProperty("feedbackTone", value);
    label->style()->unpolish(label);
    label->style()->polish(label);
    label->update();
}
static QLabel *wrapped(QWidget *parent) {
    auto *label = new QLabel(parent);
    label->setWordWrap(true);
    label->setMinimumWidth(0);
    return label;
}
static QScrollArea *scrolling(QWidget *content, QWidget *parent) {
    auto *area = new QScrollArea(parent);
    area->setWidgetResizable(true);
    area->setFrameShape(QFrame::NoFrame);
    area->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    area->setWidget(content);
    return area;
}
QString EegSeparationWorkspace::text(const char *key, const QJsonObject &params) const {
    auto value=i18n_->text(QString("app.separation.%1").arg(QString::fromLatin1(key)),params);
    const auto unit=controller_->source().metadata.value("unit").toString("uV");
    if(unit!="uV") {value.replace(QString::fromUtf8("µV"),unit);value.replace(QString::fromUtf8("μV"),unit);value.replace("uV",unit);}
    return value;
}
EegSeparationWorkspace::EegSeparationWorkspace(EegSeparationController *controller, I18nService *i18n,
                                               QWidget *parent)
    : QWidget(parent), controller_(controller), i18n_(i18n) {
    setObjectName("eegSeparationWorkspace");
    previewTimer_ = new QTimer(this);
    previewTimer_->setTimerType(Qt::PreciseTimer);
    previewTimer_->setInterval(40);
    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(12, 12, 12, 12);
    root->setSpacing(12);
    auto *header = new QHBoxLayout;
    title_ = wrapped(this);
    title_->setObjectName("separationTitle");
    auto font = title_->font();
    font.setBold(true);
    font.setPointSize(font.pointSize() + 3);
    title_->setFont(font);
    header->addWidget(title_, 1);
    helpButton_ = new ActionButton(this);
    helpButton_->setObjectName("separationHelpButton");
    header->addWidget(helpButton_);
    connect(helpButton_, &QPushButton::clicked, this, [this] { showHelp(controller_->output() ? 4 : 0); });
    auto* helpShortcut = new QShortcut(QKeySequence::HelpContents, this);
    helpShortcut->setContext(Qt::WidgetWithChildrenShortcut);
    connect(helpShortcut, &QShortcut::activated, this, [this] { showHelp(controller_->output() ? 4 : 0); });
    root->addLayout(header);
    auto* workflowPanel = new QFrame(this);
    workflowPanel->setObjectName("separationWorkflowPanel");
    auto* workflowLayout = new QVBoxLayout(workflowPanel);
    workflowLayout->setContentsMargins(12, 10, 12, 10);
    workflowLayout->setSpacing(8);
    auto* workflow = new QHBoxLayout;
    workflow->setSpacing(4);
    for (int i = 0; i < 4; ++i) {
        workflowSteps_[i] = wrapped(workflowPanel);
        workflowSteps_[i]->setObjectName(QString("separationWorkflowStep%1").arg(i + 1));
        workflowSteps_[i]->setProperty("workflowStepLabel", true);
        workflow->addWidget(workflowSteps_[i], 1);
    }
    workflowLayout->addLayout(workflow);
    auto* actions = new ControlBar(workflowPanel);
    actions->setObjectName("separationActionBar");
    actions->setWrappedMinimum(true);
    main_ = new ActionButton(actions);
    main_->setObjectName("separationMainAction");
    main_->setDefault(true);
    rerun_ = new ActionButton(actions);
    rerun_->setObjectName("separationRerun");
    liveToggle_ = new ActionButton(actions);
    liveToggle_->setObjectName("separationLiveToggle");
    retry_ = new ActionButton(actions);
    retry_->setObjectName("separationRetry");
    cancel_ = new ActionButton(actions);
    cancel_->setObjectName("separationCancel");
    for (auto* button : {main_, retry_, rerun_, liveToggle_, cancel_}) {
        button->setMinimumHeight(34);
        actions->addWidget(button);
    }
    workflowLayout->addWidget(actions);
    liveControls_ = new QWidget(workflowPanel);
    auto* liveControls = new QVBoxLayout(liveControls_);
    liveControls->setContentsMargins(0, 0, 0, 0);
    liveStatus_ = wrapped(liveControls_);
    liveStatus_->setObjectName("separationLiveStatus");
    liveStatus_->setTextFormat(Qt::PlainText);
    liveControls->addWidget(liveStatus_);
    workflowLayout->addWidget(liveControls_);
    connect(liveToggle_, &QPushButton::clicked, this, [this] {
        if (liveToggleRequested) liveToggleRequested();
    });
    subtitle_ = wrapped(this);
    hint_ = wrapped(workflowPanel);
    hint_->setObjectName("separationNextStep");
    hint_->setTextFormat(Qt::PlainText);
    workflowLayout->addWidget(hint_);
    status_ = wrapped(workflowPanel);
    status_->setObjectName("separationStatus");
    status_->setTextFormat(Qt::PlainText);
    status_->setTextInteractionFlags(Qt::TextSelectableByMouse | Qt::TextSelectableByKeyboard);
    workflowLayout->addWidget(status_);
    root->addWidget(workflowPanel);
    auto *splitter = new WorkspaceSplitter(this);
    splitter->setObjectName("separationSplitter");
    root->addWidget(splitter, 1);
    auto *input = new QWidget(splitter);
    input->setMinimumWidth(235);
    auto *il = new QVBoxLayout(input);
    il->setContentsMargins(0, 0, 0, 0);
    il->setSpacing(8);
    auto* inputSummary = new QFrame(input);
    inputSummary->setObjectName("separationInputSummary");
    auto* summaryLayout = new QVBoxLayout(inputSummary);
    summaryLayout->setContentsMargins(12, 12, 12, 12);
    summaryLayout->setSpacing(8);
    sourceTitle_ = wrapped(inputSummary);
    sourceTitle_->setObjectName("separationInputTitle");
    auto sectionFont = sourceTitle_->font();
    sectionFont.setBold(true);
    sourceTitle_->setFont(sectionFont);
    summaryLayout->addWidget(sourceTitle_);
    source_ = wrapped(inputSummary);
    source_->setObjectName("separationSource");
    summaryLayout->addWidget(source_);
    deviceInputStatus_ = wrapped(inputSummary);
    deviceInputStatus_->setObjectName("separationDeviceStatus");
    summaryLayout->addWidget(deviceInputStatus_);
    deviceStream_ = new ChoiceBox(inputSummary);
    deviceStream_->setObjectName("separationDeviceStream");
    deviceStream_->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
    deviceStream_->setMinimumContentsLength(8);
    deviceStream_->hide();
    summaryLayout->addWidget(deviceStream_);
    deviceInput_ = new ActionButton(inputSummary);
    deviceInput_->setObjectName("separationDeviceSnapshot");
    deviceInput_->hide();
    summaryLayout->addWidget(deviceInput_);
    connect(deviceInput_, &QPushButton::clicked, this, [this] {
        if (deviceSnapshotRequested) {
            pausePreview(); errorCode_.clear();
            deviceSnapshotRequested(deviceStream_->currentData().toString());
        }
    });
    teachingHelp_ = wrapped(inputSummary);
    summaryLayout->addWidget(teachingHelp_);
    metadataButton_ = new ActionButton(inputSummary);
    metadataButton_->setObjectName("separationMetadataButton");
    summaryLayout->addWidget(metadataButton_);
    connect(metadataButton_, &QPushButton::clicked, this, [this] { showHelp(3); });
    il->addWidget(inputSummary);
    hardware_ = wrapped(input);
    hardware_->setObjectName("separationHardware");
    rangeLabel_ = wrapped(input);
    rangeLabel_->setFont(sectionFont);
    il->addSpacing(4);
    il->addWidget(rangeLabel_);
    start_ = new QDoubleSpinBox(input);
    start_->setObjectName("separationStart");
    start_->setDecimals(6);
    start_->setSuffix(" s");
    start_->setRange(0, 8640000);
    duration_ = new QDoubleSpinBox(input);
    duration_->setObjectName("separationDuration");
    duration_->setDecimals(6);
    duration_->setSuffix(" s");
    duration_->setRange(5, 120);
    duration_->setValue(60);
    il->addWidget(start_);
    il->addWidget(duration_);
    rolesHelp_ = wrapped(input);
    channels_ = new QTableWidget(input);
    channels_->setObjectName("separationChannels");
    channels_->setColumnCount(2);
    channels_->setSelectionBehavior(QAbstractItemView::SelectRows);
    channels_->setSelectionMode(QAbstractItemView::ExtendedSelection);
    channels_->verticalHeader()->hide();
    channels_->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    channels_->horizontalHeader()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    channels_->setMinimumHeight(130);
    il->addWidget(channels_, 1);
    allObservations_ = new ActionButton(input);
    allObservations_->setObjectName("separationAllObservations");
    il->addWidget(allObservations_);
    connect(allObservations_, &QPushButton::clicked, this, [this] {
        auto cfg = controller_->settings();
        std::fill(cfg.roles.begin(), cfg.roles.end(), 1);
        controller_->setSettings(cfg);
        refreshSource();
    });
    bulkActions_ = new QWidget(input);
    bulkActions_->setObjectName("separationBulkActions");
    auto *bulkLayout = new QVBoxLayout(bulkActions_);
    bulkLayout->setContentsMargins(0, 0, 0, 0);
    bulkSelection_ = wrapped(bulkActions_);
    bulkSelection_->setObjectName("separationBulkSelection");
    bulkLayout->addWidget(bulkSelection_);
    bulkRole_ = new ChoiceBox(bulkActions_);
    bulkRole_->setObjectName("separationBulkRole");
    bulk_ = new ActionButton(bulkActions_);
    bulk_->setObjectName("separationApplyRole");
    bulkLayout->addWidget(bulkRole_);
    bulkLayout->addWidget(bulk_);
    il->addWidget(bulkActions_);
    advanced_ = new CollapsiblePanel({}, input);
    advanced_->setObjectName("separationAdvanced");
    rate_ = new ChoiceBox(input);
    rate_->setObjectName("separationOutputRate");
    rate_->addItem("500 Hz", 500);
    rate_->addItem("250 Hz", 250);
    notch_ = new ChoiceBox(input);
    notch_->setObjectName("separationNotch");
    notch_->addItem(QString(), 0);
    notch_->addItem("50 Hz", 50);
    notch_->addItem("60 Hz", 60);
    advanced_->addWidget(rate_);
    advanced_->addWidget(notch_);
    advanced_->setExpanded(false);
    il->addWidget(advanced_);
    helpDialog_ = new QDialog(this);
    helpDialog_->setObjectName("separationHelpDialog");
    helpDialog_->setModal(false);
    helpDialog_->setWindowModality(Qt::NonModal);
    helpDialog_->setMinimumSize(400, 320);
    auto* helpLayout = new QVBoxLayout(helpDialog_);
    helpTabs_ = new QTabWidget(helpDialog_);
    helpTabs_->setObjectName("separationHelpTabs");
    shortcutsHelp_ = wrapped(helpDialog_);
    reviewHelp_ = wrapped(helpDialog_);
    reviewHelp_->setObjectName("separationReviewHelp");
    sourceDetails_ = wrapped(helpDialog_);
    sourceDetails_->setObjectName("separationAcquisitionSummary");
    for (auto* label : {subtitle_, rolesHelp_, shortcutsHelp_, sourceDetails_, hardware_, reviewHelp_}) {
        label->setTextFormat(Qt::PlainText);
        label->setTextInteractionFlags(Qt::TextSelectableByMouse | Qt::TextSelectableByKeyboard);
    }
    for (int tab = 0; tab < 5; ++tab) {
        auto* page = new QWidget(helpDialog_);
        auto* content = new QVBoxLayout(page);
        content->setContentsMargins(16, 16, 16, 16);
        content->setSpacing(16);
        if (tab == 0) content->addWidget(subtitle_);
        if (tab == 1) content->addWidget(rolesHelp_);
        if (tab == 2) content->addWidget(shortcutsHelp_);
        if (tab == 3) { content->addWidget(sourceDetails_); content->addWidget(hardware_); }
        if (tab == 4) content->addWidget(reviewHelp_);
        content->addStretch();
        helpTabs_->addTab(scrolling(page, helpDialog_), QString());
    }
    helpLayout->addWidget(helpTabs_, 1);
    helpClose_ = new ActionButton(helpDialog_);
    helpClose_->setObjectName("separationHelpClose");
    auto* helpFooter = new QHBoxLayout;
    helpFooter->addStretch(); helpFooter->addWidget(helpClose_);
    helpLayout->addLayout(helpFooter);
    connect(helpClose_, &QPushButton::clicked, helpDialog_, &QDialog::hide);
    splitter->addWidget(scrolling(input, splitter));
    auto *preview = new QWidget(splitter);
    preview->setMinimumWidth(250);
    auto *pl = new QVBoxLayout(preview);
    previewTitle_ = wrapped(preview);
    previewTitle_->setFont(sectionFont);
    pl->addWidget(previewTitle_);
    previewControls_ = new QGridLayout;
    previewChannelLabel_ = new QLabel(preview);
    previewChannelLabel_->setObjectName("separationPreviewChannelLabel");
    channel_ = new ChoiceBox(preview);
    channel_->setObjectName("separationPreviewChannel");
    channel_->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
    window_ = new ChoiceBox(preview);
    window_->setObjectName("separationDisplayWindow");
    displayWindowLabel_ = new QLabel(preview);
    displayWindowLabel_->setObjectName("separationDisplayWindowLabel");
    window_->addItem("10 s", 10);
    window_->addItem("5 s", 5);
    previewChannelLabel_->setBuddy(channel_);
    displayWindowLabel_->setBuddy(window_);
    pl->addLayout(previewControls_);
    amplitudeControls_ = new QGridLayout;
    amplitudeModeLabel_ = new QLabel(preview);
    amplitudeMode_ = new ChoiceBox(preview);
    amplitudeMode_->setObjectName("separationAmplitudeMode");
    amplitudeMode_->addItem(QString(), 0);
    amplitudeMode_->addItem(QString(), 1);
    amplitudeModeLabel_->setBuddy(amplitudeMode_);
    amplitudeZoomLabel_ = new QLabel(preview);
    amplitudeZoomLabel_->setObjectName("separationWaveformZoomLabel");
    amplitudeZoom_ = new ChoiceBox(preview);
    amplitudeZoom_->setObjectName("separationAmplitudeZoom");
    for (int zoom : {1, 2, 5, 10})
        amplitudeZoom_->addItem(QStringLiteral("%1×").arg(zoom), zoom);
    amplitudeZoomLabel_->setBuddy(amplitudeZoom_);
    pl->addLayout(amplitudeControls_);
    arrangePreviewControls();
    amplitudeHelp_ = wrapped(preview);
    amplitudeHelp_->setObjectName("separationAmplitudeHelp");
    pl->addWidget(amplitudeHelp_);
    groups_ = new SeparationTraceView(preview);
    groups_->setObjectName("separationGroupTraces");
    groups_->setFocusPolicy(Qt::StrongFocus);
    groups_->installEventFilter(this);
    groups_->sharedAmplitude = true;
    groups_->groupPreview = true;
    pl->addWidget(groups_, 1);
    scrub_ = new ProgressSlider(Qt::Horizontal, preview);
    scrub_->setRange(0, 1000);
    scrub_->setObjectName("separationPreviewPosition");
    auto *playbackControls = new QHBoxLayout;
    play_ = new ActionButton(preview);
    play_->setObjectName("separationPlay");
    restart_ = new ActionButton(preview);
    restart_->setObjectName("separationRestart");
    playbackPosition_ = wrapped(preview);
    playbackPosition_->setObjectName("separationPlaybackPosition");
    playbackControls->addWidget(play_);
    playbackControls->addWidget(restart_);
    playbackControls->addWidget(playbackPosition_, 1);
    pl->addLayout(playbackControls);
    pl->addWidget(scrub_);
    splitter->addWidget(preview);
    auto *evidence = new QWidget(splitter);
    evidence->setMinimumWidth(265);
    auto *el = new QVBoxLayout(evidence);
    el->setSpacing(3);
    el->setContentsMargins(9, 9, 9, 0);
    evidenceTitle_ = wrapped(evidence);
    evidenceTitle_->setFont(sectionFont);
    el->addWidget(evidenceTitle_);
    reviewHint_ = wrapped(evidence);
    reviewHint_->setObjectName("separationReviewHint");
    el->addWidget(reviewHint_);
    components_ = new QTableWidget(evidence);
    components_->setObjectName("separationComponents");
    components_->setColumnCount(2);
    components_->verticalHeader()->hide();
    components_->setSelectionBehavior(QAbstractItemView::SelectRows);
    components_->setSelectionMode(QAbstractItemView::SingleSelection);
    components_->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    components_->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
    components_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    components_->verticalHeader()->setDefaultSectionSize(fontMetrics().height() + 8);
    components_->setMinimumHeight(75);
    components_->setMaximumHeight(140);
    el->addWidget(components_);
    auto* componentNavigation = new QHBoxLayout;
    selectedComponent_ = wrapped(evidence); selectedComponent_->setObjectName("separationSelectedComponent");
    previousComponent_ = new ActionButton(evidence); previousComponent_->setObjectName("separationPreviousComponent");
    nextComponent_ = new ActionButton(evidence); nextComponent_->setObjectName("separationNextComponent");
    componentNavigation->addWidget(selectedComponent_, 1);
    componentNavigation->addWidget(previousComponent_); componentNavigation->addWidget(nextComponent_);
    el->addLayout(componentNavigation);
    connect(previousComponent_, &QPushButton::clicked, this, [this] { components_->setCurrentCell(components_->currentRow() - 1, 0); });
    connect(nextComponent_, &QPushButton::clicked, this, [this] { components_->setCurrentCell(components_->currentRow() + 1, 0); });
    auto* classification = new QGridLayout;
    classificationGroup_ = new QButtonGroup(this);
    classificationGroup_->setObjectName("separationClassificationChoices");
    for (int group = 0; group < 4; ++group) {
        auto* button = new fluent::basicinput::RadioButton(evidence);
        button->setTextFont(workbenchFont()); button->setCircleSize(18); button->setTextGap(7);
        classificationGroup_->addButton(button, group);
        button->setObjectName(QString("separationClassify%1").arg(group));
        classificationButtons_[group] = button; classification->addWidget(button, group / 2, group % 2, Qt::AlignLeft);
        connect(button, &QRadioButton::clicked, this, [this, group] {
            if (!refreshing_ && components_->currentRow() >= 0) controller_->setLabel(components_->currentRow(), group);
            refreshComponentSelection();
        });
    }
    el->addLayout(classification);
    auto* divider = new QFrame(evidence);
    divider->setObjectName("separationClassificationDivider");
    divider->setProperty("processingDivider", true);
    el->addWidget(divider);

    undo_ = new ActionButton(evidence);
    undo_->setObjectName("separationUndo");
    auto* labelActions = new QHBoxLayout;
    labelActions->addWidget(undo_);
    exampleLabels_ = new ActionButton(evidence);
    exampleLabels_->setObjectName("separationTeachingLabels");
    labelActions->addWidget(exampleLabels_);
    el->addLayout(labelActions);
    componentViews_ = new QTabWidget(evidence);
    componentViews_->setObjectName("separationComponentViews");
    componentViews_->setMinimumHeight(150);
    componentTrace_ = new SeparationTraceView(componentViews_);
    componentTrace_->setObjectName("separationComponentTrace");
    componentTrace_->setFocusPolicy(Qt::StrongFocus);
    componentTrace_->installEventFilter(this);
    spectrum_ = new SeparationTraceView(componentViews_);
    spectrum_->setObjectName("separationSpectrum");
    auto* referenceView = new QWidget(componentViews_);
    auto* referenceLayout = new QVBoxLayout(referenceView);
    referenceLayout->setContentsMargins(8, 8, 8, 8);
    clues_ = wrapped(referenceView); clues_->setObjectName("separationClues");
    sourceCaption_ = wrapped(referenceView);
    referenceLayout->addWidget(clues_); referenceLayout->addWidget(sourceCaption_); referenceLayout->addStretch();
    componentViews_->addTab(componentTrace_, QString());
    componentViews_->addTab(spectrum_, QString());
    componentViews_->addTab(scrolling(referenceView, componentViews_), QString());
    el->addWidget(componentViews_, 1);
    confirmed_ = new CheckOption(evidence);
    confirmed_->setObjectName("separationConfirmed");
    confirmed_->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Minimum);
    el->addWidget(confirmed_);
    reviewConfirmHint_ = wrapped(evidence);
    reviewConfirmHint_->setObjectName("separationReviewConfirmHint");
    el->addWidget(reviewConfirmHint_);
    splitter->addWidget(evidence);
    splitter->setStretchFactor(1, 1);
    splitter->setSizes({265, 610, 335});
    connect(play_, &QPushButton::clicked, this, &EegSeparationWorkspace::togglePreview);
    connect(restart_, &QPushButton::clicked, this, [this] {
        pausePreview();
        setPreviewPosition(0);
    });
    connect(previewTimer_, &QTimer::timeout, this, [this] {
        const double next = playbackAnchor_ + previewClock_.nsecsElapsed() / 1e9;
        if (next >= previewDuration())
            previewTimer_->stop();
        setPreviewPosition(next);
    });
    connect(exampleLabels_, &QPushButton::clicked, controller_,
            &EegSeparationController::applyTeachingLabels);
    connect(bulkRole_, &QComboBox::currentIndexChanged, this, [this] { refreshRoleHelp(bulkRole_); });
    connect(bulk_, &QPushButton::clicked, this, [this] {
        if (controller_->isBusy() || inputPending_ || liveActive_ || channels_->selectionModel()->selectedRows().size() < 2)
            return;
        auto settings = controller_->settings();
        for (const auto &index : channels_->selectionModel()->selectedRows())
            settings.roles.at(index.row()) = bulkRole_->currentIndex();
        controller_->setSettings(settings);
        refreshSource();
    });
    connect(channels_, &QTableWidget::itemSelectionChanged, this,
            &EegSeparationWorkspace::refreshBulkActions);
    connect(start_, &QDoubleSpinBox::valueChanged, this, [this] { applySettings(); });
    connect(duration_, &QDoubleSpinBox::valueChanged, this, [this] { applySettings(); });
    connect(rate_, &QComboBox::currentIndexChanged, this, [this] { applySettings(); });
    connect(notch_, &QComboBox::currentIndexChanged, this, [this] { applySettings(); });
    for (auto* combo : {window_, amplitudeZoom_}) {
        combo->addItem(QString(), 0.5);
        combo->addItem(QString(), 1.0 / 4);
    }
    window_->addItem(QString(), 1); window_->addItem(QString(), 2); window_->addItem(QString(), 20);
    amplitudeZoom_->addItem(QString(), 16);
    for (auto *combo : {channel_, window_, amplitudeMode_, amplitudeZoom_})
        connect(combo, &QComboBox::currentIndexChanged, this, [this] {
            if (!refreshing_) pausePreview();
            refreshPlots();
        });
    connect(scrub_, &QSlider::sliderPressed, this, &EegSeparationWorkspace::pausePreview);
    connect(scrub_, &QSlider::valueChanged, this, [this](int value) {
        pausePreview();
        setPreviewPosition(previewDuration() * value / 1000.);
    });
    connect(components_, &QTableWidget::currentCellChanged, this, [this] {
        if (!refreshing_) {
            pausePreview(); refreshComponentSelection(); refreshPlots();
        }
    });
    connect(undo_, &QPushButton::clicked, controller_, &EegSeparationController::undoLabel);
    auto *undoShortcut = new QShortcut(QKeySequence::Undo, this);
    undoShortcut->setContext(Qt::WidgetWithChildrenShortcut);
    connect(undoShortcut, &QShortcut::activated, controller_, &EegSeparationController::undoLabel);
    connect(confirmed_, &QCheckBox::toggled, controller_, &EegSeparationController::setConfirmed);
    connect(cancel_, &QPushButton::clicked, this, [this] {
        if (inputPending_ && inputCancellationRequested) inputCancellationRequested();
        else controller_->cancel();
    });
    connect(rerun_, &QPushButton::clicked, this, [this] {
        pausePreview();
        errorCode_.clear();
        controller_->start();
    });
    connect(main_, &QPushButton::clicked, this, [this] {
        errorCode_.clear();
        refresh();
        if (controller_->output())
            exportResults();
        else
            controller_->start();
    });
    connect(retry_, &QPushButton::clicked, this, [this] {
        errorCode_.clear();
        refresh();
        if (controller_->output())
            exportResults();
        else if (controller_->source().path.isEmpty())
            chooseSource();
        else
            controller_->start();
    });
    connect(controller_, &EegSeparationController::stateChanged, this, &EegSeparationWorkspace::refresh);
    connect(controller_, &EegSeparationController::sourceChanged, this,
            &EegSeparationWorkspace::refreshSource);
    connect(controller_, &EegSeparationController::resultChanged, this, [this] {
        errorCode_.clear();
        previewTimer_->stop();
        previewPosition_ = 0;
        previewOffset_ = -1;
        {
            const QSignalBlocker b(scrub_);
            scrub_->setValue(0);
        }
        refreshComponents();
        refresh();
    });
    connect(controller_, &EegSeparationController::labelsChanged, this, [this] {
        errorCode_.clear();
        pausePreview();
        refreshComponents();
        refresh();
    });
    connect(controller_, &EegSeparationController::confirmationChanged, this,
            &EegSeparationWorkspace::refresh);
    connect(controller_, &EegSeparationController::progressChanged, this, [this](const QString &stage) {
        stage_ = stage;
        refresh();
    });
    connect(controller_, &EegSeparationController::unitRequired, this, [this](const QString &path) {
        bool accepted = false;
        const auto unit = QInputDialog::getItem(this, text("unit_title"), text("unit_required"),
                                                {"uV", "mV", "V"}, 0, false, &accepted);
        if (accepted)
            controller_->openSource(path, unit);
    });
    connect(controller_, &EegSeparationController::failed, this, &EegSeparationWorkspace::showError);
    connect(controller_, &EegSeparationController::sourceReplacementFailed, this, &EegSeparationWorkspace::showInputError);
    connect(controller_, &EegSeparationController::exported, this,
            [this](const QString &) { refresh(); });
    connect(i18n_, &I18nService::languageChanged, this, &EegSeparationWorkspace::retranslateUi);
    retranslateUi();
    refreshSource();
    setStyleSheet(separationStyle());
}
void EegSeparationWorkspace::chooseSource() {
    if (!canChangeInput()) return;
    pausePreview();
    const auto path = QFileDialog::getOpenFileName(this, text("open"));
    if (!path.isEmpty()) {
        errorCode_.clear();
        controller_->openSource(path);
    }
}
void EegSeparationWorkspace::showHelp(int tab) {
    helpTabs_->setCurrentIndex(tab);
    if (!helpDialog_->isVisible())
        helpDialog_->resize(std::clamp(window()->width() - 40, 400, 760),
                            std::clamp(window()->height() - 80, 320, 560));
    helpDialog_->show(); helpDialog_->raise(); helpDialog_->activateWindow();
}
void EegSeparationWorkspace::refreshRoleHelp(QComboBox* combo) {
    if (!combo) return;
    for (int role = 0; role < combo->count(); ++role) {
        const auto explanation = text(qPrintable(QString("role_help_%1").arg(role)));
        combo->setItemData(role, explanation, Qt::ToolTipRole);
        combo->setItemData(role, explanation, Qt::AccessibleDescriptionRole);
    }
    const auto explanation = combo->currentData(Qt::ToolTipRole).toString();
    combo->setToolTip(combo->currentText() + QStringLiteral("\n") + explanation);
    combo->setAccessibleDescription(explanation);
}
void EegSeparationWorkspace::refreshComponentSelection() {
    const int row = components_->currentRow(), count = components_->rowCount();
    const bool available = row >= 0 && row < int(controller_->labels().size());
    const bool enabled = available && !controller_->isBusy() && !inputPending_;
    selectedComponent_->setText(available ? text("selected_component", {{"component", row + 1}, {"count", count}}) : text("select_component"));
    previousComponent_->setEnabled(enabled && row > 0);
    nextComponent_->setEnabled(enabled && row + 1 < count);
    const QSignalBlocker groupBlocker(classificationGroup_);
    classificationGroup_->setExclusive(false);
    for (int group = 0; group < 4; ++group) {
        auto* button = classificationButtons_[group];
        const QSignalBlocker blocker(button);
        button->setChecked(available && controller_->labels()[row] == group);
        button->setEnabled(enabled);
    }
    classificationGroup_->setExclusive(true);
    for (int r = 0; r < count; ++r)
        for (int col = 0; col < 2; ++col) {
            if (auto* item = components_->item(r, col)) { auto font = item->font(); font.setBold(r == row); item->setFont(font); }
        }
}
void EegSeparationWorkspace::selectViewValue(QComboBox* combo, double value, bool time) {
    const QSignalBlocker blocker(combo);
    int index = -1, custom = -1;
    for (int i = 0; i < combo->count(); ++i) {
        if (std::abs(combo->itemData(i).toDouble() - value) < 1e-9) index = i;
        if (combo->itemData(i, Qt::UserRole + 1).toBool()) custom = i;
    }
    if (index < 0) {
        index = custom >= 0 ? custom : combo->count();
        if (custom < 0) combo->addItem(QString());
        combo->setItemData(index, value);
        combo->setItemData(index, true, Qt::UserRole + 1);
    }
    const int decimals = std::abs(value - std::round(value)) < 1e-9 ? 0 : 2;
    combo->setItemText(index, time ? text("window_value", {{"seconds", i18n_->formatNumber(value, decimals)}})
                                   : text("zoom_factor", {{"value", i18n_->formatNumber(value, decimals)}}));
    combo->setCurrentIndex(index);
}
void EegSeparationWorkspace::zoomGroupTime(double steps, double anchor) {
    if (!liveActive_) pausePreview();
    const double full = groups_->traces.empty() || groups_->fs <= 0 ? 0
        : groups_->traces.front().size() / groups_->fs;
    if (full <= 0 || groups_->seconds <= 0) return;
    const double next = std::clamp(groups_->seconds / std::pow(1.25, steps), std::min(.2, full), full);
    const double at = groups_->offset + std::clamp(anchor, 0., 1.) * groups_->seconds;
    if (!liveActive_ || liveViewFrozen_) previewOffset_ = std::clamp(at - anchor * next, 0., std::max(0., full - next));
    selectViewValue(window_, next, true);
    refreshPlots();
}
void EegSeparationWorkspace::zoomGroupAmplitude(double steps) {
    if (!liveActive_) pausePreview();
    selectViewValue(amplitudeZoom_, std::clamp(amplitudeZoom_->currentData().toDouble() * std::pow(1.25, steps), .25, 16.), false);
    refreshPlots();
}
void EegSeparationWorkspace::panGroupView(double seconds) {
    if (liveActive_ && !liveViewFrozen_) return;
    pausePreview();
    const double full = groups_->traces.empty() || groups_->fs <= 0 ? 0 : groups_->traces.front().size() / groups_->fs;
    previewOffset_ = std::clamp(groups_->offset + seconds, 0., std::max(0., full - groups_->seconds));
    refreshPlots();
}
void EegSeparationWorkspace::resetGroupView() {
    if (!liveActive_) pausePreview();
    previewOffset_ = -1;
    componentTrace_->amplitudeZoom = 1;
    selectViewValue(window_, 10, true);
    selectViewValue(amplitudeZoom_, 1, false);
    refreshPlots();
}
bool EegSeparationWorkspace::eventFilter(QObject* watched, QEvent* event) {
    if ((watched != groups_ && watched != componentTrace_) || !controller_->output() || controller_->isBusy() || groups_->traces.empty())
        return QWidget::eventFilter(watched, event);
    auto* target = watched == componentTrace_ ? componentTrace_ : groups_;
    const auto zoomAmplitude = [&](double steps) {
        if (target == groups_) zoomGroupAmplitude(steps);
        else {
            if (!liveActive_) pausePreview();
            componentTrace_->amplitudeZoom = std::clamp(componentTrace_->amplitudeZoom * std::pow(1.25, steps), .25, 16.);
            refreshPlots();
        }
    };
    const auto reset = [&] { if (target == componentTrace_) componentTrace_->amplitudeZoom = 1; resetGroupView(); };
    if (event->type() == QEvent::FocusIn || event->type() == QEvent::FocusOut) { target->update(); return false; }
    if (event->type() == QEvent::Wheel) {
        auto* wheel = static_cast<QWheelEvent*>(event);
        if (!(wheel->modifiers() & (Qt::ControlModifier | Qt::ShiftModifier))) return false;
        double steps = wheel->angleDelta().y() / 120.;
        if (!steps && wheel->pixelDelta().y()) steps = wheel->pixelDelta().y() > 0 ? 1 : -1;
        target->setFocus(Qt::MouseFocusReason);
        steps = std::clamp(steps, -4., 4.);
        if (wheel->modifiers() & Qt::ControlModifier)
            zoomGroupTime(steps, std::clamp((wheel->position().x() - target->plotLeft()) / std::max(1, target->plotRight() - target->plotLeft()), 0., 1.));
        else zoomAmplitude(steps);
        wheel->accept(); return true;
    }
    if (event->type() == QEvent::KeyPress) {
        auto* key = static_cast<QKeyEvent*>(event);
        switch (key->key()) {
        case Qt::Key_Plus: case Qt::Key_Equal: zoomGroupTime(1, .5); break;
        case Qt::Key_Minus: zoomGroupTime(-1, .5); break;
        case Qt::Key_Up: zoomAmplitude(1); break;
        case Qt::Key_Down: zoomAmplitude(-1); break;
        case Qt::Key_Left: panGroupView(-groups_->seconds * .2); break;
        case Qt::Key_Right: panGroupView(groups_->seconds * .2); break;
        case Qt::Key_0: reset(); break;
        case Qt::Key_Space: if (!key->isAutoRepeat()) toggleViewPause(); break;
        default: return false;
        }
        key->accept(); return true;
    }
    if (event->type() == QEvent::MouseButtonDblClick) {
        auto* mouse = static_cast<QMouseEvent*>(event);
        if (mouse->button() != Qt::LeftButton) return false;
        draggingGroups_ = false; reset(); mouse->accept(); return true;
    }
    if (event->type() == QEvent::MouseButtonPress) {
        auto* mouse = static_cast<QMouseEvent*>(event);
        if (mouse->button() != Qt::LeftButton) return false;
        target->setFocus(Qt::MouseFocusReason);
        draggingGroups_ = !liveActive_ || liveViewFrozen_;
        groupDragPosition_ = mouse->position();
        mouse->accept(); return true;
    } else if (event->type() == QEvent::MouseMove && draggingGroups_) {
        auto* mouse = static_cast<QMouseEvent*>(event);
        const double shift = mouse->position().x() - groupDragPosition_.x();
        groupDragPosition_ = mouse->position();
        panGroupView(-shift / std::max(1, target->plotRight() - target->plotLeft()) * groups_->seconds);
        mouse->accept(); return true;
    } else if (event->type() == QEvent::MouseButtonRelease) draggingGroups_ = false;
    return QWidget::eventFilter(watched, event);
}
bool EegSeparationWorkspace::canChangeInput() const {
    return !controller_->isBusy() && !inputPending_ && !liveActive_;
}
void EegSeparationWorkspace::openTeachingSource() {
    if (!canChangeInput()) return;
    pausePreview(); errorCode_.clear();
    controller_->openTeachingSource();
}
void EegSeparationWorkspace::setDeviceInput(const QString& name, const QJsonArray& streams, bool available) {
    const bool availabilityChanged = deviceAvailable_ != available;
    deviceName_ = name; deviceAvailable_ = available;
    const auto selected = deviceStream_->currentData().toString();
    const QSignalBlocker blocker(deviceStream_);
    bool changed = deviceStream_->count() != streams.size();
    for (int i = 0; !changed && i < streams.size(); ++i) {
        const auto stream = streams[i].toObject();
        changed = deviceStream_->itemData(i) != stream.value("key").toString() ||
                  deviceStream_->itemText(i) != stream.value("name").toString();
    }
    if (changed) {
        deviceStream_->clear();
        for (const auto& entry : streams) {
            const auto stream = entry.toObject();
            deviceStream_->addItem(stream.value("name").toString(), stream.value("key").toString());
        }
    }
    const int index = deviceStream_->findData(selected);
    if (index >= 0) deviceStream_->setCurrentIndex(index);
    deviceStream_->setVisible(available && deviceStream_->count() > 1);
    deviceInputStatus_->setText(text(available ? "device_available" : "device_unavailable", {{"device", name}}));
    deviceInput_->setVisible(available);
    deviceInput_->setEnabled(available && !inputPending_ && !liveActive_ && !controller_->isBusy());
    if (availabilityChanged) refresh();
}
void EegSeparationWorkspace::setInputPending(bool pending) {
    inputPending_ = pending;
    inputSeconds_ = -1;
    if (pending) { pausePreview(); errorCode_.clear(); inputError_ = false; stage_.clear(); }
    refresh();
}
void EegSeparationWorkspace::setInputProgress(double seconds) {
    inputSeconds_ = seconds;
    refresh();
}
void EegSeparationWorkspace::setLiveAvailable(bool available) {
    if (liveAvailable_ == available) return;
    liveAvailable_ = available;
    refresh();
}
void EegSeparationWorkspace::setLiveActive(bool active) {
    liveActive_ = active;
    liveViewFrozen_ = false;
    latestLiveFrame_.reset();
    setProperty("liveViewPaused", false);
    if (active) { errorCode_.clear(); inputError_ = false; }
    if (!active) liveFrame_.reset();
    pausePreview(); refresh();
}
void EegSeparationWorkspace::setLiveFrame(std::shared_ptr<const LiveIcaFrame> frame) {
    if (!liveActive_) return;
    latestLiveFrame_ = std::move(frame);
    setProperty("liveLatestEndSeconds", latestLiveFrame_->end);
    if (liveViewFrozen_) return;
    liveFrame_ = latestLiveFrame_;
    setProperty("liveEndSeconds", liveFrame_->end);
    setProperty("liveHasGaps", liveFrame_->hasGaps);
    refresh();
}
bool EegSeparationWorkspace::viewControlsAvailable() const {
    return controller_->output() && !controller_->isBusy() && (!liveActive_ || bool(liveFrame_));
}
bool EegSeparationWorkspace::viewPaused() const {
    return liveActive_ ? liveViewFrozen_ : controller_->output() && !previewTimer_->isActive();
}
QString EegSeparationWorkspace::viewPauseText() const { return text(viewPaused() ? "view_resume" : "view_pause"); }
QString EegSeparationWorkspace::viewHint() const { return text("view_hint"); }
QString EegSeparationWorkspace::viewStatusText() const {
    return text(!controller_->output() ? "view_wait" : liveActive_ ? liveViewFrozen_ ? "view_frozen" : "live_running"
        : previewTimer_->isActive() ? "view_offline_playing" : "view_offline_paused",
        {{"seconds", i18n_->formatNumber(liveFrame_ ? liveFrame_->end : 0, 1)}});
}
void EegSeparationWorkspace::toggleViewPause() {
    if (!viewControlsAvailable()) return;
    if (!liveActive_) { togglePreview(); return; }
    liveViewFrozen_ = !liveViewFrozen_;
    if (liveViewFrozen_) previewOffset_ = groups_->offset;
    else { liveFrame_ = latestLiveFrame_; previewOffset_ = -1; }
    setProperty("liveViewPaused", liveViewFrozen_);
    if (liveFrame_) { setProperty("liveEndSeconds", liveFrame_->end); setProperty("liveHasGaps", liveFrame_->hasGaps); }
    refresh();
}
void EegSeparationWorkspace::showInputError(const AppError& error) {
    showError(error); inputError_ = true; refresh();
}
void EegSeparationWorkspace::applySettings() {
    if (refreshing_)
        return;
    auto settings = controller_->settings();
    settings.start = start_->value();
    settings.duration = duration_->value();
    settings.outputRate = rate_->currentData().toInt();
    settings.notchHz = notch_->currentData().toInt();
    errorCode_.clear();
    controller_->setSettings(settings);
    refresh();
}
void EegSeparationWorkspace::refreshSource() {
    const QScopedValueRollback<bool> guard(refreshing_, true);
    const auto &s = controller_->source();
    const auto &cfg = controller_->settings();
    source_->setText(s.path.isEmpty()
                         ? text("no_source")
                         : text("source_summary", {{"file", s.metadata.contains("signal_provenance")?text("selection_source"):QFileInfo(s.path).fileName()},
                                                   {"seconds", i18n_->formatNumber(s.duration(), 2)},
                                                   {"channels", s.channels.size()}}));
    source_->setToolTip(s.metadata.contains("signal_provenance")?text("selection_hint"):s.path);
    if (s.metadata.contains("signal_provenance")) {
        const auto origin = s.metadata.value("signal_provenance").toObject();
        QString name = origin.value("source_name").toString(text("selection_source"));
        if (origin.value("simulated").toBool()) name = text("simulated_source", {{"device", name}});
        source_->setText(text("source_summary", {{"file", name}, {"seconds", i18n_->formatNumber(s.duration(), 2)},
                                                 {"channels", s.channels.size()}}) + "\n" +
            text("snapshot_interval", {{"start", i18n_->formatNumber(origin.value("source_start_seconds").toDouble(), 2)},
                                       {"end", i18n_->formatNumber(origin.value("source_end_seconds").toDouble(), 2)},
                                       {"unit", s.metadata.value("unit").toString()}}));
    }
    start_->setValue(cfg.start);
    duration_->setValue(cfg.duration);
    rate_->setCurrentIndex(rate_->findData(cfg.outputRate));
    notch_->setCurrentIndex(notch_->findData(cfg.notchHz));
    channels_->setRowCount(s.channels.size());
    for (int r = 0; r < s.channels.size(); ++r) {
        auto *item = channels_->item(r, 0);
        if (!item) {
            item = new QTableWidgetItem;
            item->setFlags(item->flags() & ~Qt::ItemIsEditable);
            channels_->setItem(r, 0, item);
        }
        item->setText(s.channels[r]);
        item->setToolTip(s.channels[r]);
        auto *combo = qobject_cast<QComboBox *>(channels_->cellWidget(r, 1));
        if (!combo) {
            combo = new ChoiceBox(channels_);
            combo->setObjectName(QStringLiteral("separationRole%1").arg(r));
            combo->setSizeAdjustPolicy(QComboBox::AdjustToContents);
            for (int j = 0; j < 5; ++j)
                combo->addItem(QString());
            channels_->setCellWidget(r, 1, combo);
            connect(combo, &QComboBox::currentIndexChanged, this, [this, r](int role) {
                if (refreshing_)
                    return;
                auto cfg = controller_->settings();
                if (r < static_cast<int>(cfg.roles.size())) {
                    cfg.roles[r] = role;
                    refreshRoleHelp(qobject_cast<QComboBox*>(channels_->cellWidget(r, 1)));
                    controller_->setSettings(cfg);
                    refresh();
                }
            });
        }
        const QSignalBlocker blocker(combo);
        for (int j = 0; j < 5; ++j)
            combo->setItemText(j, text(qPrintable(QString("role_%1").arg(j))));
        combo->setCurrentIndex(r < static_cast<int>(cfg.roles.size()) ? cfg.roles[r] : 0);
        refreshRoleHelp(combo);
    }
    channels_->resizeColumnToContents(1);
    sourceDetails_->setText(text("acquisition_summary", {{"source", s.path.isEmpty() ? text("unknown") : source_->text().section('\n', 0, 0)},
        {"rate", s.fs > 0 ? i18n_->formatNumber(s.fs, 2) : text("unknown")}, {"channels", s.channels.size()},
        {"seconds", i18n_->formatNumber(s.duration(), 2)}, {"unit", s.metadata.value("unit").toString(text("unknown"))}}));
    const auto hardware = s.metadata.value("hardware").toObject();
    const auto known = [this, &hardware](const char *key) {
        const auto v = hardware.value(QString::fromLatin1(key));
        return v.isNull() || v.isUndefined() ? text("unknown")
               : v.isString()                ? v.toString()
                                             : i18n_->formatNumber(v.toDouble(), 2);
    };
    hardware_->setText(text("hardware", {{"reference", known("reference")},
                                         {"lower", known("lower_bandwidth_hz")},
                                         {"upper", known("upper_bandwidth_hz")}}));
    refresh();
}
void EegSeparationWorkspace::refreshComponents() {
    const QScopedValueRollback<bool> guard(refreshing_, true);
    const int selected = components_->currentRow();
    const auto output = controller_->output();
    components_->setRowCount(output ? int(output->sources.size()) : 0);
    for (int r = 0; r < components_->rowCount(); ++r) {
        for (int col = 0; col < 2; ++col)
            if (!components_->item(r, col)) components_->setItem(r, col, new QTableWidgetItem);
        components_->item(r, 0)->setText(QString("IC %1").arg(r + 1));
        const int group = controller_->labels().at(r);
        components_->item(r, 1)->setText(text(qPrintable(QString("group_%1").arg(group))));
        components_->item(r, 1)->setToolTip(text(qPrintable(QString("group_help_%1").arg(group))));
    }
    components_->setFixedHeight(std::clamp(components_->horizontalHeader()->height() + components_->rowHeight(0) * std::max(1, components_->rowCount()) + 4, 75, 140));
    if (components_->rowCount()) components_->setCurrentCell(std::clamp(selected, 0, components_->rowCount() - 1), 0);
    refreshComponentSelection(); refreshPlots();
}
void EegSeparationWorkspace::refresh() {
    const bool busy = controller_->isBusy() || inputPending_, result = bool(controller_->output()),
               hasSource = !controller_->source().path.isEmpty();
    reviewHint_->setText(text(result ? "review_hint" : "review_empty"));
    reviewConfirmHint_->setVisible(result);
    const auto state = controller_->state();
    if (busy || !result)
        pausePreview();
    if (!result) { previewPosition_ = 0; previewOffset_ = -1; }
    groups_->setCursor(result && (!liveActive_ || liveViewFrozen_) ? Qt::OpenHandCursor : Qt::ArrowCursor);
    exampleLabels_->setVisible((controller_->source().teaching || controller_->source().simulatorIca) && result);
    exampleLabels_->setEnabled(!busy && controller_->canApplyTeachingLabels());
    teachingHelp_->setVisible(controller_->source().teaching || controller_->source().simulatorIca);
    teachingHelp_->setText(text(controller_->source().simulatorIca ? "simulator_notice" : "teaching_notice"));
    const int step = liveActive_ || (result && controller_->isConfirmed()) ? 3
                   : result ? 2 : hasSource ? 1 : 0;
    for (int i = 0; i < 4; ++i) {
        workflowSteps_[i]->setText(text(qPrintable(QString("workflow_step_%1").arg(i + 1))));
        auto font = workflowSteps_[i]->font(); font.setBold(i == step); workflowSteps_[i]->setFont(font);
        if (workflowSteps_[i]->property("currentStep").toBool() != (i == step)) {
            workflowSteps_[i]->setProperty("currentStep", i == step);
            workflowSteps_[i]->style()->unpolish(workflowSteps_[i]);
            workflowSteps_[i]->style()->polish(workflowSteps_[i]);
            workflowSteps_[i]->update();
        }
    }
    setProperty("workflowStep", step + 1);
    const bool showLive = result && (deviceAvailable_ || liveActive_) && (controller_->isConfirmed() || liveActive_);
    liveToggle_->setVisible(showLive);
    liveControls_->setVisible(showLive && errorCode_.isEmpty());
    deviceInput_->setText(text(hasSource ? "retake_calibration" : "prepare_calibration"));
    liveToggle_->setText(text(liveActive_ ? "live_stop" : "live_start"));
    liveToggle_->setEnabled(liveActive_ || (!busy && liveAvailable_ && result && controller_->isConfirmed()));
    liveToggle_->setToolTip(text("live_help"));
    const bool liveWarning = !liveActive_ && deviceAvailable_ && !liveAvailable_;
    const bool liveGaps = liveActive_ && liveFrame_ && liveFrame_->hasGaps;
    auto liveMessage = text(liveActive_ ? liveViewFrozen_ ? "view_frozen" : (liveGaps ? "live_gaps" : "live_running")
                                : !deviceAvailable_ ? "live_connect"
                                : !result ? "live_prepare"
                                : !liveAvailable_ ? "live_incompatible"
                                : !controller_->isConfirmed() ? "live_review" : "live_ready",
                                {{"seconds", i18n_->formatNumber(liveFrame_ ? liveFrame_->end : 0, 1)}});
    liveStatus_->setText(liveWarning || liveGaps ? text("feedback_warning", {{"message", liveMessage}}) : liveMessage);
    setFeedbackTone(liveStatus_, liveWarning || liveGaps ? "warning" : "info");
    liveStatus_->setAccessibleDescription(liveStatus_->text());
    allObservations_->setText(text("all_observations"));
    allObservations_->setToolTip(text("all_observations_help"));
    allObservations_->setVisible(hasSource);
    allObservations_->setEnabled(!busy && !liveActive_ && controller_->source().channels.size() <= 32);
    deviceInput_->setEnabled(!busy && !liveActive_ && deviceAvailable_);
    deviceStream_->setEnabled(!busy && !liveActive_);
    channels_->setEnabled(!busy && !liveActive_);
    refreshBulkActions();
    amplitudeMode_->setEnabled(result);
    amplitudeZoom_->setEnabled(result);
    start_->setEnabled(!busy && !liveActive_ && hasSource);
    duration_->setEnabled(!busy && !liveActive_ && hasSource);
    advanced_->setEnabled(!busy && !liveActive_ && hasSource);
    components_->setEnabled(!busy);
    refreshComponentSelection();
    undo_->setEnabled(!busy && controller_->canUndoLabel());
    confirmed_->setEnabled(result && !busy);
    {
        const QSignalBlocker blocker(confirmed_);
        confirmed_->setChecked(controller_->isConfirmed());
    }
    main_->setText(text(result ? "export" : "fit_model"));
    rerun_->setVisible(result);
    cancel_->setVisible(busy);
    cancel_->setEnabled(state != EegSeparationController::State::Exporting || stage_ != QStringLiteral("publishing"));
    retry_->setVisible(!busy && !inputError_ && errorRetryable_ && !errorCode_.isEmpty() && errorCode_ != QStringLiteral("analysis.cancelled"));
    retry_->setEnabled(!result || controller_->isConfirmed());
    retry_->setToolTip(text(result && !controller_->isConfirmed() ? "confirmation_help" : "retry"));
    int observations = 0, unconfirmed = 0, selectedChannels = 0;
    for (int role : controller_->settings().roles) {
        observations += role == 1;
        unconfirmed += role == 0;
        selectedChannels += role >= 1 && role <= 3;
    }
    const auto &cfg = controller_->settings();
    const bool rateValid = controller_->source().fs >= 250 && cfg.outputRate <= controller_->source().fs;
    const bool valid = hasSource && rateValid && selectedChannels <= 40 && observations >= 2 &&
                       observations <= 32 && !unconfirmed && cfg.duration >= 5 && cfg.duration <= 120 && cfg.start >= 0 &&
                       cfg.start + cfg.duration <= controller_->source().duration() + 1e-8;
    main_->setEnabled(!busy && !liveActive_ && (result ? controller_->isConfirmed() : valid));
    rerun_->setEnabled(!busy && !liveActive_ && result && valid);
    hint_->setText(text(inputPending_           ? "workflow_wait"
                        : busy                  ? "next_busy"
                        : !hasSource && deviceAvailable_ ? "workflow_prepare"
                        : !hasSource            ? "workflow_empty"
                        : result && controller_->isConfirmed() && liveAvailable_ ? "workflow_live"
                        : result && controller_->isConfirmed() ? "next_export"
                        : result                ? "workflow_review"
                        : !rateValid            ? "next_rate"
                        : selectedChannels > 40 ? "next_selected"
                        : unconfirmed           ? "next_roles"
                        : observations < 2 || observations > 32 ? "next_observations"
                        : !valid                ? "next_range"
                                                : "workflow_fit"));
    const bool validationWarning = hasSource && !result && !busy && !valid;
    if (validationWarning) hint_->setText(text("feedback_warning", {{"message", hint_->text()}}));
    setFeedbackTone(hint_, validationWarning ? "warning" : "info");
    hint_->setAccessibleDescription(hint_->text());
    main_->setToolTip(result ? text(controller_->isConfirmed() ? "export_notice" : "confirmation_help")
                            : hint_->text());
    status_->setToolTip(QString());
    if (!errorCode_.isEmpty()) {
        const QString key = QStringLiteral("error.") + errorCode_;
        QString explanation = i18n_->text(key, errorParams_);
        if (explanation == key)
            explanation = text("error_generic", {{"code", errorCode_}});
        const bool cancelled = errorCode_ == QStringLiteral("analysis.cancelled");
        const auto summary = explanation.section(QStringLiteral("\n\n"), 0, 0);
        status_->setText(cancelled ? summary : text("feedback_error", {{"message", summary}}));
        status_->setToolTip(explanation);
        setFeedbackTone(status_, cancelled ? "info" : "error");
    } else if (busy) {
        status_->setText(inputPending_ && inputSeconds_ >= 0
                            ? text("live_collecting", {{"seconds", i18n_->formatNumber(inputSeconds_, 1)}})
                            : text(inputPending_                                          ? "progress_snapshot"
                              : stage_ == QStringLiteral("publishing")                ? "progress_publishing"
                              : state == EegSeparationController::State::Inspecting ? "progress_inspecting"
                              : state == EegSeparationController::State::Exporting  ? "progress_exporting"
                                                                                    : "progress_running"));
        setFeedbackTone(status_, "info");
    } else if (!controller_->exportedPath().isEmpty()) {
        status_->setText(text("exported", {{"path", controller_->exportedPath()}}));
        setFeedbackTone(status_, "success");
    } else {
        status_->clear();
        setFeedbackTone(status_, "info");
    }
    status_->setAccessibleDescription(status_->text());
    status_->setVisible(!status_->text().isEmpty());
    hint_->setVisible(!busy && !liveActive_ &&
                     errorCode_.isEmpty() && controller_->exportedPath().isEmpty());
    // One emphasized action at a time; all workflow actions stay in one bar.
    QPushButton* primary = busy ? (cancel_->isEnabled() ? cancel_ : nullptr)
        : liveActive_ ? liveToggle_
        : !retry_->isHidden() && retry_->isEnabled() ? retry_
        : !hasSource && deviceInput_->isEnabled() ? deviceInput_
        : main_->isEnabled() ? main_ : nullptr;
    for (auto* button : {main_, rerun_, liveToggle_, retry_, cancel_, deviceInput_}) {
        button->setProperty("primaryAction", button == primary);
        static_cast<ActionButton*>(button)->setFluentStyle(button == primary ? ActionButton::Accent : ActionButton::Standard);
    }
    main_->setVisible(retry_->isHidden());
    refreshPlots();
    refreshPlaybackControls();
    if (inputActionsChanged) inputActionsChanged();
}
double EegSeparationWorkspace::previewDuration() const {
    const auto output = controller_->output();
    if (!output || output->analysis.empty() || output->fs <= 0)
        return 0.;
    const double resampledDuration = output->analysis[0].size() / output->fs;
    const double sourceRate = output->manifest.value("source_rate").toDouble();
    const double sourceSamples = output->manifest.value("sample_count").toDouble();
    // The final resampled sample may cover only part of an output-rate period.
    return sourceRate > 0 && sourceSamples > 0
               ? std::min(resampledDuration, sourceSamples / sourceRate) : resampledDuration;
}
void EegSeparationWorkspace::refreshPlaybackControls() {
    const bool available = controller_->output() && !controller_->isBusy() && !liveActive_;
    play_->setEnabled(available);
    restart_->setEnabled(available);
    scrub_->setEnabled(available);
    for (QWidget* widget : {static_cast<QWidget*>(play_), static_cast<QWidget*>(restart_),
                           static_cast<QWidget*>(scrub_), static_cast<QWidget*>(playbackPosition_)})
        widget->setVisible(!liveActive_);
    play_->setText(text(previewTimer_->isActive() ? "pause" : "play"));
    play_->setToolTip(text(previewPosition_ >= previewDuration() && available
                              ? "playback_end" : "playback_help"));
    const auto formatTime = [](double value) {
        const auto tenths = static_cast<int>(std::floor(std::max(0., value) * 10 + 1e-6));
        return QStringLiteral("%1:%2.%3").arg(tenths / 600, 2, 10, QChar('0'))
            .arg(tenths / 10 % 60, 2, 10, QChar('0')).arg(tenths % 10);
    };
    playbackPosition_->setText(text("playback_position", {{"current", formatTime(previewPosition_)},
                                                        {"total", formatTime(previewDuration())}}));
    setProperty("previewPositionSeconds", previewPosition_);
    setProperty("previewPlaying", previewTimer_->isActive());
}
void EegSeparationWorkspace::setPreviewPosition(double seconds) {
    previewOffset_ = -1;
    previewPosition_ = std::clamp(seconds, 0., previewDuration());
    const QSignalBlocker b(scrub_);
    scrub_->setValue(previewDuration() > 0 ? qRound(1000 * previewPosition_ / previewDuration()) : 0);
    refreshPlots();
    refreshPlaybackControls();
}
void EegSeparationWorkspace::togglePreview() {
    if (previewTimer_->isActive()) {
        pausePreview();
        return;
    }
    if (!isVisible() || controller_->isBusy() || previewDuration() <= 0)
        return;
    if (previewPosition_ >= previewDuration() - 1e-9)
        setPreviewPosition(0);
    previewOffset_ = -1;
    playbackAnchor_ = previewPosition_;
    previewClock_.start();
    previewTimer_->start();
    refreshPlaybackControls();
}
void EegSeparationWorkspace::pausePreview() {
    if (previewTimer_->isActive()) {
        const double seconds = playbackAnchor_ + previewClock_.nsecsElapsed() / 1e9;
        previewTimer_->stop();
        setPreviewPosition(seconds);
    } else
        refreshPlaybackControls();
}
void EegSeparationWorkspace::hideEvent(QHideEvent *event) {
    pausePreview();
    draggingGroups_ = false;
    helpDialog_->hide();
    QWidget::hideEvent(event);
}
void EegSeparationWorkspace::arrangePreviewControls() {
    if (!previewControls_ || !amplitudeControls_) return;
    const bool compact = width() < 1100;
    previewControls_->addWidget(previewChannelLabel_, 0, 0);
    previewControls_->addWidget(channel_, 0, 1);
    previewControls_->addWidget(displayWindowLabel_, compact ? 1 : 0, compact ? 0 : 2);
    previewControls_->addWidget(window_, compact ? 1 : 0, compact ? 1 : 3);
    amplitudeControls_->addWidget(amplitudeModeLabel_, 0, 0);
    amplitudeControls_->addWidget(amplitudeMode_, 0, 1);
    amplitudeControls_->addWidget(amplitudeZoomLabel_, compact ? 1 : 0, compact ? 0 : 2);
    amplitudeControls_->addWidget(amplitudeZoom_, compact ? 1 : 0, compact ? 1 : 3);
    for (auto* layout : {previewControls_, amplitudeControls_}) {
        layout->setColumnStretch(1, 1);
        layout->setColumnStretch(3, compact ? 0 : 1);
    }
}
void EegSeparationWorkspace::resizeEvent(QResizeEvent* event) {
    QWidget::resizeEvent(event);
    arrangePreviewControls();
}
void EegSeparationWorkspace::refreshBulkActions() {
    const auto count = channels_->selectionModel()->selectedRows().size();
    const bool available = count >= 2 && !controller_->source().path.isEmpty();
    const bool editable = available && !controller_->isBusy() && !inputPending_ && !liveActive_;
    bulkActions_->setVisible(available);
    bulkRole_->setEnabled(editable);
    bulk_->setEnabled(editable);
    bulkSelection_->setText(text("bulk_selection", {{"count", static_cast<int>(count)}}));
    bulk_->setText(text("bulk"));
    bulk_->setAccessibleName(bulk_->text() + QStringLiteral(" · ") + bulkSelection_->text());
    bulk_->setToolTip(bulkSelection_->text());
}
void EegSeparationWorkspace::refreshPlots() {
    // Input can change units without a language change (for example a voltage
    // file followed by uncalibrated RT-BCI counts).
    groups_->peakToPeakMessage = text("peak_to_peak", {{"value", QStringLiteral("%1")}});
    groups_->sharedAmplitude = amplitudeMode_->currentIndex() == 0;
    groups_->amplitudeZoom = amplitudeZoom_->currentData().toDouble();
    amplitudeHelp_->setText(text(groups_->sharedAmplitude ? "amplitude_help_shared"
                                                        : "amplitude_help_individual"));
    amplitudeHelp_->setVisible(!groups_->sharedAmplitude);
    amplitudeMode_->setToolTip(amplitudeHelp_->text());
    const auto output = liveActive_ && liveFrame_
        ? std::shared_ptr<const SeparationOutput>(liveFrame_, &liveFrame_->signal) : controller_->output();
    if (!output) {
        spectrumOutput_.reset();
        spectrumComponent_ = -1;
        groups_->traces.clear();
        groups_->refreshMetrics();
        componentTrace_->traces.clear();
        componentTrace_->setProperty("selectedComponent", -1);
        componentTrace_->setAccessibleName(text("component_trace"));
        spectrum_->traces.clear();
        clues_->setText(text("clues_empty"));
        channel_->clear();
        groups_->update();
        componentTrace_->update();
        spectrum_->update();
        return;
    }
    if (channel_->count() != output->channels.size()) {
        const QSignalBlocker b(channel_);
        channel_->clear();
        channel_->addItems(output->channels);
    }
    const int ch = std::max(0, channel_->currentIndex());
    const double view = std::min(window_->currentData().toDouble(),
                                 output->analysis.empty() ? 0 : output->analysis[0].size() / output->fs);
    const double full = output->analysis.empty() ? 0 : output->analysis[0].size() / output->fs;
    const double offset = liveActive_ && !liveViewFrozen_ ? std::max(0., full - view)
                                     : std::clamp(previewOffset_ >= 0 ? previewOffset_ : previewPosition_ - view / 2, 0., std::max(0., full - view));
    groups_->xAxisLabel = componentTrace_->xAxisLabel = text("axis_time");
    groups_->yAxisUnit = controller_->source().metadata.value("unit").toString("uV");
    groups_->yAxisLabel = text("axis_amplitude", {{"unit", groups_->yAxisUnit}});
    componentTrace_->yAxisUnit = QStringLiteral("a.u.");
    componentTrace_->yAxisLabel = text("axis_amplitude", {{"unit", componentTrace_->yAxisUnit}});
    spectrum_->xAxisLabel = text("axis_frequency");
    spectrum_->yAxisUnit = QStringLiteral("a.u.²/Hz");
    spectrum_->yAxisLabel = text("axis_psd");
    groups_->fs = output->fs;
    groups_->seconds = view;
    groups_->offset = offset;
    groups_->start = output->start;
    groups_->cursor = liveActive_ ? -1 : previewPosition_;
    groups_->traces.clear();
    groups_->names.clear();
    groups_->emptyGroups.clear();
    if (ch < static_cast<int>(output->analysis.size())) {
        groups_->traces.push_back(output->analysis[ch]);
        if (liveActive_ && liveFrame_) {
            // Display the centered input beside its zero-mean contributions.
            // A large ADC DC offset must not flatten every live group. This is
            // a display copy; the raw samples, model and recorder are intact.
            for (double& value : groups_->traces.back()) value -= output->mean[ch];
        }
        const auto channelName = output->channels.value(ch);
        const auto unit = controller_->source().metadata.value("unit").toString("uV");
        groups_->names << text(liveActive_ && liveFrame_ ? "live_input_channel_trace" : "input_channel_trace", {{"channel", channelName}, {"unit", unit}});
        groups_->emptyGroups.push_back(false);
        for (int g = 0; g < 4; ++g) {
            const auto &projection = liveActive_ && liveFrame_ ? liveFrame_->groups[g] : controller_->projection(g);
            if (liveActive_ && liveViewFrozen_ && liveFrame_ && controller_->output() &&
                ch < int(controller_->output()->mixing.size())) {
                // Reclassifying frozen components changes grouping while keeping
                // the same immutable source samples and model matrices.
                auto contribution = output->analysis[ch];
                for (auto& value : contribution) if (std::isfinite(value)) value = 0;
                const auto& mixing = controller_->output()->mixing[ch];
                for (int k = 0; k < int(output->sources.size()); ++k) {
                    if (k >= int(mixing.size()) || k >= int(controller_->labels().size()) || controller_->labels()[k] != g) continue;
                    for (size_t sample = 0; sample < contribution.size(); ++sample)
                        contribution[sample] += mixing[k] * output->sources[k][sample];
                }
                groups_->traces.push_back(std::move(contribution));
            } else if (ch < static_cast<int>(projection.size()))
                groups_->traces.push_back(projection[ch]);
            else
                groups_->traces.push_back({});
            QStringList contributors;
            for (int k = 0; k < int(controller_->labels().size()); ++k)
                if (controller_->labels()[k] == g) contributors << QString("IC %1").arg(k + 1);
            groups_->names << text("group_channel_trace", {{"group", text(qPrintable(QString("group_%1").arg(g)))},
                {"channel", channelName}, {"components", contributors.isEmpty() ? text("empty_group") : text("component_contribution", {{"components", contributors.join(QStringLiteral(", "))}})}, {"unit", unit}});
            groups_->emptyGroups.push_back(std::find(controller_->labels().begin(),
                                                     controller_->labels().end(),
                                                     g) == controller_->labels().end());
        }
    }
    groups_->setProperty("groupDescriptions", groups_->names);
    groups_->refreshMetrics();
    const int component = components_->currentRow();
    componentTrace_->traces.clear();
    if (component >= 0 && component < static_cast<int>(output->sources.size())) {
        componentTrace_->fs = output->fs;
        componentTrace_->start = output->start;
        componentTrace_->seconds = view;
        componentTrace_->offset = offset;
        componentTrace_->cursor = liveActive_ ? -1 : previewPosition_;
        componentTrace_->traces = {output->sources[component]};
        componentTrace_->names = {text("component_trace_selected", {{"component", component + 1}})};
        componentTrace_->setAccessibleName(text("component_trace_selected", {{"component", component + 1}}));
        componentTrace_->setProperty("selectedComponent", component);
        componentTrace_->refreshMetrics();
        spectrum_->names = {text(liveActive_ ? "live_spectrum" : "spectrum")};
        if (spectrumOutput_ != output || spectrumComponent_ != component) {
            spectrumOutput_ = output;
            spectrumComponent_ = component;
            // A live gap is a boundary, never zero-filled or passed to Welch.
            const auto& trace = output->sources[component];
            auto begin = trace.begin(), end = trace.begin();
            for (auto it = trace.begin(); it != trace.end();) {
                if (!std::isfinite(*it)) { ++it; continue; }
                const auto first = it;
                while (it != trace.end() && std::isfinite(*it)) ++it;
                if (it - first >= end - begin) { begin = first; end = it; }
            }
            const auto psd = end - begin >= 4
                ? dsp::computeWelchPsd(std::vector<double>(begin, end), output->fs, 0, false) : dsp::Spectrum{};
            std::vector<double> values;
            double maxFrequency = 0;
            for (std::size_t i = 0; i < psd.frequencies.size(); ++i)
                if (psd.frequencies[i] <= std::min(100., output->fs / 2)) {
                    values.push_back(psd.values[i]);
                    maxFrequency = psd.frequencies[i];
                }
            spectrum_->traces = {std::move(values)};
            spectrum_->frequency = true;
            spectrum_->seconds = maxFrequency;
            spectrum_->offset = 0;
            spectrum_->start = 0;
        }
        spectrum_->refreshMetrics();
        if (component < static_cast<int>(output->scores.size())) {
            const auto &s = output->scores[component];
            clues_->setText(text("clues", {{"eye", (std::find(controller_->settings().roles.begin(),
                                                              controller_->settings().roles.end(),
                                                              2) == controller_->settings().roles.end()
                                                        ? text("no_reference")
                                                        : i18n_->formatNumber(s.eyeCorrelation, 2))},
                                           {"muscle", (std::find(controller_->settings().roles.begin(),
                                                                 controller_->settings().roles.end(),
                                                                 3) == controller_->settings().roles.end()
                                                           ? text("no_reference")
                                                           : i18n_->formatNumber(s.muscleCorrelation, 2))},
                                           {"high", i18n_->formatNumber(s.highFrequencyRatio * 100, 1)}}));
        } else
            clues_->setText(text(liveActive_ ? "live_scores" : "clues_empty"));
    }
    groups_->update();
    componentTrace_->update();
    spectrum_->update();
}
void EegSeparationWorkspace::exportResults() {
    if (controller_->isBusy() || !controller_->output() || !controller_->isConfirmed())
        return;
    pausePreview();
    const auto base = controller_->exportedPath().isEmpty()
                      && controller_->source().metadata.contains("signal_provenance")
                          ? QDir(QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation)).filePath("BoundFree-separation")
                      : controller_->exportedPath().isEmpty()
                          ? QFileInfo(controller_->source().path).dir().filePath(
                                QFileInfo(controller_->source().path).completeBaseName() +
                                QStringLiteral("-separation"))
                          : controller_->exportedPath();
    auto initial = base;
    // QFileDialog enters an existing directory instead of accepting it as a
    // save name. Offer a fresh folder on every export, including repeat clicks.
    for (int suffix = 2; QFileInfo::exists(initial); ++suffix)
        initial = base + QStringLiteral("-%1").arg(suffix);
    const auto destination = QFileDialog::getSaveFileName(this, text("new_directory"), initial, {}, nullptr,
                                                          QFileDialog::DontConfirmOverwrite);
    if (destination.isEmpty())
        return;
    if (QFileInfo::exists(destination)) {
        showError(makeError("analysis.destination_exists", {{"path", destination}}, true));
        return;
    }
    controller_->exportBundle(destination);
}
void EegSeparationWorkspace::showError(const AppError &error) {
    pausePreview();
    inputError_ = false;
    errorCode_ = error.code;
    errorParams_ = error.params;
    errorRetryable_ = error.retryable;
    refresh();
}
void EegSeparationWorkspace::setDarkTheme(bool dark) {
    fluent::FluentElement::setTheme(dark ? fluent::FluentElement::Dark : fluent::FluentElement::Light);
    const auto tone = surfaceColors();
    QPalette colors = palette();
    colors.setColor(QPalette::Window, tone.bgCanvas);
    colors.setColor(QPalette::Base, tone.controlDefault);
    colors.setColor(QPalette::AlternateBase, tone.bgLayer);
    colors.setColor(QPalette::Button, tone.controlDefault);
    for (auto role : {QPalette::WindowText, QPalette::Text, QPalette::ButtonText}) {
        colors.setColor(role, tone.textPrimary);
        colors.setColor(QPalette::Disabled, role, tone.textDisabled);
    }
    colors.setColor(QPalette::PlaceholderText, tone.textSecondary);
    colors.setColor(QPalette::Highlight, tone.accentDefault);
    colors.setColor(QPalette::HighlightedText, tone.textOnAccent);
    colors.setColor(QPalette::Mid, tone.strokeDefault);
    setPalette(colors);
    for (auto* child : findChildren<QWidget*>()) {
        child->setAutoFillBackground(false);
        child->setPalette(colors);
    }
    setAutoFillBackground(true);
    setStyleSheet(separationStyle());
    // Scroll viewports and headers can retain their old palette when Qt
    // rematerializes it during stylesheet polish, especially in standalone views.
    setPalette(colors);
    for (auto* child : findChildren<QWidget*>()) child->setPalette(colors);
    // Cell editors retain their own text color; use the same neutral selection
    // surface as the table stylesheet so selected editors remain legible.
    auto tableColors = colors;
    tableColors.setColor(QPalette::Highlight, tone.subtleSecondary);
    tableColors.setColor(QPalette::HighlightedText, tone.textPrimary);
    for (auto* table : {channels_, components_}) {
        table->setPalette(tableColors);
        table->viewport()->setPalette(tableColors);
    }
    for (auto *plot : {groups_, componentTrace_, spectrum_}) {
        plot->dark = dark;
        plot->update();
    }
}
void EegSeparationWorkspace::retranslateUi() {
    sourceTitle_->setText(text("active_input"));
    deviceInput_->setText(text("device_snapshot"));
    deviceInput_->setToolTip(text("device_snapshot_help"));
    deviceStream_->setAccessibleName(text("device_stream"));
    deviceInputStatus_->setText(text(deviceAvailable_ ? "device_available" : "device_unavailable", {{"device", deviceName_}}));
    exampleLabels_->setText(text("example_labels_compact"));
    teachingHelp_->setText(text("teaching_notice"));
    title_->setText(text("title"));
    subtitle_->setText(text("guide_steps"));
    helpButton_->setText(text("help_open"));
    helpButton_->setToolTip(text("help_title") + QStringLiteral(" · F1"));
    metadataButton_->setText(text("metadata_open"));
    helpClose_->setText(text("help_close"));
    helpDialog_->setWindowTitle(text("help_title"));
    for (int tab = 0; tab < 5; ++tab)
        helpTabs_->setTabText(tab, text(qPrintable(QString("help_tab_%1").arg(tab))));
    shortcutsHelp_->setText(text("view_shortcuts") + QStringLiteral("\n\n") + text("component_plot_controls"));
    QStringList review{text("review_steps")};
    for (int group = 0; group < 4; ++group)
        review << text(qPrintable(QString("group_%1").arg(group))) + QStringLiteral("\n") + text(qPrintable(QString("group_help_%1").arg(group)));
    review << text("review_finish");
    reviewHelp_->setText(review.join(QStringLiteral("\n\n")));
    reviewConfirmHint_->setText(text("review_confirm_hint"));
    groups_->controlsHelp = text("view_hint");
    rangeLabel_->setText(text("range"));
    start_->setToolTip(text("start_time"));
    start_->setAccessibleName(text("start_time"));
    duration_->setToolTip(text("duration"));
    duration_->setAccessibleName(text("duration"));
    QStringList descriptions{text("roles_intro")};
    for (int role = 0; role < 5; ++role)
        descriptions << text(qPrintable(QString("role_%1").arg(role))) + QStringLiteral("\n") + text(qPrintable(QString("role_help_%1").arg(role)));
    rolesHelp_->setText(descriptions.join(QStringLiteral("\n\n")));
    refreshRoleHelp(bulkRole_);
    channels_->setHorizontalHeaderLabels({text("channel"), text("role")});
    components_->setHorizontalHeaderLabels({text("component"), text("source")});
    bulkRole_->setAccessibleName(text("bulk_role"));
    {
        const QSignalBlocker b(bulkRole_);
        int role = bulkRole_->currentIndex();
        bulkRole_->clear();
        for (int i = 0; i < 5; ++i)
            bulkRole_->addItem(text(qPrintable(QString("role_%1").arg(i))));
        bulkRole_->setCurrentIndex(std::max(0, role));
    }
    advanced_->setTitle(text("advanced"));

    notch_->setItemText(0, text("notch_off"));
    notch_->setAccessibleName(text("notch"));
    rate_->setAccessibleName(text("output_rate"));
    previewTitle_->setText(text("preview_title"));
    amplitudeModeLabel_->setText(text("amplitude_mode"));
    amplitudeZoomLabel_->setText(text("waveform_zoom"));
    amplitudeMode_->setAccessibleName(text("amplitude_mode"));
    amplitudeZoom_->setAccessibleName(text("waveform_zoom"));
    for (int i = 0; i < amplitudeZoom_->count(); ++i)
        amplitudeZoom_->setItemText(i, text("zoom_factor", {{"value", i18n_->formatNumber(amplitudeZoom_->itemData(i).toDouble(), std::abs(amplitudeZoom_->itemData(i).toDouble() - std::round(amplitudeZoom_->itemData(i).toDouble())) < 1e-9 ? 0 : 2)}}));
    for (int i = 0; i < window_->count(); ++i)
        window_->setItemText(i, text("window_value", {{"seconds", i18n_->formatNumber(window_->itemData(i).toDouble(), std::abs(window_->itemData(i).toDouble() - std::round(window_->itemData(i).toDouble())) < 1e-9 ? 0 : 2)}}));
    previewChannelLabel_->setText(text("preview_channel"));
    displayWindowLabel_->setText(text("display_window_visible"));
    restart_->setText(text("restart"));
    {
        const QSignalBlocker block(amplitudeMode_);
        amplitudeMode_->setItemText(0, text("amplitude_shared"));
        amplitudeMode_->setItemText(1, text("amplitude_individual"));
    }
    groups_->peakToPeakMessage = text("peak_to_peak", {{"value", QStringLiteral("%1")}});
    groups_->smallWaveMessage = text("small_wave");
    groups_->clippedMessage = text("clipped_wave");
    if (auto *splitter = findChild<QSplitter *>("separationSplitter"))
        for (int i = 1; i < splitter->count(); ++i) {
            splitter->handle(i)->setToolTip(text("splitter_help"));
            splitter->handle(i)->setAccessibleName(text("splitter_help"));
        }
    componentTrace_->controlsHelp = text("component_plot_controls");
    componentTrace_->setToolTip(componentTrace_->controlsHelp);
    channel_->setAccessibleName(text("preview_channel"));
    window_->setAccessibleName(text("display_window_visible"));
    scrub_->setAccessibleName(text("preview_position"));
    evidenceTitle_->setText(text("evidence_title"));
    for (int tab = 0; tab < 3; ++tab) componentViews_->setTabText(tab, text(qPrintable(QString("component_view_%1").arg(tab))));
    previousComponent_->setText(text("previous_component"));
    nextComponent_->setText(text("next_component"));
    for (int group = 0; group < 4; ++group) {
        auto* button = classificationButtons_[group];
        button->setText(text(qPrintable(QString("group_%1").arg(group))));
        button->setToolTip(text(qPrintable(QString("group_help_%1").arg(group))));
        button->setAccessibleDescription(button->toolTip());
    }
    undo_->setText(text("undo_compact"));
    undo_->setToolTip(text("undo"));
    sourceCaption_->setText(text("source_caption"));
    confirmed_->setText(text("confirmed"));
    confirmed_->setToolTip(text("confirmation_help") + QStringLiteral("\n") + text("review_notice"));
    rerun_->setText(text("rerun"));
    rerun_->setToolTip(text("rerun_help"));
    exampleLabels_->setToolTip(text("example_help"));
    cancel_->setText(text("cancel"));
    retry_->setText(text("retry"));
    for (auto *plot : {groups_, componentTrace_, spectrum_}) {
        plot->locale = i18n_->qlocale();
        plot->noData = text("no_result");
        plot->emptyMessage = text("empty_group");
        plot->cursorMessage = text("cursor_time", {{"time", QStringLiteral("%1")}});
    }
    refreshSource();
    refreshComponents();
    refresh();
    refreshRoleHelp(bulkRole_);
}
} // namespace bf
