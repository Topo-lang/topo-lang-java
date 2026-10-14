#ifndef TOPO_JAVA_BUILD_EVIDENCE_H
#define TOPO_JAVA_BUILD_EVIDENCE_H
#include "JavaDriver.h"
#include <functional>
namespace topo::jvm {
struct JavaEvidenceResult {
    int exitCode = 0;
    std::string path;
    build::evidence::RootMap roots;
    std::vector<std::string> diagnostics;
};
JavaEvidenceResult runJavaStage(const std::string& kind, const std::string& tool,
    const std::vector<std::string>& arguments, const std::vector<std::string>& inputs,
    const std::string& output, bool outputDirectory, bool verbose, const JavaEvidenceOptions* options);
}
#endif
