#ifndef TOPO_CHECK_JAVACALLEDGEEXTRACTOR_H
#define TOPO_CHECK_JAVACALLEDGEEXTRACTOR_H

#include "topo/Check/CallEdgeExtractor.h"

#include <string>
#include <vector>

namespace topo::check {

/// L1 regex-based Java call edge extractor used by StageIsolationCheck and
/// VisibilityCheck. Scans method bodies for `identifier(...)`, `Type.method(...)`,
/// and `this.method(...)` calls and emits caller→callee edges qualified by the
/// enclosing package + class scope (mirrors JavaCallSiteExtractor's
/// scope-tracking state machine).
class JavaCallEdgeExtractor : public CallEdgeExtractor {
public:
    std::vector<CallEdge> extractCallEdges(const std::string& filePath) override;
};

} // namespace topo::check

#endif // TOPO_CHECK_JAVACALLEDGEEXTRACTOR_H
