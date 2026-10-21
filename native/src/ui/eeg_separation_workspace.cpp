#include "ui/eeg_separation_workspace.h"
#include "dsp/spectral.h"
#include "ui/collapsible_panel.h"
#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QDoubleSpinBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QHeaderView>
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
#include <QSplitter>
#include <QSplitterHandle>
#include <QTableWidget>
#include <QTimer>
#include <QVBoxLayout>
#include <QVariantList>
#include <algorithm>
#include <cmath>
namespace bf {
// Keep resizing discoverable in the idle state, as well as on hover.
class SeparationSplitterHandle final : public QSplitterHandle {
  public:
    SeparationSplitterHandle(Qt::Orientation orientation, QSplitter *parent)
        : QSplitterHandle(orientation, parent) {
        setCursor(Qt::SplitHCursor);
        setFocusPolicy(Qt::StrongFocus);
        setAttribute(Qt::WA_Hover);
    }
  protected:
    void paintEvent(QPaintEvent *) override {
        QPainter p(this);
        const auto colors = palette();
        p.fillRect(rect(), colors.color(underMouse() || hasFocus() ? QPalette::Midlight : QPalette::Window));
        const int x = width() / 2, y = height() / 2;
        p.setPen(QPen(colors.color(hasFocus() ? QPalette::Highlight : QPalette::Mid), 1));
        p.drawLine(x, 6, x, height() - 6);
        p.setBrush(colors.color(QPalette::Button));
        p.drawRoundedRect(QRectF(1, y - 25, width() - 2, 50), 3, 3);
        p.setBrush(colors.color(QPalette::Text));
        p.setPen(Qt::NoPen);
        for (int dy : {-8, 0, 8})
            p.drawEllipse(QPointF(x, y + dy), 1.5, 1.5);
    }
    bool event(QEvent *event) override {
        if (event->type() == QEvent::HoverEnter || event->type() == QEvent::HoverLeave ||
            event->type() == QEvent::FocusIn || event->type() == QEvent::FocusOut)
            update();
        return QSplitterHandle::event(event);
    }
    void keyPressEvent(QKeyEvent *event) override {
        if (event->key() == Qt::Key_Left || event->key() == Qt::Key_Right) {
            auto *owner = splitter();
            for (int i = 1; i < owner->count(); ++i)
                if (owner->handle(i) == this) {
                    moveSplitter(pos().x() + (event->key() == Qt::Key_Left ? -20 : 20));
                    break;
                }
            event->accept();
        } else
            QSplitterHandle::keyPressEvent(event);
    }
};
class SeparationSplitter final : public QSplitter {
  public:
    explicit SeparationSplitter(QWidget *parent) : QSplitter(Qt::Horizontal, parent) {
        setObjectName("separationSplitter");
        setHandleWidth(12);
        setChildrenCollapsible(false);
    }
  protected:
    QSplitterHandle *createHandle() override {
        return new SeparationSplitterHandle(orientation(), this);
    }
};
class SeparationTraceView final : public QWidget {
  public:
    explicit SeparationTraceView(QWidget *parent = nullptr) : QWidget(parent) {
        setMinimumSize(160, 120);
        setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    }
    std::vector<std::vector<double>> traces;
    QStringList names;
    std::vector<bool> emptyGroups;
    QString emptyMessage, noData, peakToPeakMessage, smallWaveMessage, clippedMessage, cursorMessage;
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
            const auto a = std::min(trace.size(), static_cast<std::size_t>(std::max(0.0, offset * fs)));
            const auto b = std::min(trace.size(), a + static_cast<std::size_t>(seconds * fs));
            double low = 0, high = 0;
            if (a < b) {
                const auto limits = std::minmax_element(trace.begin() + a, trace.begin() + b);
                low = *limits.first; high = *limits.second;
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
            setToolTip(descriptions.join(QStringLiteral("\n")));
        }
    }

