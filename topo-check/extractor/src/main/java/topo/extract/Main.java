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
import java.nio.charset.StandardCharsets;
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
        JsonObject result;
        try {
            JsonObject request = JsonParser.parseString(readStdin()).getAsJsonObject();
            result = extract(request);
        } catch (RuntimeException error) {
            JsonArray diagnostics = new JsonArray();
            diagnostic(diagnostics, "error", "invalid-request", error.getMessage(), "", 0);
            result = failedRequest(diagnostics);
        }
        // This is a UTF-8 byte protocol, independent of the console code page.
        System.out.write((GSON.toJson(result) + "\n").getBytes(StandardCharsets.UTF_8));
        System.out.flush();
    }

    // Package-visible for deterministic real-JDT tests without replacing stdio.
    static JsonObject extract(JsonObject request) throws IOException {
        JsonArray diagnostics = new JsonArray();
        for (String key : List.of("files", "functions", "classpath", "sourcepath")) {
            if (!request.has(key)) continue;
            if (!request.get(key).isJsonArray()) {
                diagnostic(diagnostics, "error", "invalid-request", key + " must be an array", "", 0);
                continue;
            }
            for (JsonElement value : request.getAsJsonArray(key))
                if (!value.isJsonPrimitive() || !value.getAsJsonPrimitive().isString() || value.getAsString().isBlank())
                    diagnostic(diagnostics, "error", "invalid-request", key + " entries must be non-empty strings", "", 0);
        }
        if (request.has("sourceLevel") && (!request.get("sourceLevel").isJsonPrimitive() || !request.get("sourceLevel").getAsJsonPrimitive().isString()))
            diagnostic(diagnostics, "error", "invalid-request", "sourceLevel must be a string", "", 0);
        if (!diagnostics.isEmpty()) return failedRequest(diagnostics);
        if (request.has("compiler")) {
            JsonObject compiler = request.getAsJsonObject("compiler");
            String home = compiler.get("javaHome").getAsString();
            String release = compiler.get("targetRelease").getAsString();
            if (!AuditOrigins.canonical(home).equals(AuditOrigins.canonical(System.getProperty("java.home"))))
                diagnostic(diagnostics, "error", "compiler-jdk-mismatch", "The parser JDK differs from the selected compiler JDK", home, 0);
            if (!release.isEmpty() && !release.equals(Integer.toString(Runtime.version().feature())))
                diagnostic(diagnostics, "error", "compiler-release-unverified", "Historical --release standard-library bindings are not represented by this parser environment: " + release, "", 0);
            if (!diagnostics.isEmpty()) return failedRequest(diagnostics);
        }
        JsonArray allFunctions = new JsonArray();
        JsonArray allTypes = new JsonArray();
        Set<String> requestedFunctions = new HashSet<>();
        if (request.has("functions")) for (JsonElement e : request.getAsJsonArray("functions"))
            requestedFunctions.add(e.getAsString().replace("::", "."));
        List<String> files = new ArrayList<>();
        Map<String, String> sources = new LinkedHashMap<>();
        if (request.has("files")) for (JsonElement e : request.getAsJsonArray("files")) {
            String path = Path.of(e.getAsString()).toAbsolutePath().normalize().toString();
            try {
                if(Files.size(Path.of(path))>16L*1024*1024)throw new IOException("Java source exceeds 16 MiB limit");
                sources.put(path, Files.readString(Path.of(path))); files.add(path);
            }
            catch (IOException ex) { diagnostic(diagnostics, "error", "source-unreadable", ex.getMessage(), path, 0); }
        }
        String level = request.has("sourceLevel") ? request.get("sourceLevel").getAsString() : "25";
        // JavaCore validates against the selected JDT distribution, not the host JVM.
        if (!org.eclipse.jdt.core.JavaCore.getAllVersions().contains(level)) {
            diagnostic(diagnostics, "error", "source-level", "Unsupported Java source level: " + level, "", 0);
            files.clear();
        }
        String[] classpath = paths(request, "classpath", diagnostics);
        String[] sourcepath = paths(request, "sourcepath", diagnostics);
        if (sourcepath.length == 0) {
            // Match each real file identity to its package root. Explicit sourcepath
            // from the build model takes precedence over this standalone default.
            Set<String> roots = new LinkedHashSet<>();
            for (String file : files) {
                Path root = Path.of(file).getParent();
                var m = java.util.regex.Pattern.compile("(?m)^\\s*package\\s+([\\w.]+)\\s*;").matcher(sources.get(file));
                if (m.find()) for (String ignored : m.group(1).split("\\.")) if (root != null) root = root.getParent();
                if (root != null) roots.add(root.toString());
            }
            sourcepath = roots.toArray(String[]::new);
        }
        Map<String, CompilationUnit> units = new LinkedHashMap<>();
        AuditSnapshot snapshot = new AuditSnapshot(sources, classpath, sourcepath);
        if (!files.isEmpty()) {
            try { parseBatch(files, level, classpath, sourcepath, !bindingResolutionDegraded, units); }
            catch (OutOfMemoryError oom) {
                bindingResolutionDegraded = true;
                units.clear();
                parseBatch(files, level, classpath, sourcepath, false, units);
            } catch (IllegalArgumentException ex) {
                diagnostic(diagnostics, "error", "parser-environment", ex.getMessage(), "", 0);
            }
        }
        SourceFacts facts = new SourceFacts(diagnostics);
        JavaAuditFacts audit = new JavaAuditFacts(units, classpath, sourcepath, request);
        for (String file : files) {
            CompilationUnit cu = units.get(file);
            if (cu == null) {
                diagnostic(diagnostics, "error", "parse-missing", "JDT produced no compilation unit", file, 0);
                continue;
            }
            SourceContext.enter(file, sources.get(file), cu);
            boolean errors = false;
            for (var problem : cu.getProblems()) {
                if (!problem.isError()) continue;
                errors = true;
                diagnostic(diagnostics, "error", "java-" + problem.getID(), problem.getMessage(), file, problem.getSourceLineNumber());
            }
            String pkg = cu.getPackage() == null ? "" : cu.getPackage().getName().getFullyQualifiedName();
            int first = allFunctions.size();
            for (Object type : cu.types()) if (type instanceof AbstractTypeDeclaration decl)
                collectFromType(decl, pkg, requestedFunctions, allFunctions, allTypes);
            if (errors || bindingResolutionDegraded) for (int i = first; i < allFunctions.size(); i++) {
                JsonObject fn = allFunctions.get(i).getAsJsonObject();
                fn.addProperty("fidelity", "inferred");
                fn.getAsJsonArray("unsupported").add(errors ? "Java compilation errors; see diagnostics" : "JDT bindings unavailable after OutOfMemoryError");
            }
            facts.collect(cu);
            audit.collect(cu);
        }
        Set<String> got = new HashSet<>();
        for (JsonElement fn : allFunctions) got.add(fn.getAsJsonObject().get("qualifiedName").getAsString());
        for (String requested : requestedFunctions) if (!got.contains(requested))
            diagnostic(diagnostics, "error", "function-missing", "Requested function not found: " + requested, "", 0);
        if (bindingResolutionDegraded)
            diagnostic(diagnostics, "error", "binding-degraded", "OutOfMemoryError during JDT binding resolution; binding-disabled fallback used", "", 0);
        JsonObject module = new JsonObject();
        module.addProperty("schemaVersion", 2);
        module.addProperty("sourceLevel", level);
        module.add("types", allTypes); module.add("functions", allFunctions);
        JsonObject sourceFacts = facts.json();
        JsonObject auditJson = audit.json(diagnostics.isEmpty());
        snapshot.finish(auditJson);
        sourceFacts.add("audit", auditJson);
        module.add("facts", sourceFacts); module.add("diagnostics", diagnostics);
        module.addProperty("analysisComplete", diagnostics.isEmpty());
        module.addProperty("runDegraded", bindingResolutionDegraded);
        if (bindingResolutionDegraded) module.addProperty("degradationReason",
            "OutOfMemoryError during JDT binding resolution; binding-disabled fallback parse used");
        return module;
    }

    private static JsonObject failedRequest(JsonArray diagnostics) {
        JsonObject out = new JsonObject(); out.addProperty("schemaVersion", 2);
        out.add("types", new JsonArray()); out.add("functions", new JsonArray());
        out.add("facts", new SourceFacts(diagnostics).json()); out.add("diagnostics", diagnostics);
        out.addProperty("analysisComplete", false); out.addProperty("runDegraded", false); return out;
    }

    static void diagnostic(JsonArray out, String severity, String code, String message, String file, int line) {
        JsonObject d = new JsonObject(); d.addProperty("severity", severity); d.addProperty("code", code);
        d.addProperty("message", message); d.addProperty("file", file); d.addProperty("line", line); out.add(d);
    }
    private static String[] paths(JsonObject request, String key, JsonArray diagnostics) {
        List<String> out = new ArrayList<>();
        if (request.has(key)) for (JsonElement e : request.getAsJsonArray(key)) {
            Path p = Path.of(e.getAsString()).toAbsolutePath().normalize();
            if (!Files.exists(p)) diagnostic(diagnostics, "error", "missing-" + key, "Missing " + key + " entry: " + p, p.toString(), 0);
            else out.add(p.toString());
        }
        return out.toArray(String[]::new);
    }
    private static void parseBatch(List<String> files, String level, String[] classpath, String[] sourcepath,
                                   boolean bindings, Map<String, CompilationUnit> units) {
        ASTParser parser = ASTParser.newParser(AST.getJLSLatest());
        parser.setKind(ASTParser.K_COMPILATION_UNIT);
        parser.setResolveBindings(bindings); parser.setBindingsRecovery(true); parser.setStatementsRecovery(true);
        String[] sourceEncodings = new String[sourcepath.length];
        Arrays.fill(sourceEncodings, StandardCharsets.UTF_8.name());
        parser.setEnvironment(classpath, sourcepath, sourceEncodings, true);
        Map<String, String> options = new HashMap<>();
        org.eclipse.jdt.core.JavaCore.setComplianceOptions(level, options);
        parser.setCompilerOptions(options);
        String[] fileEncodings = new String[files.size()];
        Arrays.fill(fileEncodings, StandardCharsets.UTF_8.name());
        parser.createASTs(files.toArray(String[]::new), fileEncodings, new String[0], new FileASTRequestor() {
            @Override public void acceptAST(String sourceFilePath, CompilationUnit ast) {
                units.put(Path.of(sourceFilePath).toAbsolutePath().normalize().toString(), ast);
            }
        }, null);
    }
    static void resetDegradationStateForTests() { bindingResolutionDegraded = false; }
    static boolean isBindingResolutionDegradedForTests() { return bindingResolutionDegraded; }

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
        JsonObject typeEntry = SourceContext.attach(new JsonObject(), typeDecl);
        typeEntry.addProperty("qualifiedName", qualifiedTypeName);

        JsonArray fieldArr = new JsonArray();
        JsonArray baseArr = new JsonArray();
        // Parallel class-vs-interface discriminator, same length & order as
        // baseArr. Lets JavaEmitter place each base exactly: a class base in
        // `extends`, every interface base in `implements`. Omitted (like
        // baseClasses) when empty so pre-discriminator JSON stays byte-identical.
        JsonArray baseKindArr = new JsonArray();
        // Declaration-level generic parameters of this type. A bound is lossy
        // (type-parameter names only) → downgrade the TYPE fidelity and record
        // the dropped bound in its unsupported summary for verification.
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
                        JsonObject f = SourceContext.attach(new JsonObject(), frag);
                        f.add("type", AstConverter.convertType(field.getType()));
                        f.addProperty("name", frag.getName().getIdentifier());
                        f.addProperty("fidelity", "source");
                        if (frag.getInitializer() != null) f.add("init", AstConverter.convertExpression(frag.getInitializer()));
                        fieldArr.add(f);
                    }
                }
            }
        }

        typeEntry.add("fields", fieldArr);
        JsonArray initializers = new JsonArray();
        for (Object member : typeDecl.bodyDeclarations()) if (member instanceof Initializer init)
            initializers.add(AstConverter.convertStatement(init.getBody()));
        if (!initializers.isEmpty()) typeEntry.add("initializers", initializers);
        if (baseArr.size() > 0) {
            typeEntry.add("baseClasses", baseArr);
            typeEntry.add("baseClassKinds", baseKindArr);
        }
        if (typeParamArr.size() > 0) {
            typeEntry.add("templateParams", typeParamArr);
        }
        JsonArray typeUnsupported = new JsonArray();
        for (String reason : typeBoundDowngrades) typeUnsupported.add(reason);
        if (typeDecl instanceof EnumDeclaration enumType && !enumType.enumConstants().isEmpty())
            typeUnsupported.add("enum constants and enum identity require a Java-specific type model");
        if (typeDecl instanceof RecordDeclaration)
            typeUnsupported.add("record components and generated record members require a Java-specific type model");
        collectUnsupported(initializers, typeUnsupported);
        if (!typeUnsupported.isEmpty()) typeEntry.add("unsupported", typeUnsupported);
        typeEntry.addProperty("fidelity", typeUnsupported.isEmpty() ? "source" : "inferred");
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
        JsonObject fn = SourceContext.attach(new JsonObject(), method);
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
        for (Object parameter : method.parameters()) if (parameter instanceof SingleVariableDeclaration v && v.isVarargs())
            unsupported.add("variadic parameter '" + v.getName() + "' requires a variadic-call model");
        collectUnsupported(fn.get("body"), unsupported);
        fn.add("unsupported", unsupported);
        int flags = method.resolveBinding() == null ? method.getModifiers() : method.resolveBinding().getModifiers();
        fn.addProperty("accessModifier", Modifier.isPublic(flags) ? "public" : Modifier.isProtected(flags) ? "protected" : Modifier.isPrivate(flags) ? "private" : "");
        SourceContext.binding(fn, method.resolveBinding());
        // Dropping a generic bound is a lossy recovery → downgrade fidelity.
        fn.addProperty("fidelity", unsupported.isEmpty() ? "source" : "inferred");

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
                JsonObject type = AstConverter.convertType(svd.getType());
                if (svd.isVarargs()) {
                    JsonArray names = type.getAsJsonArray("nameParts");
                    names.set(names.size() - 1, new com.google.gson.JsonPrimitive(names.get(names.size() - 1).getAsString() + "[]"));
                }
                param.add("type", type);
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

    static void collectUnsupported(JsonElement element, JsonArray out) {
        if (element == null || element.isJsonNull()) return;
        if (element.isJsonArray()) { for (JsonElement e : element.getAsJsonArray()) collectUnsupported(e, out); }
        else if (element.isJsonObject()) {
            JsonObject obj = element.getAsJsonObject();
            if (obj.has("kind") && "unsupported".equals(obj.get("kind").getAsString()))
                out.add(obj.has("description") ? obj.get("description").getAsString() : "unsupported AST node");
            for (var e : obj.entrySet()) if (e.getValue().isJsonObject() || e.getValue().isJsonArray()) collectUnsupported(e.getValue(), out);
        }
    }

    private static String readStdin() throws IOException {
        StringBuilder sb = new StringBuilder();
        try (BufferedReader reader = new BufferedReader(new InputStreamReader(System.in, StandardCharsets.UTF_8))) {
            char[] buf = new char[8192];
            int read;
            while ((read = reader.read(buf)) != -1) {
                if(sb.length()+read>8*1024*1024)throw new IOException("Java request exceeds 8 MiB limit");
                sb.append(buf, 0, read);
            }
        }
        return sb.toString();
    }
}
