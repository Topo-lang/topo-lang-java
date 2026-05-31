// JavaEmitter stdlib bridging-type tests — focused on the `bytes` and
// `array<T, N>` stdlib bridging types.
//
// The Java boundary mappings under test:
//
//   bytes       -> java.util.List<Byte>   (slice<u8>-isomorphic; emits
//                                           exactly what slice<u8> emits)
//   array<T,N>  -> T[] /* ... fixed-length N ... */
//                  (Java has no value fixed-size array; element recursion
//                   mirrors slice<T>, N carried in an inline comment)
//
// A couple of pre-existing mappings (slice<u8>, optional<T>) are asserted
// alongside so the bytes↔slice<u8> isomorphism is verified end-to-end rather
// than assumed. These are unit-level checks on the emitter's `emitType`
// output via the public `emit()` entry point, matching the Cpp/Python
// per-language stdlib test pattern (cross-language equivalence is covered by
// the topo-core transpile-equivalence harness, not here).

#include "JavaEmitter.h"
#include "topo/Stdlib/Types.h"
#include "topo/Transpile/TranspileModel.h"
#include <gtest/gtest.h>
#include <memory>
#include <string>

using namespace topo;
using namespace topo::transpile;

// ---------------------------------------------------------------------------
// Helpers (mirroring CppEmitterStdlibTest)
// ---------------------------------------------------------------------------

/// Build a TypeNode for a stdlib scalar type.
static TypeNode stdlibScalar(stdlib::TypeId id) {
    TypeNode t;
    t.nameParts = {stdlib::keywordOf(id)};
    t.stdlibId = id;
    return t;
}

/// Canonical "no-bounds" TemplateParamDecl factory used by the
/// generics tests. `TemplateParamDecl` has 8 fields (kind / name /
/// constraintType / extraBounds / isVariadic / innerParams /
/// defaultType / defaultValue / location); partial list-initialization
/// silently picks up new fields. The helper keeps the "I want defaults"
/// intent in one place — avoiding missing-field-initializer warnings in
/// the test fixtures.
static TemplateParamDecl makeTypeParam(std::string name) {
    TemplateParamDecl tp;
    tp.kind = TemplateParamDecl::TypeParam;
    tp.name = std::move(name);
    return tp;
}

/// Build a TypeNode for a single-type-arg parameterized stdlib type
/// (optional / slice).
static TypeNode stdlibParametric(stdlib::TypeId id, TypeNode inner) {
    TypeNode t;
    t.nameParts = {stdlib::keywordOf(id)};
    t.stdlibId = id;
    t.templateArgs.push_back(std::move(inner));
    return t;
}

/// Build a TypeNode for `array<T, N>`: templateArgs[0] = element type T,
/// templateArgs[1].nonTypeValue = N (matches what the parser produces).
static TypeNode stdlibArray(TypeNode elem, int n) {
    TypeNode t;
    t.nameParts = {stdlib::keywordOf(stdlib::TypeId::Array)};
    t.stdlibId = stdlib::TypeId::Array;
    t.templateArgs.push_back(std::move(elem));
    TypeNode count;
    count.nonTypeValue = n;
    t.templateArgs.push_back(std::move(count));
    return t;
}

/// Build a TypeNode for `record<a: i64>` (single i64 field), so nested
/// `array<record<...>, N>` can be exercised.
static TypeNode recordSingleI64(const std::string& fieldName) {
    TypeNode t;
    t.nameParts = {stdlib::keywordOf(stdlib::TypeId::Record)};
    t.stdlibId = stdlib::TypeId::Record;
    TypeNode::RecordField f;
    f.name = fieldName;
    f.typeBox.push_back(stdlibScalar(stdlib::TypeId::I64));
    t.recordFields.push_back(std::move(f));
    return t;
}

/// Emit `boundary(value: paramType) -> returnType` and return the Java source.
static std::string emitWithParamAndReturn(TypeNode paramType, TypeNode returnType) {
    TranspileModule mod;
    TranspileFunction fn;
    fn.qualifiedName = "boundary";
    fn.returnType = std::move(returnType);
    Parameter p;
    p.type = std::move(paramType);
    p.name = "value";
    fn.params.push_back(std::move(p));
    mod.functions.push_back(std::move(fn));

    JavaEmitter emitter;
    return emitter.emit(mod).code;
}

