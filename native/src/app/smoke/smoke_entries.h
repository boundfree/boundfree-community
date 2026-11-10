#pragma once

#include "core/version.h"

#include <QString>
#include <optional>

namespace bf {

// 处理 --version 与全部 --smoke-* 自检入口。命中时返回进程退出码；
// 没有命中任何入口时返回 std::nullopt，调用方继续正常启动。
std::optional<int> runCommandLineEntry(int argc, char** argv, const VersionInfo& version,
                                       const QString& repoRoot);

}  // namespace bf
