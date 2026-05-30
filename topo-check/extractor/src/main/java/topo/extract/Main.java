// topo-extract-java: Source code extractor for Java files.
//
// Subprocess protocol:
//   stdin  -> JSON { "files": [...], "functions": [...], "symbolTable": {...} }
//   stdout <- JSON TranspileModule
//
// Parses .java files with Eclipse JDT ASTParser, matches requested qualified
// names, and converts JDT AST nodes to the TranspileModel JSON format.

package topo.extract;

import com.google.gson.Gson;
import com.google.gson.JsonArray;
import com.google.gson.JsonElement;
import com.google.gson.JsonObject;
import com.google.gson.JsonParser;
import org.eclipse.jdt.core.dom.*;

import java.io.*;
import java.nio.file.Files;
import java.nio.file.Path;
import java.util.*;

public class Main {

    private static final Gson GSON = new Gson();

    /// Tracks whether the current process has degraded to binding-disabled parsing
    /// after an OutOfMemoryError. Once degraded, subsequent files in the same run
    /// also use binding-disabled parsing to avoid repeated OOM cycles.
    private static boolean bindingResolutionDegraded = false;

    public static void main(String[] args) throws Exception {
        // Read JSON request from stdin
        String input = readStdin();
        JsonObject request = JsonParser.parseString(input).getAsJsonObject();

        JsonArray filesArr = request.getAsJsonArray("files");
        JsonArray functionsArr = request.has("functions") ? request.getAsJsonArray("functions") : new JsonArray();

        Set<String> requestedFunctions = new HashSet<>();
        for (JsonElement el : functionsArr) {
            requestedFunctions.add(el.getAsString());
        }

        JsonArray allFunctions = new JsonArray();
        JsonArray allTypes = new JsonArray();

        for (JsonElement fileEl : filesArr) {
            String filePath = fileEl.getAsString();
            String source;
            try {
                source = Files.readString(Path.of(filePath));
            } catch (IOException e) {
                System.err.println("topo-extract-java: failed to read " + filePath + ": " + e.getMessage());
                continue;
            }

            CompilationUnit cu = parseSource(source, filePath);
            if (cu == null) {
                System.err.println("topo-extract-java: failed to parse " + filePath);
                continue;
            }

            // Extract package name
            String packageName = "";
            if (cu.getPackage() != null) {
                packageName = cu.getPackage().getName().getFullyQualifiedName();
            }

            // Walk all type declarations
            for (Object typeObj : cu.types()) {
                if (typeObj instanceof AbstractTypeDeclaration typeDecl) {
                    collectFromType(typeDecl, packageName, requestedFunctions, allFunctions, allTypes);
                }
            }
        }

        // Build TranspileModule
        JsonObject module = new JsonObject();
        module.add("types", allTypes);
        module.add("functions", allFunctions);
        // OOM-degraded run signal — exposed to the C++ caller so the
        // topo-check report can surface a per-suite diagnostic instead of
        // letting the binding-disabled fallback parse pass silently.
        // Always emitted (false on the happy path); a true value MUST
        // downgrade per-function fidelity and surface a CheckDiagnostic.
        module.addProperty("runDegraded", bindingResolutionDegraded);
        if (bindingResolutionDegraded) {
            module.addProperty("degradationReason",
                "OutOfMemoryError during JDT binding resolution; "
              + "binding-disabled fallback parse used for the remainder of the run. "
              + "Cross-file qualified name resolution, method-reference / var-inference "
              + "classification are degraded.");
        }

        System.out.println(GSON.toJson(module));
    }

    // -----------------------------------------------------------------------
    // Test hook: reset the sticky degradation flag.
    //
    // The flag is process-global so tests (and any future long-running
    // extractor wrapper) need an explicit reset to avoid cross-test
    // contamination. Package-private to keep it out of the public CLI
    // surface; production code never calls it.
    // -----------------------------------------------------------------------
    static void resetDegradationStateForTests() {
        bindingResolutionDegraded = false;
    }