// ---------------------------------------------------------------------------
// bytes — must emit exactly what slice<u8> emits
// ---------------------------------------------------------------------------

TEST(JavaEmitterStdlib, BytesMapsToListOfByte) {
    std::string code = emitWithParamAndReturn(
        stdlibScalar(stdlib::TypeId::Bytes),
        stdlibScalar(stdlib::TypeId::Bool));
    EXPECT_NE(code.find("java.util.List<Byte> value"), std::string::npos)
        << "Generated code:\n" << code;
}

TEST(JavaEmitterStdlib, BytesIsIsomorphicToSliceOfU8) {
    // The contract: `bytes` emits EXACTLY what `slice<u8>` emits in this
    // emitter. Render both and assert string equality of the emitted type.
    std::string bytesCode = emitWithParamAndReturn(
        stdlibScalar(stdlib::TypeId::Bytes),
        stdlibScalar(stdlib::TypeId::Bool));
    std::string sliceU8Code = emitWithParamAndReturn(
        stdlibParametric(stdlib::TypeId::Slice,
                         stdlibScalar(stdlib::TypeId::U8)),
        stdlibScalar(stdlib::TypeId::Bool));

    const std::string token = "java.util.List<Byte> value";
    EXPECT_NE(bytesCode.find(token), std::string::npos)
        << "bytes did not emit slice<u8> form:\n" << bytesCode;
    EXPECT_NE(sliceU8Code.find(token), std::string::npos)
        << "slice<u8> baseline changed:\n" << sliceU8Code;
}

// ---------------------------------------------------------------------------
// array<T, N>
// ---------------------------------------------------------------------------

TEST(JavaEmitterStdlib, ArrayOfI64MapsToLongArrayWithFixedLengthComment) {
    // array<i64, 4> -> long[] with the fixed-length-4 boundary contract in a
    // comment (Java arrays are runtime-length).
    std::string code = emitWithParamAndReturn(
        stdlibArray(stdlibScalar(stdlib::TypeId::I64), 4),
        stdlibScalar(stdlib::TypeId::Bool));
    EXPECT_NE(code.find("long[]"), std::string::npos)
        << "Generated code:\n" << code;
    EXPECT_NE(code.find("fixed-length N=4"), std::string::npos)
        << "fixed-length-N contract comment missing:\n" << code;
}

TEST(JavaEmitterStdlib, NestedArrayOfRecord) {
    // array<record<a: i64>, 2>: element type recurses exactly like slice<T>.
    // The Java struct name for an inline record falls out of emitType()'s
    // record path; we only assert the array shell + N here.
    std::string code = emitWithParamAndReturn(
        stdlibArray(recordSingleI64("a"), 2),
        stdlibScalar(stdlib::TypeId::Bool));
    EXPECT_NE(code.find("[]"), std::string::npos)
        << "Generated code:\n" << code;
    EXPECT_NE(code.find("fixed-length N=2"), std::string::npos)
        << "nested array fixed-length-N contract comment missing:\n" << code;
}

TEST(JavaEmitterStdlib, NestedArrayOfArray) {
    // array<array<i64, 4>, 2>: inner element recursion produces a nested
    // array type; both N contracts must be carried.
    std::string code = emitWithParamAndReturn(
        stdlibArray(stdlibArray(stdlibScalar(stdlib::TypeId::I64), 4), 2),
        stdlibScalar(stdlib::TypeId::Bool));
    EXPECT_NE(code.find("long[]"), std::string::npos)
        << "Generated code:\n" << code;
    EXPECT_NE(code.find("fixed-length N=4"), std::string::npos)
        << "inner array N missing:\n" << code;
    EXPECT_NE(code.find("fixed-length N=2"), std::string::npos)
        << "outer array N missing:\n" << code;
}

