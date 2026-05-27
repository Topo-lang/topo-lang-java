#include "JavaEmitter.h"
#include "topo/Stdlib/Types.h"
#include <cctype>
#include <functional>
#include <map>

namespace topo::transpile {

// Java boxing for stdlib generic parameters.
//
// Java generics cannot take primitive type arguments (no `Optional<long>`,
// no `List<double>`). When a stdlib parameterized type wraps a primitive T,
// the emitter must produce the boxed wrapper class instead.
//
// Used by the `isStdlib()` branch in emitType() for the type-parameter slot
// of optional<T> and slice<T>. Non-primitive type names pass through
// unchanged (already valid Java reference types).
static std::string boxedName(const std::string& javaType) {
    if (javaType == "boolean") return "Boolean";
    if (javaType == "long")    return "Long";
    if (javaType == "double")  return "Double";
    if (javaType == "int")     return "Integer";
    if (javaType == "short")   return "Short";
    if (javaType == "byte")    return "Byte";
    if (javaType == "float")   return "Float";
    if (javaType == "char")    return "Character";
    return javaType;
}

static std::string ind(int level) {
    return std::string(level * 4, ' ');
}

static std::string fidelityComment(Fidelity f, int level) {
    if (f == Fidelity::Recovered) return ind(level) + "// [recovered]\n";
    if (f == Fidelity::Inferred) return ind(level) + "// [inferred]\n";
    return "";
}

static std::string binaryOpStr(BinaryOp op) {
    switch (op) {
    case BinaryOp::Add: return "+";
    case BinaryOp::Sub: return "-";
    case BinaryOp::Mul: return "*";
    case BinaryOp::Div: return "/";
    case BinaryOp::Mod: return "%";
    case BinaryOp::Eq: return "==";
    case BinaryOp::NotEq: return "!=";
    case BinaryOp::Less: return "<";
    case BinaryOp::Greater: return ">";
    case BinaryOp::LessEq: return "<=";
    case BinaryOp::GreaterEq: return ">=";
    case BinaryOp::And: return "&&";
    case BinaryOp::Or: return "||";
    case BinaryOp::BitAnd: return "&";
    case BinaryOp::BitOr: return "|";
    case BinaryOp::BitXor: return "^";
    case BinaryOp::Shl: return "<<";
    case BinaryOp::Shr: return ">>";
    }
    return "??";
}

static std::string mapConcreteType(const std::string& name) {
    // Rust / C++ integer types -> Java
    if (name == "i32" || name == "int32_t" || name == "int") return "int";
    if (name == "i64" || name == "int64_t" || name == "long" || name == "long long") return "long";
    if (name == "i16" || name == "int16_t" || name == "short") return "short";
    if (name == "i8" || name == "int8_t") return "byte";
    if (name == "u32" || name == "uint32_t" || name == "unsigned" || name == "unsigned int") return "int";
    if (name == "u64" || name == "uint64_t" || name == "size_t" || name == "usize") return "long";
    if (name == "u16" || name == "uint16_t") return "int";
    if (name == "u8" || name == "uint8_t") return "int";
    // Float types
    if (name == "f64" || name == "double" || name == "float64") return "double";
    if (name == "f32" || name == "float" || name == "float32") return "float";
    // Boolean
    if (name == "bool") return "boolean";
    if (name == "boolean") return "boolean";
    // String
    if (name == "string" || name == "str" || name == "String") return "String";
    // Void
    if (name == "void" || name == "Void") return "void";
    return "";
}

static std::string mapContainerType(const std::string& name) {
    if (name == "vector" || name == "Vec" || name == "list") return "List";
    if (name == "optional" || name == "Option") return "Optional";
    if (name == "unordered_map" || name == "map" || name == "HashMap" || name == "dict") return "Map";
    if (name == "unordered_set" || name == "set" || name == "HashSet") return "Set";
    return "";
}

static std::pair<std::string, std::string> splitQualifiedName(const std::string& qname) {
    auto pos = qname.rfind("::");
    if (pos == std::string::npos)
        return {"", qname};
    return {qname.substr(0, pos), qname.substr(pos + 2)};
}

// Render `<T, U extends Bound>` or, for intersection bounds, `<T extends
// A & B>`. Empty ⇒ "" ⇒ caller emits exactly its pre-generics output
// (byte-identical to pre-generics for non-generic decls). A type parameter
// renders bare when constraintType is empty; with a single bound it
// renders as `T extends Bound`; with extraBounds populated it renders as
// `T extends A & B & C` (Java intersection syntax). Bound rendering goes
// through the caller's `emitType` so generic bounds like `T extends
// Comparable<T>` surface their template args.
// Java has no equivalent to Rust associated-type bindings
// (`Iterator<Item = u8>`); the closest surface form (`Iterable<T>` plus a
// covariant subtype) is a different design decision and emitting it
// silently would widen the contract. We DROP the bindings and append an
// inline `/* TOPO-TRANSPILE: ... */` block comment after the bound so the
// human-facing diff makes the loss visible. Block comments are legal
// inside `<T extends Iterator /* ... */>` in Java; verified by javac.
static bool typeNodeHasAssocBindings(const TypeNode& t) {
    return !t.assocBindings.empty();
}
static std::string assocBindingDropNote(const std::string& paramName) {
    return " /* TOPO-TRANSPILE: associated-type bindings on " + paramName +
           " dropped (no Java equivalent) */";
}

// True iff this bound TypeNode is actually a Rust lifetime (`'a`-style)
// piggy-backed on the wire — Java has no analogue, so it is silently
// dropped from any bound list. No comment because lifetime entries are
// noise for non-Rust hosts.
static bool javaIsWireLifetimeBound(const TypeNode& t) {
    return !t.nameParts.empty() && !t.nameParts[0].empty() &&
           t.nameParts[0][0] == '\'';
}

// True iff this bound TypeNode is a positional `union<...>` — the wire
// shape a Python TypeVar constraint-tuple lowers to. Java has no anonymous
// untagged-union type usable as a generic bound, so it is dropped with a
// visible downgrade note.
static bool javaIsPositionalUnionBound(const TypeNode& t) {
    return t.nameParts.size() == 1 && t.nameParts[0] == "union";
}
static std::string unionBoundDropNote(const std::string& paramName) {
    return " /* TOPO-TRANSPILE: union<...> bound on " + paramName +
           " dropped (no Java untagged-union generic bound) */";
}

static std::string genericClauseImpl(const std::vector<TemplateParamDecl>& tpsIn,
                                     const std::function<std::string(const TypeNode&)>& renderType) {
    // Filter out kind=Lifetime entries up front; they have no Java
    // equivalent.
    std::vector<TemplateParamDecl> tps;
    tps.reserve(tpsIn.size());
    for (const auto& p : tpsIn) {
        if (p.kind == TemplateParamDecl::LifetimeParam) continue;
        TemplateParamDecl q = p;
        // Strip Rust lifetime bound entries from the bound list. If the
        // primary `constraintType` was a lifetime, promote the first
        // non-lifetime extraBound to primary.
        if (q.kind == TemplateParamDecl::TypeParam &&
            !q.constraintType.nameParts.empty() &&
            javaIsWireLifetimeBound(q.constraintType)) {
            std::vector<TypeNode> kept;
            for (const auto& eb : q.extraBounds)
                if (!javaIsWireLifetimeBound(eb)) kept.push_back(eb);
            if (kept.empty()) {
                q.constraintType = TypeNode{};
                q.extraBounds.clear();
            } else {
                q.constraintType = kept.front();
                q.extraBounds.assign(kept.begin() + 1, kept.end());
            }
        } else if (q.kind == TemplateParamDecl::TypeParam) {
            std::vector<TypeNode> kept;
            for (const auto& eb : q.extraBounds)
                if (!javaIsWireLifetimeBound(eb)) kept.push_back(eb);
            q.extraBounds = std::move(kept);
        }
        tps.push_back(std::move(q));
    }
    if (tps.empty()) return "";
    std::string s = "<";
    for (size_t i = 0; i < tps.size(); ++i) {
        if (i > 0) s += ", ";
        s += tps[i].name;
        if (tps[i].kind == TemplateParamDecl::TypeParam &&
            !tps[i].constraintType.nameParts.empty()) {
            // Collect renderable bounds; a positional union<...> bound
            // (Python TypeVar constraint-tuple) has no Java generic-bound
            // form, so drop it with a downgrade note.
            std::vector<const TypeNode*> bounds;
            bool unionDropped = false;
            if (javaIsPositionalUnionBound(tps[i].constraintType)) unionDropped = true;
            else bounds.push_back(&tps[i].constraintType);
            for (const auto& eb : tps[i].extraBounds) {
                if (javaIsPositionalUnionBound(eb)) unionDropped = true;
                else bounds.push_back(&eb);
            }
            bool anyAssoc = false;
            for (size_t b = 0; b < bounds.size(); ++b) {
                s += (b == 0 ? " extends " : " & ") + renderType(*bounds[b]);
                if (typeNodeHasAssocBindings(*bounds[b])) anyAssoc = true;
            }
            if (anyAssoc) s += assocBindingDropNote(tps[i].name);
            if (unionDropped) s += unionBoundDropNote(tps[i].name);
        }
    }
    s += ">";
    return s;
}

static std::string capitalize(const std::string& s) {
    if (s.empty()) return s;
    std::string result = s;
    result[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(result[0])));
    return result;
}

