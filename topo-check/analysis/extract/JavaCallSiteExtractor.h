#ifndef TOPO_CHECK_JAVACALLSITEEXTRACTOR_H
#define TOPO_CHECK_JAVACALLSITEEXTRACTOR_H

#include "topo/Check/CallSiteExtractor.h"

#include <string>
#include <vector>

namespace topo::check {

/// Extracts external API call sites from Java source files using
/// regex-based scanning with scope tracking (package, class, method).
class JavaCallSiteExtractor : public CallSiteExtractor {
public:
    /// Extract call sites matching the CapabilityCatalog API list from a single file.
    std::vector<DetectedCallSite> extractCallSites(const std::string& filePath) override;
};

} // namespace topo::check

#endif // TOPO_CHECK_JAVACALLSITEEXTRACTOR_H
