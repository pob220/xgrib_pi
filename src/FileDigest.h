#pragma once

#include <filesystem>
#include <optional>
#include <string>

namespace xgrib {

/** Return a lower-case SHA-256 digest, or nullopt on an I/O failure. */
std::optional<std::string> Sha256File(const std::filesystem::path& path);

}  // namespace xgrib