JavaEmitter::JavaEmitter(TypeBinder binder) : binder_(std::move(binder)) {}

EmitResult JavaEmitter::emit(const TranspileModule& module) {
    EmitResult result;

    struct NsGroup {
        std::vector<const TranspileType*> types;
        std::vector<const TranspileFunction*> functions;
    };
    std::map<std::string, NsGroup> groups;

    for (const auto& t : module.types) {
        auto [ns, _] = splitQualifiedName(t.qualifiedName);
        groups[ns].types.push_back(&t);
    }
    for (const auto& f : module.functions) {
        auto [ns, _] = splitQualifiedName(f.qualifiedName);
        groups[ns].functions.push_back(&f);
    }

    for (const auto& [ns, group] : groups) {
        if (!ns.empty()) {
            // Use capitalized last namespace part as class name
            auto lastSep = ns.rfind("::");
            std::string className = capitalize(lastSep == std::string::npos ? ns : ns.substr(lastSep + 2));
            result.code += "class " + className + " {\n";
        }

        for (const auto* t : group.types)
            result.code += emitStruct(*t) + "\n";
        for (const auto* f : group.functions)
            result.code += emitFunction(*f, !ns.empty()) + "\n";

        if (!ns.empty())
            result.code += "}\n";
    }

    return result;
}

