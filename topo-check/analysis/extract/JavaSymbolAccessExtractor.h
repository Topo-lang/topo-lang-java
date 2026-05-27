#ifndef TOPO_CHECK_JAVASYMBOLACCESSEXTRACTOR_H
#define TOPO_CHECK_JAVASYMBOLACCESSEXTRACTOR_H

#include "topo/Check/SymbolAccessExtractor.h"

#include <string>
#include <vector>

namespace topo::check {

/// L1 regex-based Java symbol access extractor used by PurityCheck.
///
/// Two-pass strategy:
///   1. Scan for `static` field declarations inside class bodies. `final
///      static` constants are excluded (the compiler prevents reassignment,
///      so they cannot be the source of parallel-stage races).
///   2. Inside method bodies, emit SymbolAccess{isWrite=true} for writes to
///      the detected static fields. Writes include simple assignment,
///      compound assignment, and `++`/`--`. Both bare-name access (same-class
///      via implicit `this` resolution rules — but since we excluded instance
///      fields, a bare-name match must be a same-class static) and
///      `ClassName.fieldName` qualified access are recognised.
///
/// Reads are deferred to a later milestone — the load-bearing parallel-purity
/// signal is writes to shared mutable state.
class JavaSymbolAccessExtractor : public SymbolAccessExtractor {
public:
    std::vector<SymbolAccess> extractSymbolAccesses(const std::string& filePath) override;
};

} // namespace topo::check

#endif // TOPO_CHECK_JAVASYMBOLACCESSEXTRACTOR_H
