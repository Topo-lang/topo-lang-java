#ifndef TOPO_LSP_JDTBRIDGE_H
#define TOPO_LSP_JDTBRIDGE_H

#include "topo/LSP/LSPBridge.h"

namespace topo::lsp {

class JdtBridge : public LSPBridge {
public:
    JdtBridge();
    ~JdtBridge() override;
    bool start(const std::string& rootUri) override;
    std::string displayName() const override { return "Java"; }

    bool start(const std::string& jdtPath, const std::string& rootUri);
    std::optional<SymbolResult> findDefinition(const std::string& qualifiedName,
                                               const std::vector<std::string>& javaFiles) override;
    std::vector<SymbolResult> findReferences(const std::string& qualifiedName,
                                             const std::vector<std::string>& javaFiles) override;
    std::optional<std::string> getHoverInfo(const std::string& qualifiedName,
                                            const std::vector<std::string>& javaFiles) override;

    /// Find host-language type definition for a named type.
    /// Queries JDT workspace index first; falls back to scanning sourceFiles
    /// (.java) for class/interface/enum/record declarations matching
    /// typeName.
    std::optional<SymbolResult> findTypeDefinition(const std::string& typeName,
                                                   const std::vector<std::string>& sourceFiles,
                                                   const std::vector<std::string>& includeDirs) override;

    std::string languageId() const override { return "java"; }

private:
    /// Per-project jdtls workspace data directory. Tracked so the
    /// destructor can clean it up (it is created under
    /// ~/Library/Caches/jdtls/ on macOS, ~/.cache/jdtls/ on Linux, and a
    /// hashed-name dir leaks across runs otherwise). Set the
    /// `TOPO_KEEP_JDTLS_WORKSPACE` env var to preserve it for debugging.
    std::string workspaceDataDir_;
};

} // namespace topo::lsp
#endif