std::string JavaEmitter::emitOwnership(const TypeNode& type) {
    // Copy-and-mutate, not positional reconstruction: a positional
    // TypeNode{...} silently drops any field not listed (stdlibId,
    // recordFields), so `owned slice<T>` / `owned record<...>` would lose
    // their stdlib identity through the ownership path.
    TypeNode bare = type;
    bare.ownership = OwnershipKind::None;
    bare.modifier = TypeNode::None;
    std::string inner = emitType(bare);

    switch (type.ownership) {
    case OwnershipKind::Weak: return "WeakReference<" + inner + ">";
    case OwnershipKind::Owned:
    case OwnershipKind::Shared:
        // Java has no ownership semantics; owned/shared map to plain T
        return inner;
    case OwnershipKind::None: break;
    }
    return inner;
}

std::string JavaEmitter::emitType(const TypeNode& type) {
    if (type.ownership != OwnershipKind::None) return emitOwnership(type);

    // Stdlib bridging types take priority over the legacy
    // primitive / container name lookups. Parser sets both `stdlibId` and
    // `nameParts={"i64"}` / `{"string"}` / etc. on stdlib uses; without this
    // branch running first, `string` would flow through mapConcreteType and
    // resolve identically by accident, but `optional<i64>` would lower to the
    // legacy `Optional<long>` (invalid — Java generics reject primitives).
    //
    // The Java boundary mappings are:
    //   bool        -> boolean
    //   i64         -> long
    //   f64         -> double
    //   string      -> String     (UTF-16 internally; codegen for boundary
    //                              UTF-8↔UTF-16 transcoding is deferred — the
    //                              `dev.topo.StringBoundary` runtime helper
    //                              is callable today for explicit boundaries)
    //   optional<T> -> T?         (no syntactic form in Java; codegen emits
    //                              a boxed reference whose `null` denotes
    //                              absent; primitive T is boxed via boxedName)
    //   slice<T>    -> List<T>    (conservative; see commit message — we pick
    //                              List<Boxed<T>> over Java-22+ MemorySegment
    //                              to avoid a Panama dependency in Batch 1.
    //                              Future revision can specialize numeric T to
    //                              MemorySegment for performance.)
    //   bytes       -> List<Byte> (slice<u8>-isomorphic — emits exactly what
    //                              slice<u8> emits so the two are boundary-
    //                              interchangeable; not a new host type)
    //   array<T,N>  -> T[]        (Java has no value fixed-size array; element
    //                              recursion mirrors slice<T>, and the fixed
    //                              length N is carried in an inline comment as
    //                              a boundary contract)
    if (type.isStdlib()) {
        switch (type.stdlibId) {
        case stdlib::TypeId::Bool:   return "boolean";
        case stdlib::TypeId::I64:    return "long";
        case stdlib::TypeId::TimeNs: return "long"; // ns since epoch, i64-isomorphic
        case stdlib::TypeId::Uuid:   return "java.util.UUID"; // native 16-byte (two longs); fully-qualified, no import
        case stdlib::TypeId::Decimal128: return "byte[]"; // 16-byte IEEE 754-2008 buffer (no fixed-layout native decimal)
        case stdlib::TypeId::F64:    return "double";
        case stdlib::TypeId::String: return "String";
        case stdlib::TypeId::Optional: {
            if (type.templateArgs.empty()) return "Object"; // defensive; Sema rejects optional<> upstream
            // No `T?` syntax in Java; emit boxed reference so `null` can encode absence.
            return boxedName(emitType(type.templateArgs[0]));
        }
        case stdlib::TypeId::Slice: {
            if (type.templateArgs.empty()) return "java.util.List<Object>"; // defensive
            // List<Boxed<T>>: works on all JDKs, avoids Java 22+ FFM dependency.
            return "java.util.List<" + boxedName(emitType(type.templateArgs[0])) + ">";
        }
        case stdlib::TypeId::Bytes: {
            // `bytes` is slice<u8>-isomorphic ({16,8}, non-owning byte view).
            // Emit exactly what slice<u8> emits in this emitter so the two
            // forms are interchangeable at the boundary: u8 -> byte ->
            // boxed Byte, slice<T> -> java.util.List<Boxed<T>>. We compose the
            // u8 element through the same boxedName path slice uses rather
            // than inventing a distinct host type.
            TypeNode u8;
            u8.nameParts = {stdlib::keywordOf(stdlib::TypeId::U8)};
            u8.stdlibId = stdlib::TypeId::U8;
            return "java.util.List<" + boxedName(emitType(u8)) + ">";
        }
        case stdlib::TypeId::Array: {
            // array<T, N>: fixed-length inline buffer of N contiguous T.
            // Java has no value-typed fixed-size array, so map to the host
            // array `T[]` (which carries the element-type recursion exactly
            // like slice<T>) and record the fixed-length-N contract in an
            // inline comment — Java arrays are runtime-length, so N is a
            // boundary contract the host code must honour, not a type the
            // language can express. Byte-layout contract:
            // N * align_up(sizeof(T), align(T)) at align(T).
            if (type.templateArgs.empty()) return "Object[]"; // defensive; Sema rejects array<> upstream
            const std::string elem = emitType(type.templateArgs[0]);
            std::string n = "?";
            if (type.templateArgs.size() >= 2 &&
                type.templateArgs[1].nonTypeValue.has_value()) {
                n = std::to_string(*type.templateArgs[1].nonTypeValue);
            }
            return elem + "[] /* array<" + elem + ", " + n +
                   ">: fixed-length N=" + n + " */";
        }
        // Java has no native unsigned integer or u8/u32/u64;
        // emit the same-width signed primitive and rely on JEP-394-style
        // Long.toUnsignedString etc. on the host side when bit-pattern unsignedness
        // matters. f32 → Java `float`.
        case stdlib::TypeId::U8:     return "byte";   // 8-bit signed; reinterpret as unsigned per protocol
        case stdlib::TypeId::I32:    return "int";
        case stdlib::TypeId::U32:    return "int";    // 32-bit signed; protocol-level unsigned
        case stdlib::TypeId::U64:    return "long";   // 64-bit signed; protocol-level unsigned
        case stdlib::TypeId::F32:    return "float";
        case stdlib::TypeId::I8:     return "byte";   // 8-bit signed
        case stdlib::TypeId::I16:    return "short";  // 16-bit signed
        case stdlib::TypeId::U16:    return "short";  // 16-bit signed; protocol-level unsigned
        case stdlib::TypeId::Record: {
            // record<f1: T1, ...>: an ordered heterogeneous aggregate. Java
            // has no inline tuple type and emitType returns a type
            // expression (cannot synthesise a top-level record class here),
            // so — exactly as the Array case degrades to `T[]` + an inline
            // contract comment — map to `Object[]` and document the ordered
            // field types in a comment. Field order is the load-bearing
            // cross-language byte contract; names live in the .topo decl.
            const auto& fields = type.recordFields;
            if (fields.empty()) return "Object[]"; // defensive; Sema rejects record<> upstream
            std::string types;
            for (size_t i = 0; i < fields.size(); ++i) {
                if (i > 0) types += ", ";
                types += emitType(fields[i].type());
            }
            return "Object[] /* record<" + types + ">: ordered field types */";
        }
        case stdlib::TypeId::Union: {
            // union<tag: TagT, v1: T1, ...>: Java has no anonymous
            // tagged-union or inline tuple type, so — exactly as record
            // degrades to Object[] — the boundary shape is an `Object[]`
            // carrying [tag, selected-variant] positionally. The .topo
            // declaration is the authority for field names, the tag type,
            // and the variant-overlap byte layout (only one variant occupies
            // the shared storage at a time, selected by the tag).
            const auto& fields = type.recordFields;
            if (fields.empty()) return "Object[]"; // defensive; Sema rejects upstream
            std::string types;
            for (size_t i = 0; i < fields.size(); ++i) {
                if (i > 0) types += ", ";
                types += emitType(fields[i].type());
            }
            return "Object[] /* union<" + types + ">: tag + overlapping variants */";
        }
        case stdlib::TypeId::None:
            break; // fall through to legacy paths
        }
    }

    // Try TypeBinder resolution for single-part abstract names
    if (type.nameParts.size() == 1) {
        auto resolved = binder_.resolve(type.nameParts[0]);
        if (resolved) return *resolved;
    }

    // Try concrete source-language type mapping
    if (type.nameParts.size() == 1) {
        auto mapped = mapConcreteType(type.nameParts[0]);
        if (!mapped.empty()) return mapped;
        auto container = mapContainerType(type.nameParts[0]);
        if (!container.empty()) {
            std::string result = container;
            if (!type.templateArgs.empty()) {
                result += "<";
                for (size_t i = 0; i < type.templateArgs.size(); ++i) {
                    if (i > 0) result += ", ";
                    result += emitType(type.templateArgs[i]);
                }
                result += ">";
            }
            return result;
        }
    }
    // Multi-part qualified types: check last part
    if (type.nameParts.size() > 1) {
        const auto& lastName = type.nameParts.back();
        auto container = mapContainerType(lastName);
        if (!container.empty()) {
            std::string result = container;
            if (!type.templateArgs.empty()) {
                result += "<";
                for (size_t i = 0; i < type.templateArgs.size(); ++i) {
                    if (i > 0) result += ", ";
                    result += emitType(type.templateArgs[i]);
                }
                result += ">";
            }
            return result;
        }
        auto mapped = mapConcreteType(lastName);
        if (!mapped.empty()) return mapped;
    }

    std::string result;

    // Java uses dot-separated qualified names
    for (size_t i = 0; i < type.nameParts.size(); ++i) {
        if (i > 0) result += ".";
        result += type.nameParts[i];
    }

    if (!type.templateArgs.empty()) {
        result += "<";
        for (size_t i = 0; i < type.templateArgs.size(); ++i) {
            if (i > 0) result += ", ";
            result += emitType(type.templateArgs[i]);
        }
        result += ">";
    }

    // Java ignores Ref/Ptr modifiers and const
    return result;
}