    static boolean isBindingResolutionDegradedForTests() {
        return bindingResolutionDegraded;
    }

    /// Parse Java source with JDT binding resolution enabled (issue #12).
    /// On OutOfMemoryError, fall back to binding-disabled parsing and emit a
    /// stderr warning. The fallback is sticky for the rest of the process to
    /// avoid repeated OOM cycles on subsequent files.
    private static CompilationUnit parseSource(String source, String filePath) {
        if (!bindingResolutionDegraded) {
            try {
                return parseWithBindings(source, true);
            } catch (OutOfMemoryError oom) {
                System.err.println(
                    "topo-extract-java: OutOfMemoryError during JDT binding resolution at " +
                    filePath + " — falling back to binding-disabled parsing for the rest of this run. " +
                    "Cross-file qualified name resolution and method-reference / var-inference " +
                    "classification will degrade until the JVM heap is increased.");
                bindingResolutionDegraded = true;
                // Fall through to binding-disabled retry
            }
        }
        return parseWithBindings(source, false);
    }

    private static CompilationUnit parseWithBindings(String source, boolean resolveBindings) {
        ASTParser parser = ASTParser.newParser(AST.getJLSLatest());
        parser.setKind(ASTParser.K_COMPILATION_UNIT);
        parser.setSource(source.toCharArray());
        parser.setResolveBindings(resolveBindings);
        // setEnvironment(...) with empty classpath / sourcepath is required for
        // binding resolution on standalone source (no IProject backing).
        // Unit name and encoding stay default; binding resolution still works
        // for in-file references and JDK names with this minimal environment.
        if (resolveBindings) {
            parser.setEnvironment(
                new String[0],     // classpathEntries
                new String[0],     // sourcepathEntries
                new String[0],     // encodings
                true);             // includeRunningVMBootclasspath
            parser.setUnitName("Source.java");
            parser.setBindingsRecovery(true);
            parser.setStatementsRecovery(true);
        }

        Map<String, String> options = new HashMap<>();
        options.put("org.eclipse.jdt.core.compiler.source", "17");
        options.put("org.eclipse.jdt.core.compiler.compliance", "17");
        options.put("org.eclipse.jdt.core.compiler.codegen.targetPlatform", "17");
        parser.setCompilerOptions(options);

        ASTNode result = parser.createAST(null);
        if (result instanceof CompilationUnit cu) {
            return cu;
        }
        return null;
    }

