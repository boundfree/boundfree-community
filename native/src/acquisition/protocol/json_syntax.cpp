#include "internal.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QSet>
#include <QStringDecoder>

#include <cmath>

namespace bf::bfsp::detail {
namespace {

// Qt's object parser overwrites duplicate keys. This small preflight walks the
// original UTF-8 and compares decoded names before QJsonDocument sees them.
class JsonPreflight {
public:
    explicit JsonPreflight(QByteArrayView bytes) : bytes_(bytes) {}

    bool run()
    {
        if (bytes_.startsWith(QByteArrayView("\xef\xbb\xbf", 3))) return false;
        QStringDecoder decoder(QStringDecoder::Utf8);
        decoder.decode(bytes_);
        if (decoder.hasError()) return false;
        skip_space();
        if (!value(0)) return false;
        skip_space();
        return position_ == bytes_.size();
    }

private:
    void skip_space()
    {
        while (position_ < bytes_.size()) {
            char c = bytes_[position_];
            if (c != ' ' && c != '\t' && c != '\r' && c != '\n') break;
            ++position_;
        }
    }

    bool consume(char c)
    {
        skip_space();
        if (position_ == bytes_.size() || bytes_[position_] != c) return false;
        ++position_;
        return true;
    }

    bool string(QString& decoded)
    {
        skip_space();
        if (position_ == bytes_.size() || bytes_[position_] != '"') return false;
        const auto start = position_++;
        bool escaped = false;
        bool closed = false;
        while (position_ < bytes_.size()) {
            const auto c = static_cast<unsigned char>(bytes_[position_++]);
            if (c < 0x20) return false;
            if (!escaped && c == '"') { closed = true; break; }
            if (!escaped && c == '\\') escaped = true;
            else escaped = false;
        }
        if (!closed) return false;
        QByteArray token("[");
        token.append(bytes_.data() + start, position_ - start);
        token.append(']');
        QJsonParseError parse_error;
        const auto document = QJsonDocument::fromJson(token, &parse_error);
        if (parse_error.error != QJsonParseError::NoError || !document.isArray() ||
            document.array().size() != 1 || !document.array().at(0).isString()) return false;
        decoded = document.array().at(0).toString();
        for (qsizetype i = 0; i < decoded.size(); ++i) {
            const QChar c = decoded[i];
            if (c.isHighSurrogate()) {
                if (++i == decoded.size() || !decoded[i].isLowSurrogate()) return false;
            } else if (c.isLowSurrogate()) {
                return false;
            }
        }
        return decoded.toUtf8().size() <= 4096;
    }

    bool value(int depth)
    {
        skip_space();
        if (position_ == bytes_.size()) return false;
        const char c = bytes_[position_];
        if (c == '"') {
            QString text;
            return string(text);
        }
        if (c == '{' || c == '[') {
            if (depth >= 16) return false;
            ++position_;
            skip_space();
            const bool object = c == '{';
            const char end = object ? '}' : ']';
            if (position_ < bytes_.size() && bytes_[position_] == end) { ++position_; return true; }
            QSet<QString> keys;
            int items = 0;
            for (;;) {
                if (object) {
                    QString key;
                    if (!string(key) || keys.contains(key) || !consume(':')) return false;
                    keys.insert(key);
                } else if (++items > 4096) {
                    return false;
                }
                if (!value(depth + 1)) return false;
                skip_space();
                if (position_ == bytes_.size()) return false;
                if (bytes_[position_] == end) { ++position_; return true; }
                if (bytes_[position_++] != ',') return false;
            }
        }
        const auto start = position_;
        while (position_ < bytes_.size()) {
            const char current = bytes_[position_];
            if (current == ',' || current == '}' || current == ']' || current == ' ' ||
                current == '\t' || current == '\r' || current == '\n') break;
            ++position_;
        }
        if (position_ == start) return false;
        const QByteArray token(bytes_.data() + start, position_ - start);
        if (token == "true" || token == "false" || token == "null") return true;
        bool ok = false;
        const double number = token.toDouble(&ok);
        return ok && std::isfinite(number);
    }

    QByteArrayView bytes_;
    qsizetype position_ = 0;
};
}  // namespace

Result parse_json(QByteArrayView bytes, QJsonObject& output)
{
    if (!JsonPreflight(bytes).run()) return fail(Error::InvalidPayload, "json_syntax_or_limits");
    QJsonParseError parse_error;
    const auto document = QJsonDocument::fromJson(QByteArray(bytes.data(), bytes.size()), &parse_error);
    if (parse_error.error != QJsonParseError::NoError || !document.isObject())
        return fail(Error::InvalidPayload, "json_object");
    output = document.object();
    return {};
}

}  // namespace bf::bfsp::detail