std::string JavaEmitter::emitExpr(const Expr& expr) {
    switch (expr.kind()) {
    case Expr::Kind::BinaryOp: {
        const auto& e = static_cast<const BinaryOpExpr&>(expr);
        return "(" + emitExpr(*e.lhs) + " " + binaryOpStr(e.op) + " " + emitExpr(*e.rhs) + ")";
    }
    case Expr::Kind::UnaryOp: {
        const auto& e = static_cast<const UnaryOpExpr&>(expr);
        std::string op;
        switch (e.op) {
        case UnaryOp::Negate: op = "-"; break;
        case UnaryOp::Not: op = "!"; break;
        case UnaryOp::BitNot: op = "~"; break;
        case UnaryOp::PreIncrement: return "++" + emitExpr(*e.operand);
        case UnaryOp::PostIncrement: return emitExpr(*e.operand) + "++";
        case UnaryOp::PreDecrement: return "--" + emitExpr(*e.operand);
        case UnaryOp::PostDecrement: return emitExpr(*e.operand) + "--";
        }
        return op + emitExpr(*e.operand);
    }
    case Expr::Kind::Call: {
        const auto& e = static_cast<const CallExpr&>(expr);
        std::string result = e.callee + "(";
        for (size_t i = 0; i < e.args.size(); ++i) {
            if (i > 0) result += ", ";
            result += emitExpr(*e.args[i]);
        }
        result += ")";
        return result;
    }
    case Expr::Kind::MemberAccess: {
        const auto& e = static_cast<const MemberAccessExpr&>(expr);
        return emitExpr(*e.object) + "." + e.member;
    }
    case Expr::Kind::Index: {
        const auto& e = static_cast<const IndexExpr&>(expr);
        return emitExpr(*e.object) + "[" + emitExpr(*e.index) + "]";
    }
    case Expr::Kind::Literal: {
        const auto& e = static_cast<const LiteralExpr&>(expr);
        if (e.litKind == LiteralKind::String) return "\"" + e.value + "\"";
        if (e.litKind == LiteralKind::Boolean) return (e.value == "true") ? "true" : "false";
        return e.value;
    }
    case Expr::Kind::VarRef: {
        const auto& e = static_cast<const VarRefExpr&>(expr);
        return e.name;
    }
    case Expr::Kind::Construct: {
        const auto& e = static_cast<const ConstructExpr&>(expr);
        std::string result = "new " + emitType(e.type) + "(";
        for (size_t i = 0; i < e.args.size(); ++i) {
            if (i > 0) result += ", ";
            result += emitExpr(*e.args[i]);
        }
        result += ")";
        return result;
    }
    case Expr::Kind::Lambda: {
        const auto& e = static_cast<const LambdaExpr&>(expr);
        // Java lambdas: (params) -> { body }
        std::string result = "(";
        for (size_t i = 0; i < e.params.size(); ++i) {
            if (i > 0) result += ", ";
            result += emitType(e.params[i].type) + " " + e.params[i].name;
        }
        result += ") -> {\n";
        for (const auto& st : e.body)
            result += emitStmt(*st, 1);
        result += "}";
        return result;
    }
    case Expr::Kind::Throw: {
        const auto& e = static_cast<const ThrowExpr&>(expr);
        return "throw " + emitExpr(*e.operand);
    }
    case Expr::Kind::Unsupported: {
        const auto& e = static_cast<const UnsupportedExpr&>(expr);
        return "/* TOPO-TRANSPILE: unsupported — " + e.description + " */";
    }
    case Expr::Kind::Ternary: {
        const auto& e = static_cast<const TernaryExpr&>(expr);
        return "(" + emitExpr(*e.condition) + " ? " + emitExpr(*e.trueExpr) + " : " + emitExpr(*e.falseExpr) + ")";
    }
    case Expr::Kind::CompoundAssign: {
        const auto& e = static_cast<const CompoundAssignExpr&>(expr);
        return emitExpr(*e.target) + " " + binaryOpStr(e.op) + "= " + emitExpr(*e.value);
    }
    }
    return "/* TOPO-TRANSPILE: unsupported — unknown expression */";
}