TEST(JavaEmitterStdlib, ArrayOfBytesElementRecurses) {
    // array<bytes, 3>: element `bytes` recurses through the same emitType()
    // path, so the element renders as the slice<u8> form.
    std::string code = emitWithParamAndReturn(
        stdlibArray(stdlibScalar(stdlib::TypeId::Bytes), 3),
        stdlibScalar(stdlib::TypeId::Bool));
    EXPECT_NE(code.find("java.util.List<Byte>[]"), std::string::npos)
        << "Generated code:\n" << code;
    EXPECT_NE(code.find("fixed-length N=3"), std::string::npos)
        << "array<bytes,3> N missing:\n" << code;
}

// ---------------------------------------------------------------------------
// Java idioms — track A: `throws` clause + getter/setter accessors.
// ---------------------------------------------------------------------------

/// Build a bare TypeNode naming a single (reference) type.
static TypeNode namedType(const std::string& name) {
    TypeNode t;
    t.nameParts = {name};
    return t;
}

TEST(JavaEmitterIdioms, ThrowsClauseEmittedBetweenParenAndBrace) {
    TranspileModule mod;
    TranspileFunction fn;
    fn.qualifiedName = "readFile";
    fn.returnType = namedType("void");
    fn.throwsClause = {namedType("IOException")};
    mod.functions.push_back(std::move(fn));

    JavaEmitter emitter;
    std::string code = emitter.emit(mod).code;
    EXPECT_NE(code.find(") throws IOException {"), std::string::npos)
        << "throws clause not emitted between ) and {:\n" << code;
}

TEST(JavaEmitterIdioms, MultipleThrowsCommaSeparated) {
    TranspileModule mod;
    TranspileFunction fn;
    fn.qualifiedName = "doWork";
    fn.returnType = namedType("void");
    fn.throwsClause = {namedType("IOException"), namedType("SQLException")};
    mod.functions.push_back(std::move(fn));

    JavaEmitter emitter;
    std::string code = emitter.emit(mod).code;
    EXPECT_NE(code.find(") throws IOException, SQLException {"),
              std::string::npos)
        << "multi-exception throws clause malformed:\n" << code;
}

TEST(JavaEmitterIdioms, EmptyThrowsClauseEmitsNoThrows) {
    TranspileModule mod;
    TranspileFunction fn;
    fn.qualifiedName = "pure";
    fn.returnType = namedType("void");
    // throwsClause left empty (default)
    mod.functions.push_back(std::move(fn));

    JavaEmitter emitter;
    std::string code = emitter.emit(mod).code;
    EXPECT_EQ(code.find("throws"), std::string::npos)
        << "empty throwsClause must not emit `throws`:\n" << code;
    EXPECT_NE(code.find(") {"), std::string::npos)
        << "signature should close directly with ) {:\n" << code;
}

TEST(JavaEmitterIdioms, StructEmitsPrivateFieldsAndAccessors) {
    TranspileModule mod;
    TranspileType ty;
    ty.qualifiedName = "geom::Point";
    TranspileField fx;
    fx.type = namedType("int");
    fx.name = "x";
    TranspileField fy;
    fy.type = namedType("int");
    fy.name = "y";
    ty.fields = {fx, fy};
    mod.types.push_back(std::move(ty));

    JavaEmitter emitter;
    std::string code = emitter.emit(mod).code;

    EXPECT_NE(code.find("private int x;"), std::string::npos)
        << "field x not private:\n" << code;
    EXPECT_NE(code.find("private int y;"), std::string::npos)
        << "field y not private:\n" << code;
    EXPECT_NE(code.find("public int getX() {"), std::string::npos)
        << "getX accessor missing:\n" << code;
    EXPECT_NE(code.find("public void setX(int x) {"), std::string::npos)
        << "setX accessor missing:\n" << code;
    EXPECT_NE(code.find("public int getY() {"), std::string::npos)
        << "getY accessor missing:\n" << code;
    EXPECT_NE(code.find("public void setY(int y) {"), std::string::npos)
        << "setY accessor missing:\n" << code;
    EXPECT_EQ(code.find("public int x;"), std::string::npos)
        << "field x must not remain public:\n" << code;
}

// ---------------------------------------------------------------------------
// Inheritance hierarchy: TranspileType.baseClasses -> extends / implements
// (track B). Named TranspileJavaInheritance so the targeted
// `ctest -R 'Transpile|transpile'` filter picks these up.
// ---------------------------------------------------------------------------

