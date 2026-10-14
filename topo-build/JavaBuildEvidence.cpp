#include "JavaBuildEvidence.h"
#include "topo/Build/ExecutionEvidence.h"
#include "topo/Platform/Process.h"
#include "topo/Platform/ToolResolution.h"
#include <algorithm>
#include <cstdlib>
#include <set>

namespace topo::jvm {
namespace fs = std::filesystem;
namespace ev = build::evidence;
JavaEvidenceResult runJavaStage(const std::string& kind, const std::string& tool,
    const std::vector<std::string>& arguments, const std::vector<std::string>& inputs,
    const std::string& output, bool outputDirectory, bool verbose, const JavaEvidenceOptions* options) {
    JavaEvidenceResult result;
    result.path = options && !options->manifestPath.empty() ? options->manifestPath : output + ".topo-evidence.json";
    result.roots = options ? options->roots : ev::RootMap{};
    auto executable = platform::findSpawnableOnPath(tool);
    if (executable.empty()) executable = tool;
    std::vector<std::string> actualInputs = inputs;
    if (options) actualInputs.insert(actualInputs.end(), options->extraInputs.begin(), options->extraInputs.end());
    actualInputs.push_back(executable);
    fs::path jdk = fs::path(executable).parent_path().parent_path();
    for (const auto& relative : {"release", "lib/modules"})
        if (fs::is_regular_file(jdk / relative)) actualInputs.push_back((jdk / relative).string());
    if (!options) {
        std::vector<fs::path> candidates = {fs::absolute(output).parent_path(), fs::current_path()};
        for (const auto& input : actualInputs)
            candidates.push_back(fs::is_directory(input) ? fs::path(input) : fs::absolute(input).parent_path());
        std::set<std::string> unique;
        for (const auto& candidate : candidates) {
            std::error_code ec; auto root = fs::weakly_canonical(candidate, ec);
            if (!ec && unique.insert(root.string()).second)
                result.roots.emplace("local-" + std::to_string(result.roots.size()), root);
        }
    }
    ev::Manifest manifest;
    ev::Record record;
    if (options) for (const auto& parent : options->parents) {
        auto verification = ev::verifyManifest(parent, result.roots);
        if (!verification.integrity) {
            result.diagnostics.push_back("Parent Java stage bytes changed; provenance link remains unverified");
            result.diagnostics.insert(result.diagnostics.end(), verification.diagnostics.begin(), verification.diagnostics.end());
        } else {
            for (const auto& node : parent.nodes)
                if (std::none_of(manifest.nodes.begin(), manifest.nodes.end(), [&](const auto& existing) {return existing.id == node.id;}))
                    manifest.nodes.push_back(node);
            record.parents.insert(record.parents.end(), parent.roots.begin(), parent.roots.end());
        }
    }
    ev::ExecutionRecorder recorder(result.roots, result.path, "topo-java-driver");
    auto process = recorder.run(kind, executable, arguments, actualInputs, {output}, verbose);
    result.exitCode = process.exitCode;
    auto captured = recorder.manifest();
    auto diagnostics = recorder.diagnostics();
    result.diagnostics.insert(result.diagnostics.end(), diagnostics.begin(), diagnostics.end());
    if (captured.nodes.empty()) {
        result.diagnostics.push_back("Java stage produced no encodable execution record");
        return result;
    }
    auto parents = record.parents;
    record = captured.nodes.back();
    record.parents = std::move(parents);
    record.action.parameters["outputKind"] = outputDirectory ? "directory" : "file";
    if (!result.diagnostics.empty()) record.producer["captureDiagnostics"] = nlohmann::json(result.diagnostics).dump();
    ev::sealRecord(record);
    manifest.nodes.push_back(record); manifest.roots = {record.id};
    std::string error;
    if (!ev::writeManifest(result.path, manifest, error)) result.diagnostics.push_back(error);
    return result;
}
}
