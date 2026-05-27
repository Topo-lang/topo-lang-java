#ifndef TOPO_CHECK_JAVAUNSAFECATALOG_H
#define TOPO_CHECK_JAVAUNSAFECATALOG_H

#include "topo/Check/CapabilityCatalog.h"
#include <string>

namespace topo::check {

/// Java unsafe behavior catalog.
/// Classifies Java patterns by unsafe level.
/// Level 1 (System): java.io, java.nio, java.net, ProcessBuilder
/// Level 2 (Dep): third-party libraries (non java.*/javax.*)
/// Level 3 (Input): servlet requests, SQL statements
/// Level 4 (Escape): reflection, serialization, sun.misc.Unsafe
class JavaUnsafeCatalog {
public:
    /// Classify a call site pattern (method name or qualified call).
    static UnsafeLevel classifyCall(const std::string& pattern);

    /// Classify an import path.
    static UnsafeLevel classifyImport(const std::string& path);
};

} // namespace topo::check

#endif // TOPO_CHECK_JAVAUNSAFECATALOG_H
