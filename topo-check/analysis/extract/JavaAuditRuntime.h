#ifndef TOPO_JAVA_AUDIT_RUNTIME_H
#define TOPO_JAVA_AUDIT_RUNTIME_H
#include "topo/Platform/AnalysisProcess.h"
#include <nlohmann/json.hpp>
namespace topo::check {
struct JavaAuditRuntime {
    std::string executable;
    std::vector<std::string> arguments;
    platform::AnalysisProcessPolicy policy;
    std::map<std::string, std::string> inputDigests;
    std::filesystem::path runtimeRoot;
    std::map<std::string, std::string> runtimeInventory;
};
std::map<std::string, std::string> javaAuditRuntimeInventory(const std::filesystem::path& root);
// Resolve data-only installed metadata. Project model paths grant no ambient
// read authority: external inputs require reviewer-supplied roots.
JavaAuditRuntime resolveJavaAuditRuntime(const std::string& launcher,
                                        const nlohmann::json& request,
                                        const std::vector<std::string>& inputRoots);
} // namespace topo::check
#endif