/// Emit a struct/class with the given baseClasses and return the Java source.
static std::string emitTypeWithBases(const std::string& qname,
                                     std::vector<TypeNode> bases) {
    TranspileModule mod;
    TranspileType ty;
    ty.qualifiedName = qname;
    ty.baseClasses = std::move(bases);
    mod.types.push_back(std::move(ty));
    JavaEmitter emitter;
    return emitter.emit(mod).code;
}

TEST(TranspileJavaInheritance, ExtendsAndImplements) {
    // class Dog extends Animal implements Comparable
    std::string code = emitTypeWithBases(
        "Dog", {namedType("Animal"), namedType("Comparable")});
    EXPECT_NE(code.find("class Dog extends Animal implements Comparable"),
              std::string::npos)
        << "Generated code:\n" << code;
}

TEST(TranspileJavaInheritance, ExtendsOnly) {
    std::string code = emitTypeWithBases("Cat", {namedType("Animal")});
    EXPECT_NE(code.find("class Cat extends Animal {"), std::string::npos)
        << "Generated code:\n" << code;
    EXPECT_EQ(code.find("implements"), std::string::npos)
        << "no implements clause expected:\n" << code;
}

TEST(TranspileJavaInheritance, MultipleInterfaces) {
    std::string code = emitTypeWithBases(
        "Service",
        {namedType("Base"), namedType("Runnable"), namedType("Closeable")});
    EXPECT_NE(
        code.find("class Service extends Base implements Runnable, Closeable"),
        std::string::npos)
        << "Generated code:\n" << code;
}

TEST(TranspileJavaInheritance, EmptyBasesByteIdenticalToPreInheritance) {
    // The pre-inheritance emission was exactly `public class <name> {`.
    // An empty baseClasses list must reproduce that with no extends/implements.
    std::string code = emitTypeWithBases("Plain", {});
    EXPECT_NE(code.find("public class Plain {"), std::string::npos)
        << "Generated code:\n" << code;
    EXPECT_EQ(code.find("extends"), std::string::npos) << code;
    EXPECT_EQ(code.find("implements"), std::string::npos) << code;
}

/// Emit a class with explicit per-base class/interface discriminators.
static std::string emitTypeWithKinds(const std::string& qname, std::vector<TypeNode> bases,
                                     std::vector<BaseClassKind> kinds) {
    TranspileModule mod;
    TranspileType ty;
    ty.qualifiedName = qname;
    ty.baseClasses = std::move(bases);
    ty.baseClassKinds = std::move(kinds);
    mod.types.push_back(std::move(ty));
    JavaEmitter emitter;
    return emitter.emit(mod).code;
}

TEST(TranspileJavaInheritance, DiscriminatorClassPlusInterface) {
    // class Dog extends Animal implements Comparable — discriminator-driven.
    std::string code = emitTypeWithKinds("Dog", {namedType("Animal"), namedType("Comparable")},
                                         {BaseClassKind::Class, BaseClassKind::Interface});
    EXPECT_NE(code.find("class Dog extends Animal implements Comparable"), std::string::npos)
        << "Generated code:\n" << code;
}

TEST(TranspileJavaInheritance, InterfaceOnlyClassUsesImplementsNotExtends) {
    // `class C implements I` — no superclass. baseClasses=[I], kinds=[Interface].
    // The FIX: this must emit `implements I`, NOT the pre-fix mis-render
    // `extends I` that the legacy first-base-is-extends heuristic produced.
    std::string code = emitTypeWithKinds("Handler", {namedType("Runnable")}, {BaseClassKind::Interface});
    EXPECT_NE(code.find("class Handler implements Runnable {"), std::string::npos)
        << "interface-only class must use `implements`, not `extends`:\n"
        << code;
    EXPECT_EQ(code.find("extends"), std::string::npos)
        << "interface-only class must NOT emit `extends`:\n"
        << code;
}

