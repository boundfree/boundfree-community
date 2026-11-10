#pragma once

#include "acquisition/model/signal_model.h"

#include <QByteArray>
#include <QJsonObject>
#include <QString>

#include <cstdint>
#include <optional>

namespace bf::acquisition_io {

QString dtypeName(acquisition::DType dtype);
QString unitName(acquisition::Unit unit);

// Complete stream identity, including a decimal-string local generation.
QJsonObject identityToJson(const acquisition::DeviceDescriptor& device,
                           const acquisition::StreamDescriptor& stream);
// recordStart is the original sample index snapshotted when recording begins.
// Invalid descriptor metadata produces an empty object. A locally generated
// capture may have a larger raw block limit than a BFSP wire DATA frame.
QJsonObject descriptorToJson(const acquisition::DeviceDescriptor& device,
                             const acquisition::StreamDescriptor& stream,
                             std::optional<std::uint64_t> recordStart = std::nullopt);

// Validates BFSP Descriptor JSON before mapping it into the native model. The
// transport must validate original wire bytes too: a QJsonObject cannot retain
// duplicate JSON keys. Device/boot identity is checked by the owning session.
acquisition::Result<acquisition::StreamDescriptor>
descriptorFromJson(const QJsonObject& json, acquisition::Generation generation);

// payload is the complete BFSP DATA payload, including its 48-byte prefix;
// the caller must match the frame header's stream_id/session to this stream.
// A missing host timestamp remains unknown; no synthetic zero is inserted.
acquisition::Result<acquisition::SampleBlock>
blockFromPayload(const acquisition::StreamDescriptor& stream, const QByteArray& payload,
                 const acquisition::MemoryBudget& budget,
                 std::optional<std::uint64_t> hostNs, const QString& hostClockId);

// Serialize only channel-major sample bytes for capture_append, in LE order.
acquisition::Result<QByteArray> encodeRawSamples(const acquisition::SampleBlock& block);

} // namespace bf::acquisition_io
