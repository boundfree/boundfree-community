#include "controller/marker_store.h"

#include <algorithm>

namespace bf {
namespace {

const QVector<QColor>& markerColors() {
    static const QVector<QColor> colors{
        QColor("#FFD740"), QColor("#FF5252"), QColor("#69F0AE"), QColor("#40C4FF"),
        QColor("#E040FB"), QColor("#FF6D00"), QColor("#CCFF90"), QColor("#EA80FC"),
    };
    return colors;
}

}  // namespace

void MarkerStore::replaceFromEvents(const QVector<QPair<double, QString>>& events) {
    markers_.clear();
    for (const auto& event : events) {
        add(event.first, event.second);
    }
}

void MarkerStore::add(double time, const QString& label) {
    Marker marker{std::max(0.0, time), label.isEmpty() ? defaultLabel() : label, nextColor()};
    const auto position = std::upper_bound(markers_.begin(), markers_.end(), marker.time, [](double value, const Marker& item) {
        return value < item.time;
    });
    markers_.insert(position, marker);
}

bool MarkerStore::updateLabel(int index, const QString& label) {
    if (index < 0 || index >= markers_.size()) {
        return false;
    }
    markers_[index].label = label;
    return true;
}

bool MarkerStore::remove(int index) {
    if (index < 0 || index >= markers_.size()) {
        return false;
    }
    markers_.removeAt(index);
    return true;
}

void MarkerStore::clear() {
    markers_.clear();
}

QColor MarkerStore::nextColor() const {
    const QVector<QColor>& colors = markerColors();
    return colors[markers_.size() % colors.size()];
}

QString MarkerStore::defaultLabel() const {
    return QString("M%1").arg(markers_.size() + 1);
}

}  // namespace bf