TEST(TranspileJavaInheritance, InterfaceExtendsInterfaceNoSpuriousExtendsBase) {
    // An InterfaceDeclaration's parent interfaces are extracted with kind
    // Interface and NO Class base. The transpiled type must therefore carry
    // only `implements` of its parents and never a spurious `extends Base`
    // (the interface-extends-interface invariant: no Class base ⇒ no extends).
    std::string code = emitTypeWithKinds("Comparable2", {namedType("Comparable"), namedType("Serializable")},
                                         {BaseClassKind::Interface, BaseClassKind::Interface});
    EXPECT_NE(code.find("implements Comparable, Serializable"), std::string::npos)
        << "Generated code:\n" << code;
    EXPECT_EQ(code.find("extends"), std::string::npos)
        << "interface-derived type must not produce `extends`:\n"
        << code;
}

TEST(TranspileJavaInheritance, DiscriminatorMultipleInterfacesAfterClass) {
    std::string code =
        emitTypeWithKinds("Service", {namedType("Base"), namedType("Runnable"), namedType("Closeable")},
                          {BaseClassKind::Class, BaseClassKind::Interface, BaseClassKind::Interface});
    EXPECT_NE(code.find("class Service extends Base implements Runnable, Closeable"), std::string::npos)
        << "Generated code:\n" << code;
}

TEST(TranspileJavaInheritance, EmptyKindsFallsBackToLegacyHeuristic) {
    // No discriminator (empty baseClassKinds) ⇒ legacy heuristic, byte-identical
    // to the pre-discriminator behavior: first base = extends, rest = implements.
    std::string code = emitTypeWithBases("Dog", {namedType("Animal"), namedType("Comparable")});
    EXPECT_NE(code.find("class Dog extends Animal implements Comparable"), std::string::npos)
        << "Generated code:\n" << code;
}

// ---------------------------------------------------------------------------
// Declaration-level generic type parameters: TranspileType/TranspileFunction
// .templateParams -> Java `class Box<T>` / `<T> T identity(...)`.
// ---------------------------------------------------------------------------

TEST(JavaEmitterIdioms, GenericClassEmitsTypeParamsAfterName) {
    TranspileModule mod;
    TranspileType ty;
    ty.qualifiedName = "Box";
    ty.templateParams.push_back(makeTypeParam("T"));
    mod.types.push_back(std::move(ty));
    JavaEmitter emitter;
    std::string code = emitter.emit(mod).code;
    EXPECT_NE(code.find("public class Box<T> {"), std::string::npos)
        << "Generated code:\n" << code;
}

TEST(JavaEmitterIdioms, GenericClassTwoParams) {
    TranspileModule mod;
    TranspileType ty;
    ty.qualifiedName = "Pair";
    ty.templateParams.push_back(makeTypeParam("K"));
    ty.templateParams.push_back(makeTypeParam("V"));
    mod.types.push_back(std::move(ty));
    JavaEmitter emitter;
    std::string code = emitter.emit(mod).code;
    EXPECT_NE(code.find("public class Pair<K, V> {"), std::string::npos)
        << "Generated code:\n" << code;
}

TEST(JavaEmitterIdioms, GenericClassTypeParamsPrecedeExtends) {
    // The generic clause binds to the class name BEFORE `extends`; the
    // inheritance logic must still place the base after it.
    TranspileModule mod;
    TranspileType ty;
    ty.qualifiedName = "Box";
    ty.templateParams.push_back(makeTypeParam("T"));
    ty.baseClasses = {namedType("Base")};
    mod.types.push_back(std::move(ty));
    JavaEmitter emitter;
    std::string code = emitter.emit(mod).code;
    EXPECT_NE(code.find("class Box<T> extends Base"), std::string::npos)
        << "Generated code:\n" << code;
}

TEST(JavaEmitterIdioms, NonGenericClassByteIdenticalToPreChange) {
    // Empty templateParams must reproduce the exact pre-generics emission.
    std::string code = emitTypeWithBases("Plain", {});
    EXPECT_NE(code.find("public class Plain {"), std::string::npos)
        << "Generated code:\n" << code;
    EXPECT_EQ(code.find("<"), std::string::npos)
        << "no generic clause expected on a non-generic class:\n" << code;
}

