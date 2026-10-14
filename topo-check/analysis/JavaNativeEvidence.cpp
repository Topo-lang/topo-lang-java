#include "JavaNativeEvidence.h"
#include <algorithm>
#include <cstdint>
#include <fstream>
#include <map>
#include <set>

namespace topo::check {
namespace fs = std::filesystem;
namespace ev = build::evidence;
namespace {
std::string layout(const std::string& type) {
    static const std::map<std::string, std::string> types = {
        {"void", "void"}, {"bool", "JAVA_BOOLEAN"}, {"_Bool", "JAVA_BOOLEAN"},
        {"signed char", "JAVA_BYTE"}, {"short", "JAVA_SHORT"}, {"int", "JAVA_INT"},
        {"long", "JAVA_LONG"}, {"long long", "JAVA_LONG"}, {"float", "JAVA_FLOAT"},
        {"double", "JAVA_DOUBLE"}, {"void *", "ADDRESS"}, {"const void *", "ADDRESS"}};
    auto found = types.find(type); return found == types.end() ? "" : found->second;
}
// Inspect supported Mach-O bytes directly: no dlopen, constructors or tools.
bool arm64Export(const fs::path& path, const std::string& symbol, std::string& error) {
    std::ifstream input(path, std::ios::binary);
    if (!input) { error = "Native library cannot be read"; return false; }
    input.seekg(0, std::ios::end); auto length = input.tellg();
    if (length < 32 || length > 512 * 1024 * 1024) { error = "Native binary is outside the supported size range"; return false; }
    input.seekg(0); std::vector<unsigned char> bytes(static_cast<std::size_t>(length));
    input.read(reinterpret_cast<char*>(bytes.data()), length);
    auto word = [&](std::size_t offset) -> std::uint32_t {
        if (offset + 4 > bytes.size()) return 0;
        return std::uint32_t(bytes[offset]) | std::uint32_t(bytes[offset+1]) << 8 |
            std::uint32_t(bytes[offset+2]) << 16 | std::uint32_t(bytes[offset+3]) << 24;
    };
    if (word(0) != 0xfeedfacf || word(4) != 0x0100000c || word(12) != 6) {
        error = "Native inspection supports macOS ARM64 Mach-O dylibs with direct C ABI only"; return false;
    }
    if (symbol.empty()) return true;
    std::size_t cursor = 32, commandEnd = 32ull + word(20);
    if (commandEnd > bytes.size()) { error = "Invalid Mach-O load-command extent"; return false; }
    for (std::uint32_t i = 0; i < word(16); ++i) {
        if (cursor + 8 > commandEnd || word(cursor+4) < 8 || cursor + word(cursor+4) > commandEnd) {
            error = "Invalid Mach-O load command"; return false;
        }
        if (word(cursor) == 2 && word(cursor+4) >= 24) {
            std::size_t symbols = word(cursor+8), count = word(cursor+12), strings = word(cursor+16), size = word(cursor+20);
            if (symbols + count * 16ull > bytes.size() || strings + size > bytes.size()) {
                error = "Invalid Mach-O symbol table"; return false;
            }
            for (std::size_t n = 0; n < count; ++n) {
                std::size_t entry = symbols + n * 16, name = word(entry);
                unsigned char type = bytes[entry+4];
                if (name >= size || (type & 0xe0) || !(type & 1) || (type & 0x0e) != 0x0e) continue;
                auto first = bytes.begin() + strings + name;
                auto end = std::find(first, bytes.begin() + strings + size, 0);
                if (end != bytes.begin() + strings + size && std::string(first, end) == "_" + symbol) return true;
            }
        }
        cursor += word(cursor+4);
    }
    error = "Native symbol is absent from the actual dylib export symbol table: " + symbol; return false;
}
}
AuditNativeBinding verifyJavaNativeBinding(const AuditNativeBinding& binding,
    const ev::Manifest& manifest, const ev::RootMap& roots) {
    AuditNativeBinding out = binding;
    out.staticBinding = false; out.runtimeMapping = false; out.provenanceStatus = "unverified";
    auto fail = [&](const std::string& reason) { out.diagnostics.push_back(reason); return out; };
    if (!binding.resolved || binding.lookup != "library-path" || binding.library.empty())
        return fail("Native provenance needs a fixed library path");
    if (!fs::path(binding.library).is_absolute()) return fail("Relative FFM library path requires an observed runtime working directory");
    auto verification = ev::verifyManifest(manifest, roots);
    if (!verification.integrity) {
        out.provenanceStatus = "mismatch";
        out.diagnostics = verification.diagnostics;
        return out;
    }
    std::error_code ec; auto library = fs::canonical(binding.library, ec);
    if (ec) return fail("FFM library path does not name an existing artifact");
    const ev::Record* selected = nullptr;
    for (const auto& record : manifest.nodes) for (const auto& output : record.outputs) {
        auto root = roots.find(output.root); if (root == roots.end()) continue;
        auto file = fs::canonical(root->second / output.path, ec);
        if (!ec && file == library) {
            if (selected && selected->id != record.id) return fail("Native library has ambiguous producing records");
            selected = &record;
        }
    }
    if (!selected) { out.provenanceStatus = "mismatch"; return fail("FFM library differs from the manifest's actual output path"); }
    if (binding.symbol.empty()) {
        std::string error;
        if (!arm64Export(library, "", error)) { out.provenanceStatus = "mismatch"; return fail(error); }
        out.staticBinding = true; out.provenanceStatus = "library-bytes-only";
        out.evidenceRecords.push_back(selected->id);
        out.diagnostics.push_back("Library path and bytes match; native initializers and runtime loading still require review");
        return out;
    }
    try {
        const auto& native = selected->action.parameters.at("native");
        if (native.at("abi") != "c" || native.at("targetTriple") != "arm64-apple-darwin" || native.at("binaryKind") != "dylib")
            return fail("Unsupported native ABI, target triple or binary kind");
        int matches = 0;
        std::string expected;
        for (const auto& entry : native.at("exports")) if (entry.at("name") == binding.symbol) {
            ++matches; expected = layout(entry.at("returnType").get<std::string>());
            if (expected.empty()) return fail("Native return type has no supported FFM ABI mapping");
            expected += "("; bool first = true;
            for (const auto& parameter : entry.at("parameterTypes")) {
                auto value = layout(parameter.get<std::string>());
                if (value.empty() || value == "void") return fail("Native parameter has no supported FFM ABI mapping");
                if (!first) expected += ",";
                first = false; expected += value;
            }
            expected += ")";
        }
        if (matches != 1) { out.provenanceStatus = "mismatch"; return fail("Native symbol is absent or ambiguous in C ABI evidence"); }
        if (!binding.descriptor.empty() && expected != binding.descriptor) {
            out.provenanceStatus = "mismatch"; return fail("FFM descriptor does not match the native C ABI: expected " + expected);
        }
        std::string error;
        if (!arm64Export(library, binding.symbol, error)) { out.provenanceStatus = "mismatch"; return fail(error); }
    } catch (const std::exception& error) { return fail(std::string("Native manifest lacks supported ABI evidence: ") + error.what()); }
    out.staticBinding = true;
    out.provenanceStatus = binding.descriptor.empty() ? "symbol-bytes-only" : "static-binding-only";
    out.evidenceRecords.push_back(selected->id);
    out.diagnostics.push_back("C ABI metadata and current library bytes agree; producer authentication and runtime-loaded mapping remain unverified");
    return out;
}
}
