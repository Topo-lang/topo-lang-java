#ifndef TOPO_TRANSPILE_JAVAEMITTER_H
#define TOPO_TRANSPILE_JAVAEMITTER_H

#include "topo/Transpile/Emitter.h"
#include "topo/Sema/TypeBinder.h"

namespace topo::transpile {

class JavaEmitter : public Emitter {
public:
    explicit JavaEmitter(TypeBinder binder = TypeBinder::createDefault(HostLanguage::Java));
    EmitResult emit(const TranspileModule& module) override;

private:
    TypeBinder binder_;

    std::string emitType(const TypeNode& type);
    std::string emitExpr(const Expr& expr);
    std::string emitStmt(const Stmt& stmt, int indent);
    std::string emitFunction(const TranspileFunction& func, bool inClass = false);
    std::string emitStruct(const TranspileType& type);
    std::string emitOwnership(const TypeNode& type);
};

} // namespace topo::transpile

#endif // TOPO_TRANSPILE_JAVAEMITTER_H