TEST(JavaEmitterIdioms, GenericMethodEmitsTypeParamBeforeReturnType) {
    TranspileModule mod;
    TranspileFunction fn;
    fn.qualifiedName = "identity";
    fn.returnType = namedType("T");
    Parameter p;
    p.name = "x";
    p.type = namedType("T");
    fn.params.push_back(p);
    fn.templateParams.push_back(makeTypeParam("T"));
    mod.functions.push_back(std::move(fn));
    JavaEmitter emitter;
    std::string code = emitter.emit(mod).code;
    // Free function, no accessModifier ⇒ just `static`; the generic clause
    // must sit between `static` and the return type.
    EXPECT_NE(code.find("static <T> T identity(T x)"), std::string::npos)
        << "Generated code:\n" << code;
}

TEST(JavaEmitterIdioms, NonGenericMethodByteIdenticalToPreChange) {
    TranspileModule mod;
    TranspileFunction fn;
    fn.qualifiedName = "answer";
    fn.returnType = namedType("int");
    mod.functions.push_back(std::move(fn));
    JavaEmitter emitter;
    std::string code = emitter.emit(mod).code;
    EXPECT_NE(code.find("static int answer()"), std::string::npos)
        << "Generated code:\n" << code;
    EXPECT_EQ(code.find("<"), std::string::npos)
        << "no generic clause expected on a non-generic method:\n" << code;
}

// --- Single trait-bound rendering: `<T extends Bound>` ---
// Same wire contract as Rust's <T: Bound> MVP; the bound goes through
// emitType so qualified / parameterized bounds carry through.

TEST(JavaEmitterIdioms, GenericClassWithSingleBoundEmitsExtends) {
    TranspileModule mod;
    TranspileType ty;
    ty.qualifiedName = "Box";
    TemplateParamDecl tp = makeTypeParam("T");
    tp.constraintType = namedType("Comparable");
    ty.templateParams.push_back(tp);
    mod.types.push_back(std::move(ty));
    JavaEmitter emitter;
    std::string code = emitter.emit(mod).code;
    EXPECT_NE(code.find("public class Box<T extends Comparable> {"), std::string::npos)
        << "Generated code:\n" << code;
}

TEST(JavaEmitterIdioms, GenericMethodWithSingleBoundEmitsExtends) {
    TranspileModule mod;
    TranspileFunction fn;
    fn.qualifiedName = "pick";
    fn.returnType = namedType("T");
    Parameter p;
    p.name = "x";
    p.type = namedType("T");
    fn.params.push_back(p);
    TemplateParamDecl tp = makeTypeParam("T");
    tp.constraintType = namedType("Number");
    fn.templateParams.push_back(tp);
    mod.functions.push_back(std::move(fn));
    JavaEmitter emitter;
    std::string code = emitter.emit(mod).code;
    EXPECT_NE(code.find("static <T extends Number> T pick(T x)"), std::string::npos)
        << "Generated code:\n" << code;
}

TEST(JavaEmitterIdioms, UnboundedTypeParamByteIdenticalToPreBoundsOutput) {
    // Absence of a bound must produce the exact pre-bounds emission.
    TranspileModule mod;
    TranspileType ty;
    ty.qualifiedName = "Plain";
    ty.templateParams.push_back(makeTypeParam("T"));
    mod.types.push_back(std::move(ty));
    JavaEmitter emitter;
    std::string code = emitter.emit(mod).code;
    EXPECT_NE(code.find("public class Plain<T> {"), std::string::npos)
        << "no bound: must remain bare `<T>`; got:\n" << code;
    EXPECT_EQ(code.find("extends"), std::string::npos)
        << "no `extends` clause expected when bound absent:\n" << code;
}

// --- Intersection multi-bound rendering: `<T extends A & B>` ---
// Multi-bound (Java intersection) joins the first bound with `extends` and
// each additional bound with ` & ` (Java syntax). Wire shape `bounds:
// [TypeNode]` populates [constraintType, ...extraBounds] on the model side.

TEST(JavaEmitterIdioms, GenericClassWithIntersectionBoundEmitsAmpersand) {
    TranspileModule mod;
    TranspileType ty;
    ty.qualifiedName = "Sortable";
    TemplateParamDecl tp = makeTypeParam("T");
    tp.constraintType = namedType("Comparable");
    tp.extraBounds.push_back(namedType("Serializable"));
    ty.templateParams.push_back(tp);
    mod.types.push_back(std::move(ty));
    JavaEmitter emitter;
    std::string code = emitter.emit(mod).code;
    EXPECT_NE(
        code.find("public class Sortable<T extends Comparable & Serializable> {"),
        std::string::npos)
        << "Generated code:\n" << code;
}

