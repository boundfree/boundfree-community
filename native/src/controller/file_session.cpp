#include "controller/file_session.h"

namespace bf {

QString formatChannelList(const QStringList& channels) {
    return channels.join(",");
}

}  // namespace bf
