#include "JavaSafePatterns.h"

#define TOML_HEADER_ONLY 1
#define TOML_EXCEPTIONS 0
#include <toml++/toml.hpp>

#include <filesystem>
#include <iostream>

namespace fs = std::filesystem;

namespace topo::check {

bool JavaSafePatterns::load(const std::string& tomlPath) {
    toml::parse_result result = toml::parse_file(tomlPath);
    if (!result) {
        std::cerr << "JavaSafePatterns: failed to parse " << tomlPath << ": "
                  << result.error() << "\n";
        return false;
    }
    const auto& tbl = result.table();

    // [constructs].safe
    if (auto arr = tbl.at_path("constructs.safe").as_array()) {
        for (const auto& elem : *arr) {
            if (auto s = elem.value<std::string>()) safeConstructs_.insert(*s);
        }
    }
    // [constructs].unsafe
    if (auto arr = tbl.at_path("constructs.unsafe").as_array()) {
        for (const auto& elem : *arr) {
            if (auto s = elem.value<std::string>()) unsafeConstructs_.insert(*s);
        }
    }
    // [stdlib] -- iterate key-value pairs where value is "safe"
    if (auto stdlibTbl = tbl["stdlib"].as_table()) {
        for (const auto& [key, val] : *stdlibTbl) {
            if (auto s = val.value<std::string>(); s && *s == "safe") {
                safeStdlib_.insert(std::string(key.str()));
            }
        }
    }

    loaded_ = true;
    return true;
}

bool JavaSafePatterns::loadDefault() {
    // Try environment variable first
    if (const char* dir = std::getenv("TOPO_PATTERNS_DIR")) {
        fs::path p = fs::path(dir) / "JavaSafePatterns.toml";
        if (fs::exists(p)) return load(p.string());
    }
    // For development, try the source tree location.
    // The catalog lives under topo-check/analysis/catalog/ in the current
    // layout (per the topo-<tool>/ subdirectory convention documented in
    // the project README); the legacy analysis/catalog/ path is kept as a
    // fallback so older checkouts still resolve.
    fs::path candidates[] = {
        fs::path(TOPO_SOURCE_DIR) / "topo-lang-java" / "topo-check" / "analysis" / "catalog" / "JavaSafePatterns.toml",
        fs::path(TOPO_SOURCE_DIR) / "topo-lang-java" / "analysis" / "catalog" / "JavaSafePatterns.toml",
    };
    for (const auto& p : candidates) {
        if (fs::exists(p)) return load(p.string());
    }
    return false;
}

bool JavaSafePatterns::isConstructSafe(const std::string& keyword) const {
    return safeConstructs_.count(keyword) > 0;
}

bool JavaSafePatterns::isConstructUnsafe(const std::string& keyword) const {
    return unsafeConstructs_.count(keyword) > 0;
}

bool JavaSafePatterns::isStdlibSymbolSafe(const std::string& qualifiedName) const {
    // Exact match first
    if (safeStdlib_.count(qualifiedName)) return true;
    // Try prefix match: "java::util::List::add" -> check "java::util::List"
    // Members of safe types are safe
    auto pos = qualifiedName.rfind("::");
    while (pos != std::string::npos && pos > 0) {
        std::string prefix = qualifiedName.substr(0, pos);
        if (safeStdlib_.count(prefix)) return true;
        pos = prefix.rfind("::");
    }
    return false;
}

} // namespace topo::check
