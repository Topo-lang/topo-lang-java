#ifndef TOPO_CHECK_JAVASTUBGENERATOR_H
#define TOPO_CHECK_JAVASTUBGENERATOR_H

#include "topo/Check/StubGenerator.h"
#include <string>

namespace topo::check {

/// Java implementation of StubGenerator.
/// Finds method definitions in Java source files by name matching,
/// then replaces the body using brace-balancing.
class JavaStubGenerator : public StubGenerator {
public:
    StubResult stubFunction(const std::string& filePath, const std::string& funcName) override;
    bool restoreFile(const std::string& filePath, const StubResult& result) override;

    /// Find the position of a method body in Java source text.
    static size_t findMethodBodyStart(const std::string& source, const std::string& methodName);
    /// Find matching closing '}' using brace-balancing.
    static size_t findMatchingBrace(const std::string& source, size_t openPos);
    /// Determine return type: void, boolean, primitive, or object.
    static bool isVoidReturn(const std::string& source, size_t bodyStart);
    static bool isBooleanReturn(const std::string& source, size_t bodyStart);
    static bool isPrimitiveReturn(const std::string& source, size_t bodyStart);
};

} // namespace topo::check
#endif