TEST(JavaEmitterIdioms, GenericMethodWithIntersectionBoundEmitsAmpersand) {
    TranspileModule mod;
    TranspileFunction fn;
    fn.qualifiedName = "pick";
    fn.returnType = namedType("T");
    Parameter p;
    p.name = "x";
    p.type = namedType("T");
    fn.params.push_back(p);
    TemplateParamDecl tp = makeTypeParam("T");
    tp.constraintType = namedType("Number");
    tp.extraBounds.push_back(namedType("Comparable"));
    fn.templateParams.push_back(tp);
    mod.functions.push_back(std::move(fn));
    JavaEmitter emitter;
    std::string code = emitter.emit(mod).code;
    EXPECT_NE(
        code.find("static <T extends Number & Comparable> T pick(T x)"),
        std::string::npos)
        << "Generated code:\n" << code;
}

TEST(JavaEmitterIdioms, SingleBoundEmitWithExtraBoundsEmptyByteIdentical) {
    // Defensive regression: single-bound (extraBounds empty) must emit
    // byte-identical to pre-multi-bound output — no stray ` & ` artefact.
    TranspileModule mod;
    TranspileType ty;
    ty.qualifiedName = "Box";
    TemplateParamDecl tp = makeTypeParam("T");
    tp.constraintType = namedType("Comparable");
    ty.templateParams.push_back(tp);
    mod.types.push_back(std::move(ty));
    JavaEmitter emitter;
    std::string code = emitter.emit(mod).code;
    EXPECT_NE(code.find("public class Box<T extends Comparable> {"),
              std::string::npos)
        << "Generated:\n" << code;
    EXPECT_EQ(code.find("Comparable &"), std::string::npos)
        << "no stray ` & ` after single bound. Got:\n" << code;
}

// ---------------------------------------------------------------------------
// Empty-body + non-void-return zero-value stub
//
// Regression guard: an int return with an empty body must not be rejected
// by javac. `int compute() {}` would ship past the emitter but javac then rejected the
// roundtrip with "missing return statement". The emitter now synthesises a
// zero-value `return <X>;` body (gated on empty `func.body` AND non-void
// returnType), pairs it with an `unsupported` entry + a `// FIXME` inline
// marker so the synthesis is visible to a human reviewer, and downgrades the
// function's fidelity from Source to Inferred.
// ---------------------------------------------------------------------------

static std::string emitEmptyBodyFunctionReturning(TypeNode returnType,
                                                  const std::string& name = "compute") {
    TranspileModule mod;
    TranspileFunction fn;
    fn.qualifiedName = name;
    fn.returnType = std::move(returnType);
    mod.functions.push_back(std::move(fn));
    JavaEmitter emitter;
    return emitter.emit(mod).code;
}

TEST(JavaEmitterEmptyBodyStub, IntReturnEmitsReturnZero) {
    std::string code = emitEmptyBodyFunctionReturning(
        stdlibScalar(stdlib::TypeId::I32));
    EXPECT_NE(code.find("return 0;"), std::string::npos) << code;
    EXPECT_NE(code.find("// FIXME: emitter inserted stub"), std::string::npos) << code;
    EXPECT_NE(code.find("// TOPO-TRANSPILE: unsupported — empty-body-stub"),
              std::string::npos) << code;
    EXPECT_NE(code.find("// [inferred]"), std::string::npos)
        << "fidelity should be downgraded from Source to Inferred:\n" << code;
}