std::string JavaEmitter::emitStmt(const Stmt& stmt, int level) {
    std::string prefix = fidelityComment(stmt.fidelity, level);

    switch (stmt.kind()) {
    case Stmt::Kind::VarDecl: {
        const auto& s = static_cast<const VarDeclStmt&>(stmt);
        std::string result = prefix + ind(level) + emitType(s.type) + " " + s.name;
        if (s.init) result += " = " + emitExpr(*s.init);
        result += ";\n";
        return result;
    }
    case Stmt::Kind::Assign: {
        const auto& s = static_cast<const AssignStmt&>(stmt);
        return prefix + ind(level) + emitExpr(*s.target) + " = " + emitExpr(*s.value) + ";\n";
    }
    case Stmt::Kind::Return: {
        const auto& s = static_cast<const ReturnStmt&>(stmt);
        if (s.value) return prefix + ind(level) + "return " + emitExpr(*s.value) + ";\n";
        return prefix + ind(level) + "return;\n";
    }
    case Stmt::Kind::If: {
        const auto& s = static_cast<const IfStmt&>(stmt);
        std::string result = prefix + ind(level) + "if (" + emitExpr(*s.condition) + ") {\n";
        for (const auto& st : s.thenBody)
            result += emitStmt(*st, level + 1);
        result += ind(level) + "}";
        if (!s.elseBody.empty()) {
            result += " else {\n";
            for (const auto& st : s.elseBody)
                result += emitStmt(*st, level + 1);
            result += ind(level) + "}";
        }
        result += "\n";
        return result;
    }
    case Stmt::Kind::For: {
        const auto& s = static_cast<const ForStmt&>(stmt);
        std::string init;
        if (s.init) {
            std::string raw = emitStmt(*s.init, 0);
            size_t start = raw.find_first_not_of(" \t\n");
            if (start != std::string::npos) raw = raw.substr(start);
            while (!raw.empty() && (raw.back() == '\n' || raw.back() == ';' || raw.back() == ' '))
                raw.pop_back();
            init = raw;
        }
        std::string cond = s.condition ? emitExpr(*s.condition) : "";
        std::string incr = s.increment ? emitExpr(*s.increment) : "";

        std::string result = prefix + ind(level) + "for (" + init + "; " + cond + "; " + incr + ") {\n";
        for (const auto& st : s.body)
            result += emitStmt(*st, level + 1);
        result += ind(level) + "}\n";
        return result;
    }
    case Stmt::Kind::While: {
        const auto& s = static_cast<const WhileStmt&>(stmt);
        std::string result = prefix + ind(level) + "while (" + emitExpr(*s.condition) + ") {\n";
        for (const auto& st : s.body)
            result += emitStmt(*st, level + 1);
        result += ind(level) + "}\n";
        return result;
    }
    case Stmt::Kind::ExprStmt: {
        const auto& s = static_cast<const ExprStmt&>(stmt);
        return prefix + ind(level) + emitExpr(*s.expr) + ";\n";
    }
    case Stmt::Kind::TryCatch: {
        const auto& s = static_cast<const TryCatchStmt&>(stmt);
        std::string result = prefix + ind(level) + "try {\n";
        for (const auto& st : s.tryBody)
            result += emitStmt(*st, level + 1);
        result += ind(level) + "}";
        for (const auto& c : s.catchClauses) {
            result += " catch (" + emitType(c.exceptionType);
            if (!c.varName.empty()) result += " " + c.varName;
            result += ") {\n";
            for (const auto& st : c.body)
                result += emitStmt(*st, level + 1);
            result += ind(level) + "}";
        }
        if (!s.finallyBody.empty()) {
            result += " finally {\n";
            for (const auto& st : s.finallyBody)
                result += emitStmt(*st, level + 1);
            result += ind(level) + "}";
        }
        result += "\n";
        return result;
    }
    case Stmt::Kind::Break: return prefix + ind(level) + "break;\n";
    case Stmt::Kind::Continue: return prefix + ind(level) + "continue;\n";
    case Stmt::Kind::Switch: {
        const auto& s = static_cast<const SwitchStmt&>(stmt);
        std::string result = prefix + ind(level) + "switch (" + emitExpr(*s.subject) + ") {\n";
        for (const auto& c : s.cases) {
            if (c.value)
                result += ind(level + 1) + "case " + emitExpr(*c.value) + ":\n";
            else
                result += ind(level + 1) + "default:\n";
            for (const auto& st : c.body)
                result += emitStmt(*st, level + 2);
        }
        result += ind(level) + "}\n";
        return result;
    }
    }
    return prefix + ind(level) + "// TOPO-TRANSPILE: unsupported — unknown statement\n";
}

