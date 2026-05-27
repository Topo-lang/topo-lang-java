// JavaLSPCallSiteExtractor -- LSP-based Java call site extraction via JDT.
//
// Uses semantic tokens to find method call references (tokens
// without "declaration"/"definition" modifier), then resolves each
// via hover to get the qualified callee name for JavaUnsafeCatalog classification.
// Converts Java '.' separators to Topo '::' format.

#include "JavaLSPCallSiteExtractor.h"
#include "JavaLSPUtils.h"
#include "JavaUnsafeCatalog.h"
#include "JdtBridge.h"
#include "topo/Check/CapabilityCatalog.h"

#include <string>
#include <vector>

namespace topo::check {

JavaLSPCallSiteExtractor::JavaLSPCallSiteExtractor(lsp::JdtBridge& bridge)
    : bridge_(bridge) {}

std::vector<DetectedCallSite> JavaLSPCallSiteExtractor::extractCallSites(const std::string& filePath) {
    std::vector<DetectedCallSite> results;

    if (!bridge_.isAvailable()) return results;

    // 1. Open document for JDT analysis
    bridge_.openDocument(filePath);
    struct DocGuard {
        lsp::JdtBridge& b;
        const std::string& path;
        ~DocGuard() { b.closeDocument(path); }
    } guard{bridge_, filePath};

    // 2. Get semantic tokens
    auto tokens = bridge_.getSemanticTokens(filePath);
    if (tokens.empty()) {
        return results;
    }

    // 3. Process call reference tokens (method without declaration/definition)
    for (const auto& token : tokens) {
        // Only interested in method references (call sites)
        if (token.type != "method") continue;

        // Skip declarations and definitions -- those are symbol definitions, not calls
        if (hasModifier(token.modifiers, "declaration") ||
            hasModifier(token.modifiers, "definition")) {
            continue;
        }

        // 4. Resolve the call target via hover
        auto hover = bridge_.getHoverAt(filePath, token.line, token.column);
        if (!hover) continue;

        // Extract qualified name from hover response (converts '.' to '::')
        std::string qualifiedName = extractQualifiedName(*hover);
        if (qualifiedName.empty()) continue;

        // 5. Classify via JavaUnsafeCatalog (primary gate)
        // JavaUnsafeCatalog expects dot-separated paths for classifyCall,
        // so convert back to dots for the lookup
        std::string dotName;
        for (size_t i = 0; i < qualifiedName.size(); ++i) {
            if (qualifiedName[i] == ':' && i + 1 < qualifiedName.size() && qualifiedName[i + 1] == ':') {
                dotName += '.';
                ++i; // skip second ':'
            } else {
                dotName += qualifiedName[i];
            }
        }

        auto unsafeLevel = JavaUnsafeCatalog::classifyCall(dotName);
        if (unsafeLevel == UnsafeLevel::Safe) {
            // Also try with just the simple name (unqualified) for broader matching
            auto lastSep = qualifiedName.rfind("::");
            if (lastSep != std::string::npos) {
                std::string simpleName = qualifiedName.substr(lastSep + 2);
                unsafeLevel = JavaUnsafeCatalog::classifyCall(simpleName);
            }
        }

        // Only report non-safe call sites
        if (unsafeLevel == UnsafeLevel::Safe) continue;

        // 6. Classify capability (optional)
        auto capability = classifyApiCall(dotName);
        if (!capability) {
            auto lastSep = qualifiedName.rfind("::");
            if (lastSep != std::string::npos) {
                capability = classifyApiCall(qualifiedName.substr(lastSep + 2));
            }
        }

        // 7. Build DetectedCallSite
        DetectedCallSite site;
        site.calleePattern = qualifiedName;
        site.callerQualifiedName = "<lsp:" + filePath + ":" + std::to_string(token.line + 1) + ">";
        site.capability = capability;
        site.unsafeLevel = unsafeLevel;
        site.file = filePath;
        site.line = token.line + 1; // semantic tokens are 0-based, diagnostics are 1-based
        results.push_back(std::move(site));
    }

    return results;
}

} // namespace topo::check
