#include "core/ProjectFile.h"

#include <fstream>
#include <sstream>
#include <system_error>

#include "core/Json.h"

namespace spacer::projectfile {

namespace {

/// Upgrade `j` in place from schema `from` to `from + 1`. Add a case when the
/// schema changes; v1 is the first version, so there is nothing to migrate yet.
void migrateOnce(Json& /*j*/, int /*from*/) {}

void migrateToCurrent(Json& j, int fromVersion) {
    for (int v = fromVersion; v < kProjectSchemaVersion; ++v) {
        migrateOnce(j, v);
        j["schemaVersion"] = v + 1;
    }
}

Unexpected<Error> err(std::string msg) {
    return Unexpected<Error>{Error{std::move(msg)}};
}

}  // namespace

Expected<LoadedProject, Error> parse(std::string_view text) {
    try {
        Json j = Json::parse(text.begin(), text.end());
        if (!j.is_object()) return err("project file: top level must be a JSON object");

        const auto it = j.find("schemaVersion");
        if (it == j.end() || !it->is_number_integer())
            return err("project file: missing or invalid \"schemaVersion\"");
        const int version = it->get<int>();
        if (version < 1) return err("project file: invalid schemaVersion " + std::to_string(version));
        if (version > kProjectSchemaVersion)
            return err("project file was saved by a newer version of Spacer (schema " +
                       std::to_string(version) + ", this build understands up to " +
                       std::to_string(kProjectSchemaVersion) + ")");

        migrateToCurrent(j, version);

        LoadedProject out;
        out.fileSchemaVersion = version;
        out.project = j.get<Project>();
        out.unknownKeys = findUnknownKeys(j, Json(out.project));
        return out;
    } catch (const std::exception& e) {
        return err(std::string("project file: ") + e.what());
    }
}

Expected<LoadedProject, Error> load(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) return err("cannot open " + path.string());
    std::ostringstream ss;
    ss << in.rdbuf();
    if (in.bad()) return err("error reading " + path.string());
    return parse(ss.str());
}

std::string serialize(const Project& project) {
    return Json(project).dump(2) + "\n";
}

std::optional<Error> save(const Project& project, const std::filesystem::path& path) {
    std::string text;
    try {
        text = serialize(project);
    } catch (const std::exception& e) {
        return Error{std::string("cannot serialise project: ") + e.what()};
    }

    auto tmp = path;
    tmp += ".tmp";
    {
        std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
        if (!out) return Error{"cannot write " + tmp.string()};
        out << text;
        out.flush();
        if (!out) return Error{"error writing " + tmp.string()};
    }
    std::error_code ec;
    std::filesystem::rename(tmp, path, ec);
    if (ec) {
        std::filesystem::remove(tmp, ec);
        return Error{"cannot replace " + path.string()};
    }
    return std::nullopt;
}

}  // namespace spacer::projectfile
