#ifndef TOPO_JAVA_NATIVE_EVIDENCE_H
#define TOPO_JAVA_NATIVE_EVIDENCE_H
#include "topo/Check/AuditTypes.h"
#include "topo/Build/ArtifactEvidence.h"
namespace topo::check {
// Join an FFM constant path/symbol/descriptor with current artifact bytes and
// a C ABI manifest. This never loads the library or establishes runtime mapping.
AuditNativeBinding verifyJavaNativeBinding(const AuditNativeBinding& binding,
    const build::evidence::Manifest& manifest, const build::evidence::RootMap& roots);
}
#endif