    private static void collectFromType(
            AbstractTypeDeclaration typeDecl,
            String enclosingName,
            Set<String> requestedFunctions,
            JsonArray allFunctions,
            JsonArray allTypes) {

        String typeName = typeDecl.getName().getIdentifier();
        String qualifiedTypeName = enclosingName.isEmpty() ? typeName : enclosingName + "." + typeName;

        // Build the TranspileType entry for this declaration. The shape mirrors
        // the C++ `TranspileType` deserializer in TranspileModelJson.cpp:
        //   { qualifiedName, fields:[{type,name}], baseClasses:[TypeNode], fidelity }
        // baseClasses is omitted entirely when empty (matches the C++
        // omit-when-empty idiom and keeps pre-inheritance JSON byte-identical).
        JsonObject typeEntry = new JsonObject();
        typeEntry.addProperty("qualifiedName", qualifiedTypeName);

        JsonArray fieldArr = new JsonArray();
        JsonArray baseArr = new JsonArray();
        // Parallel class-vs-interface discriminator, same length & order as
        // baseArr. Lets JavaEmitter place each base exactly: a class base in
        // `extends`, every interface base in `implements`. Omitted (like
        // baseClasses) when empty so pre-discriminator JSON stays byte-identical.
        JsonArray baseKindArr = new JsonArray();
        // Declaration-level generic parameters of this type. A bound is lossy
        // (type-parameter names only) → downgrade the TYPE fidelity. Unlike
        // functions, TranspileType has no `unsupported` vector, so the dropped
        // bound is surfaced solely via fidelity. Collected here, attached after
        // the field/base assembly so the omit-when-empty rule is uniform.
        JsonArray typeParamArr = new JsonArray();
        List<String> typeBoundDowngrades = new ArrayList<>();
        if (typeDecl instanceof TypeDeclaration tdGen) {
            typeParamArr = convertTypeParameters(tdGen.typeParameters(), typeBoundDowngrades);
        }

        // Inheritance hierarchy. A class TypeDeclaration → getSuperclassType()
        // (a single `extends` target, a CLASS) + superInterfaceTypes() (the
        // `implements` list, INTERFACEs). An interface TypeDeclaration
        // (isInterface()==true) has a null superclass and its
        // superInterfaceTypes() are its `extends` parents — those are
        // INTERFACEs too, so the same superclass/superInterface split tags
        // them correctly. EnumDeclaration / RecordDeclaration /
        // AnnotationTypeDeclaration only expose superInterfaceTypes()
        // (INTERFACEs). All land in baseClasses in source order; JavaEmitter
        // uses the kinds to decide `extends` vs `implements`.
        if (typeDecl instanceof TypeDeclaration td) {
            Type superClass = td.getSuperclassType();
            if (superClass != null) {
                baseArr.add(AstConverter.convertType(superClass));
                baseKindArr.add("class");
            }
            for (Object ifaceObj : td.superInterfaceTypes()) {
                if (ifaceObj instanceof Type iface) {
                    baseArr.add(AstConverter.convertType(iface));
                    baseKindArr.add("interface");
                }
            }
        } else if (typeDecl instanceof EnumDeclaration ed) {
            for (Object ifaceObj : ed.superInterfaceTypes()) {
                if (ifaceObj instanceof Type iface) {
                    baseArr.add(AstConverter.convertType(iface));
                    baseKindArr.add("interface");
                }
            }
        }

        // Fields: straightforward instance/static field declarations. A single
        // `FieldDeclaration` may declare multiple fragments (`int x, y;`); emit
        // one TranspileField per fragment, sharing the declared type.
        for (Object memberObj : typeDecl.bodyDeclarations()) {
            if (memberObj instanceof FieldDeclaration field) {
                for (Object fragObj : field.fragments()) {
                    if (fragObj instanceof VariableDeclarationFragment frag) {
                        JsonObject f = new JsonObject();
                        f.add("type", AstConverter.convertType(field.getType()));
                        f.addProperty("name", frag.getName().getIdentifier());
                        f.addProperty("fidelity", "source");
                        fieldArr.add(f);
                    }
                }
            }
        }

        typeEntry.add("fields", fieldArr);
        if (baseArr.size() > 0) {
            typeEntry.add("baseClasses", baseArr);
            typeEntry.add("baseClassKinds", baseKindArr);
        }
        if (typeParamArr.size() > 0) {
            typeEntry.add("templateParams", typeParamArr);
        }
        typeEntry.addProperty("fidelity", typeBoundDowngrades.isEmpty() ? "source" : "inferred");
        allTypes.add(typeEntry);

        // Collect methods
        for (Object memberObj : typeDecl.bodyDeclarations()) {
            if (memberObj instanceof MethodDeclaration method) {
                String methodName = method.getName().getIdentifier();
                String qualifiedMethodName = qualifiedTypeName + "." + methodName;

                // Filter by requested functions (empty = include all)
                if (!requestedFunctions.isEmpty() && !requestedFunctions.contains(qualifiedMethodName)) {
                    continue;
                }

                JsonObject fn = convertMethod(method, qualifiedMethodName);
                allFunctions.add(fn);
            }

            // Recurse into nested types
            if (memberObj instanceof AbstractTypeDeclaration nestedType) {
                collectFromType(nestedType, qualifiedTypeName, requestedFunctions, allFunctions, allTypes);
            }
        }
    }