// Pick a `return <zero>;` clause for a rendered Java return type, used when a
// TranspileFunction arrives with non-void returnType and an empty body — javac
// rejects `int foo() {}` with "missing return statement", which would break the
// roundtrip-through-javac contract. Returns "" for `void` and falls through to
// `return null;` for unrecognised spellings (safe default for any reference type).
static std::string javaZeroValueReturn(const std::string& renderedType) {
    if (renderedType == "void") return "";
    if (renderedType == "boolean") return "return false;";
    if (renderedType == "int" || renderedType == "long" ||
        renderedType == "short" || renderedType == "byte" ||
        renderedType == "double" || renderedType == "float")
        return "return 0;";
    if (renderedType == "char") return "return '\\0';";
    return "return null;";
}

std::string JavaEmitter::emitFunction(const TranspileFunction& func, bool inClass) {
    std::string result;

    const std::string renderedReturn = emitType(func.returnType);
    const bool needsZeroValueStub =
        func.body.empty() && renderedReturn != "void" && !renderedReturn.empty();

    // Synthesised stub is by definition not Source — downgrade so the fidelity
    // comment matches the reality the // FIXME marker below documents.
    Fidelity effectiveFidelity = func.fidelity;
    if (needsZeroValueStub && effectiveFidelity == Fidelity::Source)
        effectiveFidelity = Fidelity::Inferred;
    result += fidelityComment(effectiveFidelity, 0);

    for (const auto& u : func.unsupported)
        result += "// TOPO-TRANSPILE: unsupported — " + u + "\n";
    if (needsZeroValueStub) {
        result += "// TOPO-TRANSPILE: unsupported — empty-body-stub: returnType=" +
                  renderedReturn + "\n";
    }

    auto [_, simpleName] = splitQualifiedName(func.qualifiedName);
    if (!func.accessModifier.empty()) result += func.accessModifier + " ";
    else if (inClass) result += "public ";
    // All transpiled functions are static (free functions have no instance context)
    result += "static ";
    // Java method-level generics sit between the modifiers and the return type.
    std::string generics = genericClauseImpl(
        func.templateParams,
        [this](const TypeNode& t) { return emitType(t); });
    if (!generics.empty()) result += generics + " ";
    result += renderedReturn + " " + simpleName + "(";
    for (size_t i = 0; i < func.params.size(); ++i) {
        if (i > 0) result += ", ";
        result += emitType(func.params[i].type) + " " + func.params[i].name;
    }
    result += ")";

    if (!func.throwsClause.empty()) {
        result += " throws ";
        for (size_t i = 0; i < func.throwsClause.size(); ++i) {
            if (i > 0) result += ", ";
            result += emitType(func.throwsClause[i]);
        }
    }

    result += " {\n";

    if (needsZeroValueStub) {
        result += ind(1) + "// FIXME: emitter inserted stub — original body was empty\n";
        result += ind(1) + javaZeroValueReturn(renderedReturn) + "\n";
    } else {
        for (const auto& s : func.body)
            result += emitStmt(*s, 1);
    }

    result += "}\n";
    return result;
}

