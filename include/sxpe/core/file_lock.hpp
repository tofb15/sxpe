#pragma once

#include "sxpe/error.hpp"

#include <filesystem>
#include <string>

namespace sxpe::core {

/// Actionable text for sharing-violation / exclusive-lock failures (open + save).
/// Prefer this over raw errno / Win32 codes in bus surfaces.
[[nodiscard]] std::string file_locked_message();

/// Map the last CreateFile / open(2) failure (call immediately after the failing API).
/// Never returns a blanket "in use" for missing files.
[[nodiscard]] std::string map_open_failure_message(bool writable);

/// Map the last replace / rename / write-open failure for an existing destination.
[[nodiscard]] std::string map_save_failure_message();

/// True when path looks like Documents/.../Electronic Arts/.../Mods (any Sims title folder).
[[nodiscard]] bool looks_like_ea_mods_path(const std::filesystem::path& path);

/// Optional warning when under Mods without an exclusive lock (read-only / shared open).
[[nodiscard]] std::string mods_path_lock_warning();

enum class LockProbeStatus {
    ok = 0,
    locked,
    access_denied,
    not_found,
    other,
};

struct LockProbeResult {
    LockProbeStatus status{LockProbeStatus::other};
    std::string message;
};

/// Briefly try an exclusive write lock (CreateFile share / flock). Does not keep the lock.
/// Safe to call while another fd in this process holds a read mapping (Windows may report locked).
[[nodiscard]] LockProbeResult probe_exclusive_write(const std::filesystem::path& path);

}  // namespace sxpe::core