// The synthesised stub is a lossy transpile event: it must register as an
// unsupported construct so verifyModule() counts it — that count drives the
// CLI's unsupported-construct warning and the --verify-max-unsupported gate.
// Before the fix the stub left only a source comment, so a non-void function
// that failed to transpile passed verification silently.
TEST(JavaEmitterEmptyBodyStub, StubRegistersAsUnsupportedConstruct) {
    TranspileModule mod;
    TranspileFunction fn;
    fn.qualifiedName = "compute";
    fn.returnType = stdlibScalar(stdlib::TypeId::I32);
    mod.functions.push_back(std::move(fn));

    JavaEmitter emitter;
    std::string code = emitter.emit(mod).code;
    EXPECT_NE(code.find("return 0;"), std::string::npos) << code;

    TranspileVerification v = verifyModule(mod);
    EXPECT_EQ(v.totalUnsupported, 1)
        << "empty-body stub must count as one unsupported construct";
    ASSERT_EQ(v.perFunction.size(), 1u);
    ASSERT_FALSE(v.perFunction[0].constructs.empty());
    EXPECT_NE(v.perFunction[0].constructs.front().find("empty-body-stub"),
              std::string::npos);
}

TEST(JavaEmitterEmptyBodyStub, LongReturnEmitsReturnZero) {
    std::string code = emitEmptyBodyFunctionReturning(
        stdlibScalar(stdlib::TypeId::I64));
    EXPECT_NE(code.find("return 0;"), std::string::npos) << code;
}

TEST(JavaEmitterEmptyBodyStub, DoubleReturnEmitsReturnZero) {
    std::string code = emitEmptyBodyFunctionReturning(
        stdlibScalar(stdlib::TypeId::F64));
    EXPECT_NE(code.find("return 0;"), std::string::npos) << code;
}

TEST(JavaEmitterEmptyBodyStub, BooleanReturnEmitsReturnFalse) {
    std::string code = emitEmptyBodyFunctionReturning(
        stdlibScalar(stdlib::TypeId::Bool));
    EXPECT_NE(code.find("return false;"), std::string::npos) << code;
}

TEST(JavaEmitterEmptyBodyStub, CharReturnEmitsReturnNullChar) {
    std::string code = emitEmptyBodyFunctionReturning(namedType("char"));
    EXPECT_NE(code.find("return '\\0';"), std::string::npos) << code;
}

TEST(JavaEmitterEmptyBodyStub, StringReturnEmitsReturnNull) {
    std::string code = emitEmptyBodyFunctionReturning(
        stdlibScalar(stdlib::TypeId::String));
    EXPECT_NE(code.find("return null;"), std::string::npos) << code;
}

TEST(JavaEmitterEmptyBodyStub, GenericListReturnEmitsReturnNull) {
    std::string code = emitEmptyBodyFunctionReturning(
        stdlibParametric(stdlib::TypeId::Slice,
                         stdlibScalar(stdlib::TypeId::I64)));
    EXPECT_NE(code.find("return null;"), std::string::npos) << code;
}

TEST(JavaEmitterEmptyBodyStub, VoidReturnEmitsNoStub) {
    std::string code = emitEmptyBodyFunctionReturning(namedType("void"));
    EXPECT_EQ(code.find("return null;"), std::string::npos) << code;
    EXPECT_EQ(code.find("return 0;"), std::string::npos) << code;
    EXPECT_EQ(code.find("// FIXME: emitter inserted stub"), std::string::npos) << code;
    EXPECT_EQ(code.find("// [inferred]"), std::string::npos)
        << "void empty body must not downgrade fidelity:\n" << code;
}

TEST(JavaEmitterEmptyBodyStub, NonEmptyBodyIsNotRewritten) {
    // Sanity: when a body is present, the emitter must emit it verbatim and not
    // splice in a synthetic return. `topo::transpile::LiteralExpr` is fully
    // qualified to disambiguate from the AST's `topo::LiteralExpr`.
    TranspileModule mod;
    TranspileFunction fn;
    fn.qualifiedName = "compute";
    fn.returnType = stdlibScalar(stdlib::TypeId::I32);
    auto ret = std::make_unique<transpile::ReturnStmt>();
    auto lit = std::make_unique<transpile::LiteralExpr>();
    lit->litKind = transpile::LiteralKind::Integer;
    lit->value = "42";
    ret->value = std::move(lit);
    fn.body.push_back(std::move(ret));
    mod.functions.push_back(std::move(fn));
    JavaEmitter emitter;
    std::string code = emitter.emit(mod).code;
    EXPECT_NE(code.find("return 42;"), std::string::npos) << code;
    EXPECT_EQ(code.find("// FIXME: emitter inserted stub"), std::string::npos) << code;
}