  protected:
    void paintEvent(QPaintEvent *) override {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        p.fillRect(rect(), dark ? QColor("#22223a") : QColor(Qt::white));
        const QColor ink = dark ? QColor("#dde0ff") : QColor("#222222");
        const QColor grid = dark ? QColor("#41415b") : QColor("#e6e6e6");
        p.setPen(ink);
        if (traces.empty()) {
            p.drawText(rect().adjusted(16, 16, -16, -16), Qt::AlignCenter | Qt::TextWordWrap, noData);
            return;
        }
        const int n = static_cast<int>(traces.size());
        const double row = (height() - 30.0) / n;
        const std::array<QColor, 5> colors{QColor("#64748b"), QColor("#2563a8"), QColor("#228367"),
                                           QColor("#ab6226"), QColor("#9056a4")};
        const int left = 10, right = width() - 12;
        double commonScale = 0.0;
        if (sharedAmplitude && !fullPeaks.empty())
            commonScale = *std::max_element(fullPeaks.begin(), fullPeaks.end());
        for (int r = 0; r < n; ++r) {
            const auto &v = traces[r];
            const double top = r * row;
            const double plotTop = top + (groupPreview ? 39 : 23), plotBottom = top + row - 23;
            const double mid = (plotTop + plotBottom) / 2;
            const double halfHeight = std::max(1.0, (plotBottom - plotTop) / 2);
            p.setPen(grid);
            p.drawLine(left, qRound(mid), right, qRound(mid));
            for (int t = 0; t <= 5; ++t)
                p.drawLine(left + (right - left) * t / 5, qRound(plotTop), left + (right - left) * t / 5,
                           qRound(plotBottom));
            p.setPen(ink);
            p.drawText(QRectF(left, top + 2, right - left, 20), Qt::AlignLeft, names.value(r));
            if (groupPreview && r < static_cast<int>(peakToPeaks.size())) {
                const auto caption = peakToPeakMessage.arg(
                    locale.toString(peakToPeaks[r] < 1e-9 ? 0 : peakToPeaks[r], 'g', 4));
                p.drawText(QRectF(left, top + 20, right - left, 18), Qt::AlignLeft, caption);
            }
            if (v.empty())
                continue;
            const auto begin =
                frequency ? std::size_t(0)
                          : std::min(v.size() - 1, static_cast<std::size_t>(std::max(0.0, offset * fs)));
            const auto end =
                frequency ? v.size() : std::min(v.size(), begin + static_cast<std::size_t>(seconds * fs));
            if (end <= begin)
                continue;
            double scale = 0.0;
            for (auto i = begin; i < end; ++i)
                scale = std::max(scale, std::abs(v[i]));
            if (sharedAmplitude)
                scale = commonScale;
            else if (!frequency && r < static_cast<int>(fullPeaks.size()))
                scale = fullPeaks[r];
            if (scale < 1e-12)
                scale = 1.0;
            if (groupPreview)
                scale /= amplitudeZoom;
            QPainterPath path;
            // Min/max envelopes retain short peaks when many samples share a pixel.
            for (int x = left; x <= right; ++x) {
                const auto a = begin + (end - begin) * static_cast<std::size_t>(x - left) /
                                           std::max(1, right - left + 1);
                const auto b = std::min(
                    end, std::max(a + 1, begin + (end - begin) * static_cast<std::size_t>(x - left + 1) /
                                                     std::max(1, right - left + 1)));
                if (a >= end)
                    break;
                auto limits = std::minmax_element(v.begin() + a, v.begin() + b);
                const double y = mid - *limits.first / scale * halfHeight,
                             z = mid - *limits.second / scale * halfHeight;
                if (x == left)
                    path.moveTo(x, y);
                else
                    path.lineTo(x, y);
                path.lineTo(x, z);
            }
            p.save();
            p.setClipRect(QRectF(left, plotTop, right - left + 1, std::max(1.0, plotBottom - plotTop)));
            p.setPen(QPen(colors[r % 5], 1.1));
            p.drawPath(path);
            if (!frequency && cursor >= offset && cursor <= offset + seconds && seconds > 0) {
                const double cursorX = left + (right - left) * (cursor - offset) / seconds;
                p.setPen(QPen(dark ? QColor("#e4baf0") : QColor("#68237a"), 1.3, Qt::DashLine));
                p.drawLine(QPointF(cursorX, plotTop), QPointF(cursorX, plotBottom));
            }
            p.restore();
            p.setPen(ink);
            if (r < static_cast<int>(emptyGroups.size()) && emptyGroups[r])
                p.drawText(QRectF(left + 12, plotTop, right - left - 24, std::max(1.0, plotBottom - plotTop)),
                           Qt::AlignCenter | Qt::TextWordWrap, emptyMessage);
            else
                p.drawText(QRectF(left, top + row - 18, right - left, 16), Qt::AlignRight,
                           (frequency ? locale.toString(scale, 'g', 3)
                                      : QStringLiteral("± %1").arg(locale.toString(scale, 'g', 3))));
            if (groupPreview && r < static_cast<int>(smallWaves.size()) &&
                (smallWaves[r] || clipped[r])) {
                const QString message = clipped[r] ? clippedMessage : smallWaveMessage;
                const int scaleWidth = p.fontMetrics().horizontalAdvance(QStringLiteral("± 000000")) + 12;
                p.drawText(QRectF(left, top + row - 18, std::max(1, right - left - scaleWidth), 16),
                           Qt::AlignLeft, p.fontMetrics().elidedText(message, Qt::ElideRight,
                                                                   std::max(1, right - left - scaleWidth)));
            }
        }
        p.setPen(ink);
        p.drawText(QRectF(left, height() - 23, right - left, 20), Qt::AlignLeft,
                   QStringLiteral("%1 – %2 %3")
                       .arg(locale.toString(start + offset, 'f', 1))
                       .arg(locale.toString(start + offset + seconds, 'f', 1))
                       .arg(frequency ? QStringLiteral("Hz") : QStringLiteral("s")));
        if (!frequency && cursor >= 0) {
            p.drawText(QRectF(left, height() - 23, right - left, 20), Qt::AlignRight,
                       cursorMessage.arg(locale.toString(start + cursor, 'f', 1)));
        }
    }
};
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
    return i18n_->text(QString("app.separation.%1").arg(QString::fromLatin1(key)), params);
}
EegSeparationWorkspace::EegSeparationWorkspace(EegSeparationController *controller, I18nService *i18n,
                                               QWidget *parent)
    : QWidget(parent), controller_(controller), i18n_(i18n) {
    setObjectName("eegSeparationWorkspace");
    previewTimer_ = new QTimer(this);
    previewTimer_->setTimerType(Qt::PreciseTimer);
    previewTimer_->setInterval(40);
    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(10, 8, 10, 8);
    auto *header = new QHBoxLayout;
    title_ = wrapped(this);
    auto font = title_->font();
    font.setBold(true);
    font.setPointSize(font.pointSize() + 3);
    title_->setFont(font);
    header->addWidget(title_, 1);
    root->addLayout(header);
    subtitle_ = wrapped(this);
    root->addWidget(subtitle_);
    hint_ = wrapped(this);
    hint_->setObjectName("separationNextStep");
    root->addWidget(hint_);
    auto *splitter = new SeparationSplitter(this);
    root->addWidget(splitter, 1);
    auto *input = new QWidget(splitter);
    input->setMinimumWidth(235);
    auto *il = new QVBoxLayout(input);
    source_ = wrapped(input);
    source_->setObjectName("separationSource");
    open_ = new QPushButton(input);
    open_->setObjectName("separationOpen");
    il->addWidget(source_);
    il->addWidget(open_);
    teaching_ = new QPushButton(input);
    teaching_->setObjectName("separationOpenTeaching");
    il->addWidget(teaching_);
    teachingHelp_ = wrapped(input);
    il->addWidget(teachingHelp_);
    hardware_ = wrapped(input);
    hardware_->setObjectName("separationHardware");
    il->addWidget(hardware_);
    rangeLabel_ = wrapped(input);
    il->addWidget(rangeLabel_);
    start_ = new QDoubleSpinBox(input);
    start_->setObjectName("separationStart");
    start_->setDecimals(3);
    start_->setSuffix(" s");
    start_->setRange(0, 8640000);
    duration_ = new QDoubleSpinBox(input);
    duration_->setObjectName("separationDuration");
    duration_->setDecimals(3);
    duration_->setSuffix(" s");
    duration_->setRange(5, 120);
    duration_->setValue(60);
    il->addWidget(start_);
    il->addWidget(duration_);
    rolesHelp_ = wrapped(input);
    il->addWidget(rolesHelp_);
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
    bulkActions_ = new QWidget(input);
    bulkActions_->setObjectName("separationBulkActions");
    auto *bulkLayout = new QVBoxLayout(bulkActions_);
    bulkLayout->setContentsMargins(0, 0, 0, 0);
    bulkSelection_ = wrapped(bulkActions_);
    bulkSelection_->setObjectName("separationBulkSelection");
    bulkLayout->addWidget(bulkSelection_);
    bulkRole_ = new QComboBox(bulkActions_);
    bulkRole_->setObjectName("separationBulkRole");
    bulk_ = new QPushButton(bulkActions_);
    bulk_->setObjectName("separationApplyRole");
    bulkLayout->addWidget(bulkRole_);
    bulkLayout->addWidget(bulk_);
    il->addWidget(bulkActions_);
    advanced_ = new CollapsiblePanel({}, input);
    rate_ = new QComboBox(input);
    rate_->addItem("500 Hz", 500);
    rate_->addItem("250 Hz", 250);
    notch_ = new QComboBox(input);
    notch_->addItem(QString(), 0);
    notch_->addItem("50 Hz", 50);
    notch_->addItem("60 Hz", 60);
    advanced_->addWidget(rate_);
    advanced_->addWidget(notch_);
    advanced_->setExpanded(false);
    il->addWidget(advanced_);
    splitter->addWidget(scrolling(input, splitter));
    auto *preview = new QWidget(splitter);
    preview->setMinimumWidth(250);
    auto *pl = new QVBoxLayout(preview);
    previewTitle_ = wrapped(preview);
    pl->addWidget(previewTitle_);
    auto *controls = new QHBoxLayout;
    previewChannelLabel_ = new QLabel(preview);
    previewChannelLabel_->setObjectName("separationPreviewChannelLabel");
    channel_ = new QComboBox(preview);
    channel_->setObjectName("separationPreviewChannel");
    channel_->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
    window_ = new QComboBox(preview);
    window_->setObjectName("separationDisplayWindow");
    displayWindowLabel_ = new QLabel(preview);
    displayWindowLabel_->setObjectName("separationDisplayWindowLabel");
    window_->addItem("10 s", 10);
    window_->addItem("5 s", 5);
    previewChannelLabel_->setBuddy(channel_);
    displayWindowLabel_->setBuddy(window_);
    controls->addWidget(previewChannelLabel_);
    controls->addWidget(channel_, 1);
    controls->addWidget(displayWindowLabel_);
    controls->addWidget(window_);
    pl->addLayout(controls);
    auto *amplitudeControls = new QHBoxLayout;
    amplitudeModeLabel_ = new QLabel(preview);
    amplitudeMode_ = new QComboBox(preview);
    amplitudeMode_->setObjectName("separationAmplitudeMode");
    amplitudeMode_->addItem(QString(), 0);
    amplitudeMode_->addItem(QString(), 1);
    amplitudeModeLabel_->setBuddy(amplitudeMode_);
    amplitudeZoomLabel_ = new QLabel(preview);
    amplitudeZoomLabel_->setObjectName("separationWaveformZoomLabel");
    amplitudeZoom_ = new QComboBox(preview);
    amplitudeZoom_->setObjectName("separationAmplitudeZoom");
    for (int zoom : {1, 2, 5, 10})
        amplitudeZoom_->addItem(QStringLiteral("%1×").arg(zoom), zoom);
    amplitudeZoomLabel_->setBuddy(amplitudeZoom_);
    amplitudeControls->addWidget(amplitudeModeLabel_);
    amplitudeControls->addWidget(amplitudeMode_, 1);
    amplitudeControls->addWidget(amplitudeZoomLabel_);
    amplitudeControls->addWidget(amplitudeZoom_);
    pl->addLayout(amplitudeControls);
    amplitudeHelp_ = wrapped(preview);
    amplitudeHelp_->setObjectName("separationAmplitudeHelp");
    pl->addWidget(amplitudeHelp_);
    groups_ = new SeparationTraceView(preview);
    groups_->setObjectName("separationGroupTraces");
    groups_->sharedAmplitude = true;
    groups_->groupPreview = true;
    pl->addWidget(groups_, 1);
    scrub_ = new QSlider(Qt::Horizontal, preview);
    scrub_->setRange(0, 1000);
    scrub_->setObjectName("separationPreviewPosition");
    auto *playbackControls = new QHBoxLayout;
    play_ = new QPushButton(preview);
    play_->setObjectName("separationPlay");
    restart_ = new QPushButton(preview);
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
    evidenceTitle_ = wrapped(evidence);
    el->addWidget(evidenceTitle_);
    components_ = new QTableWidget(evidence);
    components_->setObjectName("separationComponents");
    components_->setColumnCount(2);
    components_->verticalHeader()->hide();
    components_->setSelectionBehavior(QAbstractItemView::SelectRows);
    components_->setSelectionMode(QAbstractItemView::SingleSelection);
    components_->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Fixed);
    components_->setColumnWidth(0, 60);
    components_->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
    components_->setMinimumHeight(100);
    components_->setMaximumHeight(220);
    el->addWidget(components_);
    undo_ = new QPushButton(evidence);
    undo_->setObjectName("separationUndo");
    el->addWidget(undo_);
    exampleLabels_ = new QPushButton(evidence);
    exampleLabels_->setObjectName("separationTeachingLabels");
    el->addWidget(exampleLabels_);
    auto *details = new QWidget(evidence);
    auto *dl = new QVBoxLayout(details);
    clues_ = wrapped(details);
    clues_->setObjectName("separationClues");
    evidenceDetails_ = new CollapsiblePanel({}, details);
    evidenceDetails_->addWidget(clues_);
    evidenceDetails_->setExpanded(false);
    sourceCaption_ = wrapped(details);
    dl->addWidget(sourceCaption_);
    componentTrace_ = new SeparationTraceView(details);
    componentTrace_->setObjectName("separationComponentTrace");
    componentTrace_->setMinimumHeight(145);
    dl->addWidget(componentTrace_);
    spectrum_ = new SeparationTraceView(details);
    spectrum_->setObjectName("separationSpectrum");
    spectrum_->setMinimumHeight(145);
    dl->addWidget(spectrum_);
    dl->addWidget(evidenceDetails_);
    dl->addStretch();
    el->addWidget(scrolling(details, evidence), 1);
    confirmed_ = new QCheckBox(evidence);
    confirmed_->setObjectName("separationConfirmed");
    confirmed_->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Minimum);
    el->addWidget(confirmed_);
    splitter->addWidget(evidence);
    splitter->setStretchFactor(1, 1);
    splitter->setSizes({265, 610, 335});
    status_ = wrapped(this);
    status_->setObjectName("separationStatus");
    root->addWidget(status_);
    auto *footer = new QHBoxLayout;
    retry_ = new QPushButton(this);
    cancel_ = new QPushButton(this);
    main_ = new QPushButton(this);
    main_->setObjectName("separationMainAction");
    main_->setDefault(true);
    main_->setMinimumHeight(34);
    footer->addWidget(retry_);
    footer->addStretch();
    footer->addWidget(cancel_);
    footer->addWidget(main_);
    root->addLayout(footer);
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
    connect(open_, &QPushButton::clicked, this, &EegSeparationWorkspace::chooseSource);
    connect(teaching_, &QPushButton::clicked, controller_, &EegSeparationController::openTeachingSource);
    connect(exampleLabels_, &QPushButton::clicked, controller_,
            &EegSeparationController::applyTeachingLabels);
    connect(bulk_, &QPushButton::clicked, this, [this] {
        if (controller_->isBusy() || channels_->selectionModel()->selectedRows().size() < 2)
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
            pausePreview();
            refreshPlots();
        }
    });
    connect(undo_, &QPushButton::clicked, controller_, &EegSeparationController::undoLabel);
    auto *undoShortcut = new QShortcut(QKeySequence::Undo, this);
    undoShortcut->setContext(Qt::WidgetWithChildrenShortcut);
    connect(undoShortcut, &QShortcut::activated, controller_, &EegSeparationController::undoLabel);
    connect(confirmed_, &QCheckBox::toggled, controller_, &EegSeparationController::setConfirmed);
    connect(cancel_, &QPushButton::clicked, controller_, &EegSeparationController::cancel);
    connect(main_, &QPushButton::clicked, this, [this] {
        errorCode_.clear();
        if (controller_->output())
            exportResults();
        else
            controller_->start();
    });
    connect(retry_, &QPushButton::clicked, this, [this] {
        errorCode_.clear();
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
        {
            const QSignalBlocker b(scrub_);
            scrub_->setValue(0);
        }
        refreshComponents();
        refresh();
    });
    connect(controller_, &EegSeparationController::labelsChanged, this, [this] {
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
    connect(controller_, &EegSeparationController::exported, this,
            [this](const QString &path) { status_->setText(text("exported", {{"path", path}})); });
    connect(i18n_, &I18nService::languageChanged, this, &EegSeparationWorkspace::retranslateUi);
    retranslateUi();
    refreshSource();
}
void EegSeparationWorkspace::chooseSource() {
    pausePreview();
    const auto path = QFileDialog::getOpenFileName(this, text("open"));
    if (!path.isEmpty()) {
        errorCode_.clear();
        controller_->openSource(path);
    }
}
void EegSeparationWorkspace::applySettings() {
    if (refreshing_)
        return;
    auto settings = controller_->settings();
    settings.start = start_->value();
    settings.duration = duration_->value();
    settings.outputRate = rate_->currentData().toInt();
    settings.notchHz = notch_->currentData().toInt();
    controller_->setSettings(settings);
    refresh();
}
void EegSeparationWorkspace::refreshSource() {
    const QScopedValueRollback<bool> guard(refreshing_, true);
    const auto &s = controller_->source();
    const auto &cfg = controller_->settings();
    source_->setText(s.path.isEmpty()
                         ? text("no_source")
                         : text("source_summary", {{"file", QFileInfo(s.path).fileName()},
                                                   {"seconds", i18n_->formatNumber(s.duration(), 2)},
                                                   {"channels", s.channels.size()}}));
    source_->setToolTip(s.path);
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
            combo = new QComboBox(channels_);
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
                    controller_->setSettings(cfg);
                    refresh();
                }
            });
        }
        const QSignalBlocker blocker(combo);
        for (int j = 0; j < 5; ++j)
            combo->setItemText(j, text(qPrintable(QString("role_%1").arg(j))));
        combo->setCurrentIndex(r < static_cast<int>(cfg.roles.size()) ? cfg.roles[r] : 0);
    }
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
    components_->setRowCount(output ? static_cast<int>(output->sources.size()) : 0);
    if (output) {
        for (int r = 0; r < components_->rowCount(); ++r) {
            if (!components_->item(r, 0)) {
                auto *item = new QTableWidgetItem(QStringLiteral("IC %1").arg(r + 1));
                item->setFlags(item->flags() & ~Qt::ItemIsEditable);
                components_->setItem(r, 0, item);
            }
            auto *combo = qobject_cast<QComboBox *>(components_->cellWidget(r, 1));
            if (!combo) {
                combo = new QComboBox(components_);
                combo->setObjectName(QStringLiteral("separationLabel%1").arg(r));
                components_->setCellWidget(r, 1, combo);
                for (int g = 0; g < 4; ++g)
                    combo->addItem(QString());
                connect(combo, &QComboBox::highlighted, this,
                        [this, r] { components_->setCurrentCell(r, 0); });
                connect(combo, &QComboBox::currentIndexChanged, this, [this, r](int group) {
                    if (!refreshing_) {
                        components_->setCurrentCell(r, 0);
                        controller_->setLabel(r, group);
                    }
                });
            }
            // Keep the active editor alive while Qt delivers its activated signal.
            const QSignalBlocker blocker(combo);
            for (int g = 0; g < 4; ++g)
                combo->setItemText(g, text(qPrintable(QString("group_%1").arg(g))));
            combo->setCurrentIndex(controller_->labels().at(r));
        }
    }
    if (components_->rowCount())
        components_->setCurrentCell(std::clamp(selected, 0, components_->rowCount() - 1), 0);
    refreshPlots();
}
void EegSeparationWorkspace::refresh() {
    const bool busy = controller_->isBusy(), result = bool(controller_->output()),
               hasSource = !controller_->source().path.isEmpty();
    const auto state = controller_->state();
    if (busy || !result)
        pausePreview();
    if (!result)
        previewPosition_ = 0;
    teaching_->setEnabled(!busy);
    exampleLabels_->setVisible(controller_->source().teaching && result);
    exampleLabels_->setEnabled(!busy && controller_->canApplyTeachingLabels());
    teachingHelp_->setVisible(controller_->source().teaching || !hasSource);
    open_->setEnabled(!busy);
    channels_->setEnabled(!busy);
    refreshBulkActions();
    amplitudeMode_->setEnabled(result);
    amplitudeZoom_->setEnabled(result);
    start_->setEnabled(!busy && hasSource);
    duration_->setEnabled(!busy && hasSource);
    advanced_->setEnabled(!busy && hasSource);
    components_->setEnabled(!busy);
    undo_->setEnabled(!busy && controller_->canUndoLabel());
    confirmed_->setEnabled(result && !busy);
    {
        const QSignalBlocker blocker(confirmed_);
        confirmed_->setChecked(controller_->isConfirmed());
    }
    main_->setText(text(result ? "export" : "start"));
    cancel_->setVisible(busy);
    cancel_->setEnabled(stage_ != QStringLiteral("publishing"));
    retry_->setVisible(!busy && !errorCode_.isEmpty() && errorCode_ != QStringLiteral("analysis.cancelled"));
    int observations = 0, unconfirmed = 0, selectedChannels = 0;
    for (int role : controller_->settings().roles) {
        observations += role == 1;
        unconfirmed += role == 0;
        selectedChannels += role >= 1 && role <= 3;
    }
    const auto &cfg = controller_->settings();
    const bool rateValid = controller_->source().fs >= 250 && cfg.outputRate <= controller_->source().fs;
    const bool valid = hasSource && rateValid && selectedChannels <= 40 && observations >= 2 &&
                       observations <= 32 && !unconfirmed && cfg.duration >= 5 &&
                       cfg.start + cfg.duration <= controller_->source().duration() + 1e-8;
    main_->setEnabled(!busy && (result ? controller_->isConfirmed() : valid));
    hint_->setText(text(busy                    ? "next_busy"
                        : !hasSource            ? "next_open"
                        : result                ? "next_review"
                        : !rateValid            ? "next_rate"
                        : selectedChannels > 40 ? "next_selected"
                        : unconfirmed           ? "next_roles"
                        : !valid                ? "next_range"
                                                : "next_start"));
    if (!errorCode_.isEmpty()) {
        const QString key = QStringLiteral("error.") + errorCode_;
        QString explanation = i18n_->text(key, errorParams_);
        if (explanation == key)
            explanation = text("error_generic", {{"code", errorCode_}});
        status_->setText(explanation.section(QStringLiteral("\n\n"), 0, 0));
        status_->setToolTip(explanation);
    } else if (busy)
        status_->setText(text(stage_ == QStringLiteral("publishing")                ? "progress_publishing"
                              : state == EegSeparationController::State::Inspecting ? "progress_inspecting"
                              : state == EegSeparationController::State::Exporting  ? "progress_exporting"
                                                                                    : "progress_running"));
    else if (!controller_->exportedPath().isEmpty())
        status_->setText(text("exported", {{"path", controller_->exportedPath()}}));
    else
        status_->setText(text(result ? "review_notice" : "setup_notice"));
    refreshPlots();
    refreshPlaybackControls();
}
double EegSeparationWorkspace::previewDuration() const {
    const auto output = controller_->output();
    return output && !output->analysis.empty() && output->fs > 0
               ? output->analysis[0].size() / output->fs : 0.;
}
void EegSeparationWorkspace::refreshPlaybackControls() {
    const bool available = controller_->output() && !controller_->isBusy();
    play_->setEnabled(available);
    restart_->setEnabled(available);
    scrub_->setEnabled(available);
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
    QWidget::hideEvent(event);
}
void EegSeparationWorkspace::refreshBulkActions() {
    const auto count = channels_->selectionModel()->selectedRows().size();
    const bool available = count >= 2 && !controller_->source().path.isEmpty();
    bulkActions_->setVisible(available);
    bulkRole_->setEnabled(available && !controller_->isBusy());
    bulk_->setEnabled(available && !controller_->isBusy());
    bulkSelection_->setText(text("bulk_selection", {{"count", static_cast<int>(count)}}));
    bulk_->setText(text("bulk", {{"count", static_cast<int>(count)}}));
}
void EegSeparationWorkspace::refreshPlots() {
    groups_->sharedAmplitude = amplitudeMode_->currentIndex() == 0;
    groups_->amplitudeZoom = amplitudeZoom_->currentData().toDouble();
    amplitudeHelp_->setText(text(groups_->sharedAmplitude ? "amplitude_help_shared"
                                                        : "amplitude_help_individual"));
    const auto output = controller_->output();
    if (!output) {
        spectrumOutput_.reset();
        spectrumComponent_ = -1;
        groups_->traces.clear();
        groups_->refreshMetrics();
        componentTrace_->traces.clear();
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
    const double view = std::min(double(window_->currentData().toInt()),
                                 output->analysis.empty() ? 0 : output->analysis[0].size() / output->fs);
    const double full = output->analysis.empty() ? 0 : output->analysis[0].size() / output->fs;
    const double offset = std::clamp(previewPosition_ - view / 2, 0., std::max(0., full - view));
    groups_->fs = output->fs;
    groups_->seconds = view;
    groups_->offset = offset;
    groups_->start = output->start;
    groups_->cursor = previewPosition_;
    groups_->traces.clear();
    groups_->names.clear();
    groups_->emptyGroups.clear();
    if (ch < static_cast<int>(output->analysis.size())) {
        groups_->traces.push_back(output->analysis[ch]);
        groups_->names << text("analysis_trace");
        groups_->emptyGroups.push_back(false);
        for (int g = 0; g < 4; ++g) {
            const auto &projection = controller_->projection(g);
            if (ch < static_cast<int>(projection.size()))
                groups_->traces.push_back(projection[ch]);
            else
                groups_->traces.push_back({});
            groups_->names << text(qPrintable(QString("group_%1").arg(g))) + QStringLiteral(" · µV");
            groups_->emptyGroups.push_back(std::find(controller_->labels().begin(),
                                                     controller_->labels().end(),
                                                     g) == controller_->labels().end());
        }
    }
    groups_->refreshMetrics();
    const int component = components_->currentRow();
    componentTrace_->traces.clear();
    if (component >= 0 && component < static_cast<int>(output->sources.size())) {
        componentTrace_->fs = output->fs;
        componentTrace_->start = output->start;
        componentTrace_->seconds = view;
        componentTrace_->offset = offset;
        componentTrace_->cursor = previewPosition_;
        componentTrace_->traces = {output->sources[component]};
        componentTrace_->names = {text("component_trace")};
        componentTrace_->refreshMetrics();
        spectrum_->names = {text("spectrum")};
        if (spectrumOutput_ != output || spectrumComponent_ != component) {
            spectrumOutput_ = output;
            spectrumComponent_ = component;
            const auto psd = dsp::computeWelchPsd(output->sources[component], output->fs, 0, false);
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
            clues_->setText(text("clues_empty"));
    }
    groups_->update();
    componentTrace_->update();
    spectrum_->update();
}
void EegSeparationWorkspace::exportResults() {
    pausePreview();
    QDialog dialog(this);
    dialog.setWindowTitle(text("export_title"));
    auto *layout = new QVBoxLayout(&dialog);
    auto *notice = wrapped(&dialog);
    notice->setText(text("export_notice"));
    layout->addWidget(notice);
    auto *files = new QLabel(text("export_files"), &dialog);
    files->setTextInteractionFlags(Qt::TextSelectableByMouse);
    layout->addWidget(files);
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    buttons->button(QDialogButtonBox::Ok)->setText(text("choose_export"));
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    layout->addWidget(buttons);
    if (dialog.exec() != QDialog::Accepted)
        return;
    const auto initial = QFileInfo(controller_->source().path)
                             .dir()
                             .filePath(QFileInfo(controller_->source().path).completeBaseName() +
                                       QStringLiteral("-separation"));
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
    errorCode_ = error.code;
    errorParams_ = error.params;
    errorRetryable_ = error.retryable;
    refresh();
}
void EegSeparationWorkspace::setDarkTheme(bool dark) {
    QPalette colors = palette();
    colors.setColor(QPalette::Window, QColor(dark ? "#181828" : "#f0f0f0"));
    colors.setColor(QPalette::Base, QColor(dark ? "#22223a" : "#ffffff"));
    colors.setColor(QPalette::Button, QColor(dark ? "#282840" : "#f0f0f0"));
    for (auto role : {QPalette::WindowText, QPalette::Text, QPalette::ButtonText})
        colors.setColor(role, QColor(dark ? "#dde0ff" : "#222222"));
    setPalette(colors);
    setAutoFillBackground(true);
    for (auto *plot : {groups_, componentTrace_, spectrum_}) {
        plot->dark = dark;
        plot->update();
    }
}
void EegSeparationWorkspace::retranslateUi() {
    teaching_->setText(text("open_teaching"));
    exampleLabels_->setText(text("example_labels"));
    teachingHelp_->setText(text("teaching_notice"));
    title_->setText(text("title"));
    subtitle_->setText(text("subtitle"));
    open_->setText(text("open"));
    rangeLabel_->setText(text("range"));
    start_->setToolTip(text("start_time"));
    start_->setAccessibleName(text("start_time"));
    duration_->setToolTip(text("duration"));
    duration_->setAccessibleName(text("duration"));
    rolesHelp_->setText(text("roles_help"));
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
        amplitudeZoom_->setItemText(i, text("zoom_factor", {{"value", amplitudeZoom_->itemData(i).toInt()}}));
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
    channel_->setAccessibleName(text("preview_channel"));
    window_->setAccessibleName(text("display_window_visible"));
    scrub_->setAccessibleName(text("preview_position"));
    evidenceTitle_->setText(text("evidence_title"));
    evidenceDetails_->setTitle(text("evidence_details"));
    undo_->setText(text("undo"));
    sourceCaption_->setText(text("source_caption"));
    confirmed_->setText(text("confirmed"));
    confirmed_->setToolTip(text("confirmation_help"));
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
}
} // namespace bf
