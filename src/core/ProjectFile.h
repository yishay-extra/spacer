#pragma once

// Project file I/O (SPEC §15). `*.spacer` = UTF-8 JSON.

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "core/Expected.h"
#include "core/Project.h"

namespace spacer {

struct Error {
    std::string message;
};

struct LoadedProject {
    Project project;
    /// Paths of keys in the file that this version does not know, e.g.
    /// "sources[0].foo". Ignored on load (forward compatibility); the caller
    /// decides whether to log them.
    std::vector<std::string> unknownKeys;
    /// Version found in the file before migration.
    int fileSchemaVersion{kProjectSchemaVersion};
};

namespace projectfile {

/// Parse project JSON text. Fails (never throws) on malformed JSON, wrong
/// types, unknown enum values, missing ids/discriminators, a missing
/// schemaVersion, or a schemaVersion newer than kProjectSchemaVersion.
/// Does not run Validate; semantic checks are the caller's choice.
Expected<LoadedProject, Error> parse(std::string_view text);

/// Read and parse a file.
Expected<LoadedProject, Error> load(const std::filesystem::path& path);

/// Serialise (pretty-printed, 2-space indent, keys sorted).
std::string serialize(const Project& project);

/// Write atomically (temp file in the same directory, then rename).
/// Returns nullopt on success.
std::optional<Error> save(const Project& project, const std::filesystem::path& path);

}  // namespace projectfile
}  // namespace spacer