    private static JsonObject convertMethod(MethodDeclaration method, String qualifiedName) {
        JsonObject fn = new JsonObject();
        fn.addProperty("qualifiedName", qualifiedName);
        fn.add("returnType", AstConverter.convertType(method.getReturnType2()));
        fn.add("params", convertParams(method.parameters()));
        fn.add("body", convertMethodBody(method.getBody()));

        // Method-level generics. Omit the key when there are none so a
        // non-generic method stays byte-identical to pre-change output.
        List<String> boundDowngrades = new ArrayList<>();
        JsonArray tps = convertTypeParameters(method.typeParameters(), boundDowngrades);
        if (tps.size() > 0) {
            fn.add("templateParams", tps);
        }

        JsonArray unsupported = new JsonArray();
        for (String d : boundDowngrades) {
            unsupported.add(d);
        }
        fn.add("unsupported", unsupported);
        // Dropping a generic bound is a lossy recovery → downgrade fidelity.
        fn.addProperty("fidelity", boundDowngrades.isEmpty() ? "source" : "inferred");

        // Declared checked-exception types (Java `throws` clause).
        JsonArray throwsArr = new JsonArray();
        for (Object exObj : method.thrownExceptionTypes()) {
            if (exObj instanceof Type exType) {
                throwsArr.add(AstConverter.convertType(exType));
            }
        }
        if (throwsArr.size() > 0) {
            fn.add("throwsClause", throwsArr);
        }
        return fn;
    }

    // Declaration-level generic parameters. Wire shape mirrors the C++
    // TemplateParamDecl deserializer:
    //   `{kind:"type", name, bound?: TypeNode, bounds?: [TypeNode]}`
    // (omit-when-empty handled by the caller). Single-bound `<T extends X>`
    // writes the legacy `bound` key (byte-identical wire shape); intersection
    // `<T extends A & B>` writes the new `bounds: [TypeNode]` list. Wildcards
    // / variance are not type-parameter-decl concerns.
    private static JsonArray convertTypeParameters(List<?> typeParams, List<String> boundDowngrades) {
        JsonArray arr = new JsonArray();
        for (Object tpObj : typeParams) {
            if (tpObj instanceof TypeParameter tp) {
                String name = tp.getName().getIdentifier();
                JsonObject e = new JsonObject();
                e.addProperty("kind", "type");
                e.addProperty("name", name);
                List<?> bounds = tp.typeBounds();
                if (bounds.size() == 1 && bounds.get(0) instanceof Type bt) {
                    e.add("bound", AstConverter.convertType(bt));
                } else if (bounds.size() >= 2) {
                    JsonArray boundArr = new JsonArray();
                    boolean allTypes = true;
                    for (Object b : bounds) {
                        if (b instanceof Type bt2) {
                            boundArr.add(AstConverter.convertType(bt2));
                        } else {
                            allTypes = false;
                            break;
                        }
                    }
                    if (allTypes) {
                        e.add("bounds", boundArr);
                    } else {
                        boundDowngrades.add(
                            "bounded type parameter '" + name
                                + "' — non-Type entry in typeBounds() dropped");
                    }
                }
                arr.add(e);
            }
        }
        return arr;
    }

    private static JsonArray convertParams(List<?> params) {
        JsonArray arr = new JsonArray();
        for (Object paramObj : params) {
            if (paramObj instanceof SingleVariableDeclaration svd) {
                JsonObject param = new JsonObject();
                param.addProperty("name", svd.getName().getIdentifier());
                param.add("type", AstConverter.convertType(svd.getType()));
                arr.add(param);
            }
        }
        return arr;
    }

    private static JsonArray convertMethodBody(Block body) {
        if (body == null) {
            return new JsonArray();
        }
        JsonArray stmts = new JsonArray();
        for (Object stmtObj : body.statements()) {
            if (stmtObj instanceof Statement stmt) {
                stmts.add(AstConverter.convertStatement(stmt));
            }
        }
        return stmts;
    }

    private static String readStdin() throws IOException {
        StringBuilder sb = new StringBuilder();
        try (BufferedReader reader = new BufferedReader(new InputStreamReader(System.in))) {
            char[] buf = new char[8192];
            int read;
            while ((read = reader.read(buf)) != -1) {
                sb.append(buf, 0, read);
            }
        }
        return sb.toString();
    }
}
