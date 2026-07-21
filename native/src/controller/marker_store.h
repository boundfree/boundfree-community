#pragma once

#include <QColor>
#include <QString>
#include <QVector>

namespace bf {

struct Marker {
    double time = 0.0;
    QString label;
    QColor color;
};

class MarkerStore final {
public:
    void replaceFromEvents(const QVector<QPair<double, QString>>& events);
    void add(double time, const QString& label = {});
    bool updateLabel(int index, const QString& label);
    bool remove(int index);
    void clear();

    const QVector<Marker>& markers() const { return markers_; }

private:
    QColor nextColor() const;
    QString defaultLabel() const;

    QVector<Marker> markers_;
};

}  // namespace bf
