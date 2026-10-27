#pragma once

#include <filesystem>
#include <string>

namespace journey {
// A nonempty explicit override takes precedence. Otherwise a private desk.key
// beside the reservation data is created once and reused across restarts.
std::string resolve_desk_key(std::filesystem::path storage, std::string override_key = {});
}
