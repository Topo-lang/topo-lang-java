#ifndef TOPO_CHECK_JAVASYMBOLEXTRACTOR_H
#define TOPO_CHECK_JAVASYMBOLEXTRACTOR_H

#include "topo/Check/SymbolExtractor.h"

#include <string>
#include <vector>

namespace topo::check {

/// Regex-based L1 Java symbol extractor (safety-net fallback).
///
/// Scans Java source files line-by-line with brace-depth tracking to extract
/// class, interface, enum, method, and constructor declarations.  Tracks
/// package declarations for qualified-name construction and access modifiers
/// (public/private/protected/package-private) for visibility mapping.
///
/// This extractor intentionally favours false positives over false negatives:
/// it is a safety net for when the JDT Language Server is unavailable.
class JavaSymbolExtractor : public SymbolExtractor {
public:
    std::vector<HostSymbol> extractSymbols(const std::string& filePath) override;
};

} // namespace topo::check

#endif // TOPO_CHECK_JAVASYMBOLEXTRACTOR_H