std::string JavaEmitter::emitStruct(const TranspileType& type) {
    std::string result;
    result += fidelityComment(type.fidelity, 0);
    auto [_, simpleName] = splitQualifiedName(type.qualifiedName);
    // Generic parameters bind to the class name, before any extends/implements
    // clause the inheritance logic below appends.
    result += "public class " + simpleName + genericClauseImpl(
        type.templateParams,
        [this](const TypeNode& t) { return emitType(t); });

    // Inheritance hierarchy, in source order.
    //
    // When the extractor supplies a parallel baseClassKinds discriminator
    // (same length as baseClasses), Java placement is exact:
    //   - the (at most one) Class base → `extends`
    //   - every Interface base → `implements`
    // This correctly handles an interface-only class
    // (`class C implements I`, no superclass → no Class base → no `extends`,
    // `implements I`) and an interface-extends-interface: the Java extractor
    // emits an InterfaceDeclaration's parent interfaces with kind Interface
    // and NO Class base, so a transpiled interface type carries only Interface
    // bases and no spurious `extends Base`. (This emitter renders every type
    // as `public class`; the discriminator still guarantees an
    // interface-derived type never produces an incorrect `extends`.)
    //
    // When baseClassKinds is EMPTY the discriminator is unavailable; fall back
    // to the legacy heuristic (first base = extends, rest = implements) so
    // pre-discriminator payloads keep byte-identical output. Empty baseClasses
    // ⇒ no clause at all, byte-identical to pre-inheritance emission.
    if (!type.baseClasses.empty()) {
        const bool haveKinds = type.baseClassKinds.size() == type.baseClasses.size();
        if (haveKinds) {
            std::string extendsTarget;
            std::vector<std::string> implementsList;
            for (size_t i = 0; i < type.baseClasses.size(); ++i) {
                if (type.baseClassKinds[i] == BaseClassKind::Class) {
                    // At most one class base in valid Java; last wins if the
                    // extractor ever emits more (defensive — not expected).
                    extendsTarget = emitType(type.baseClasses[i]);
                } else {
                    implementsList.push_back(emitType(type.baseClasses[i]));
                }
            }
            if (!extendsTarget.empty()) result += " extends " + extendsTarget;
            if (!implementsList.empty()) {
                result += " implements ";
                for (size_t i = 0; i < implementsList.size(); ++i) {
                    if (i > 0) result += ", ";
                    result += implementsList[i];
                }
            }
        } else {
            // Legacy heuristic fallback (no discriminator available).
            result += " extends " + emitType(type.baseClasses[0]);
            if (type.baseClasses.size() > 1) {
                result += " implements ";
                for (size_t i = 1; i < type.baseClasses.size(); ++i) {
                    if (i > 1) result += ", ";
                    result += emitType(type.baseClasses[i]);
                }
            }
        }
    }
    result += " {\n";

    // Idiomatic Java: private fields with public getX()/setX() accessors.
    // (The transpile-equivalence harness never touches struct fields, so
    // this encapsulation is non-breaking; direct-field tests would use the
    // accessors.)
    for (const auto& f : type.fields) {
        result += fidelityComment(f.fidelity, 1);
        result += ind(1) + "private " + emitType(f.type) + " " + f.name + ";\n";
    }

    for (const auto& f : type.fields) {
        std::string javaType = emitType(f.type);
        std::string accessorSuffix = f.name;
        if (!accessorSuffix.empty())
            accessorSuffix[0] = static_cast<char>(
                std::toupper(static_cast<unsigned char>(accessorSuffix[0])));

        result += "\n";
        result += ind(1) + "public " + javaType + " get" + accessorSuffix
                  + "() {\n";
        result += ind(2) + "return this." + f.name + ";\n";
        result += ind(1) + "}\n";

        result += ind(1) + "public void set" + accessorSuffix + "("
                  + javaType + " " + f.name + ") {\n";
        result += ind(2) + "this." + f.name + " = " + f.name + ";\n";
        result += ind(1) + "}\n";
    }

    result += "}\n";
    return result;
}

} // namespace topo::transpile
