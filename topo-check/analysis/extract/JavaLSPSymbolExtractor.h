#ifndef TOPO_CHECK_JAVASPSYMBOLEXTRACTOR_H
#define TOPO_CHECK_JAVASPSYMBOLEXTRACTOR_H

#include "topo/Check/SymbolExtractor.h"

// Forward declaration
namespace topo::lsp { class JdtBridge; }

namespace topo::check {

/// LSP-based Java symbol extractor using JDT semantic tokens and hover.
///
/// Extracts symbols by:
/// 1. Opening the document for JDT analysis
/// 2. Requesting semantic tokens to find declarations/definitions
/// 3. Using hover to resolve qualified names and signatures
///
/// Falls back gracefully: if JDT returns empty tokens for a file,
/// the result is simply empty (caller can fall back to regex extractor).
class JavaLSPSymbolExtractor : public SymbolExtractor {
public:
    explicit JavaLSPSymbolExtractor(lsp::JdtBridge& bridge);

    std::vector<HostSymbol> extractSymbols(const std::string& filePath) override;

private:
    /// Parse return type from a hover signature like "int com.example.App.func(args)".
    static std::string parseReturnType(const std::string& hover);

    /// Parse parameter types from a hover signature.
    static std::vector<std::string> parseParamTypes(const std::string& hover);

    /// Detect enclosing class from a qualified name like "com::example::App::method".
    static std::string detectEnclosingClass(const std::string& qualifiedName);

    lsp::JdtBridge& bridge_;
};

} // namespace topo::check

#endif // TOPO_CHECK_JAVASPSYMBOLEXTRACTOR_H
