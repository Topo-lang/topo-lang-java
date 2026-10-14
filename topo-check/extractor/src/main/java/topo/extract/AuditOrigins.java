package topo.extract;

import com.google.gson.*;
import org.eclipse.jdt.core.IJavaElement;
import org.eclipse.jdt.core.dom.*;
import java.io.IOException;
import java.nio.file.*;
import java.util.*;
import java.util.jar.JarFile;

/** Resolve declaration identities from the environment supplied to this JDT batch. */
final class AuditOrigins {
    private final Map<String,String> sourceTypes = new HashMap<>();
    private final List<String> classpath;
    private final List<String> sourcepath;
    private final Set<String> analyzedFiles;
    private final LinkedHashMap<String,JsonObject> dependencies = new LinkedHashMap<>();
    private final LinkedHashSet<String> inputs = new LinkedHashSet<>();
    private final Map<String,String> resolved = new HashMap<>();
    private final Map<String,String> dependencyByType = new HashMap<>();
    private final List<String> problems=new ArrayList<>();
    private final Map<String,JsonObject> dependencyMetadata=new HashMap<>();
    private final String projectRoot;

    AuditOrigins(Map<String,CompilationUnit> units, String[] classpath, String[] sourcepath,JsonObject request) {
        this.classpath=Arrays.stream(classpath).map(AuditOrigins::canonical).toList();
        this.sourcepath=Arrays.stream(sourcepath).map(AuditOrigins::canonical).toList();
        projectRoot=request.has("projectRoot")?canonical(request.get("projectRoot").getAsString()):"";
        if(request.has("dependencies"))for(JsonElement entry:request.getAsJsonArray("dependencies")) {
            JsonObject d=entry.getAsJsonObject();String path=canonical(d.get("path").getAsString());
            if(dependencyMetadata.putIfAbsent(path,d)!=null)problems.add("Duplicate Java dependency ownership: "+path);
        }
        this.analyzedFiles=new HashSet<>();
        for (var entry:units.entrySet()) {
            String file=canonical(entry.getKey()); inputs.add(file); analyzedFiles.add(file);
            entry.getValue().accept(new ASTVisitor() {
                @Override public void preVisit(ASTNode n) {
                    ITypeBinding b=n instanceof AbstractTypeDeclaration t ? t.resolveBinding()
                        : n instanceof AnonymousClassDeclaration a ? a.resolveBinding() : null;
                    if(b!=null) sourceTypes.put(b.getTypeDeclaration().getKey(),file);
                }
            });
        }
        for(String path:this.classpath) dependency(path,false,"");
        analyzerInput(JavaAuditFacts.class);
        analyzerInput(ASTParser.class);
        analyzerInput(com.google.gson.Gson.class);
        Path jdk=Path.of(System.getProperty("java.home"));
        for(Path input:List.of(jdk.resolve("release"),jdk.resolve("lib/modules"),
                jdk.resolve("bin/java"),jdk.resolve("bin/java.exe")))
            if(Files.isRegularFile(input))inputs.add(canonical(input.toString()));
    }
    private void analyzerInput(Class<?> analyzerClass) {
        try {
            Path artifact=Path.of(analyzerClass.getProtectionDomain().getCodeSource().getLocation().toURI());
            if(Files.isRegularFile(artifact))inputs.add(canonical(artifact.toString()));
            else if(Files.isDirectory(artifact))try(var files=Files.walk(artifact)) {
                files.filter(Files::isRegularFile).sorted().forEach(p->inputs.add(canonical(p.toString())));
            }
            else problems.add("Analyzer code source is unavailable: "+artifact);
        } catch(Exception ex) { problems.add("Cannot identify analyzer code source for "+analyzerClass.getName()+": "+ex.getMessage()); }
    }
    static String canonical(String value) {
        try { return Path.of(value).toRealPath().toString(); }
        catch(IOException ex) { return Path.of(value).toAbsolutePath().normalize().toString(); }
    }
    private void dependency(String path,boolean project,String version) {
        if(dependencies.containsKey(path))return;
        JsonObject metadata=dependencyMetadata.get(path);
        String ownership=metadata==null?"unknown":metadata.get("kind").getAsString();
        if(!Set.of("unknown","project","external").contains(ownership))ownership="unknown";
        String coordinate=metadata!=null && metadata.has("coordinate")?metadata.get("coordinate").getAsString():"";
        if(metadata!=null && metadata.has("version"))version=metadata.get("version").getAsString();
        boolean jdk=path.equals(canonical(Path.of(System.getProperty("java.home"),"lib/modules").toString()));
        if(jdk){ownership="external";coordinate="jdk:"+System.getProperty("java.vendor");version=System.getProperty("java.runtime.version");}
        boolean underProject=!projectRoot.isEmpty() && Path.of(path).startsWith(projectRoot);
        boolean underSource=sourcepath.stream().anyMatch(p->Path.of(path).startsWith(p));
        if(!jdk && (underProject || underSource || project || ownership.equals("project")))ownership="project";
        if(ownership.equals("external") && coordinate.isEmpty())ownership="unknown";
        if(ownership.equals("unknown"))problems.add("Java dependency ownership is unresolved: "+path);
        if(ownership.equals("project"))problems.add("Project dependency requires analyzed source coverage; binary classpath is insufficient: "+path);
        JsonObject d=new JsonObject();d.addProperty("id",path);d.addProperty("path",path);
        d.addProperty("version",version);d.addProperty("digest","");d.addProperty("project",ownership.equals("project"));
        d.addProperty("ownership",ownership);d.addProperty("coordinate",coordinate);
        dependencies.putIfAbsent(path,d);if(Files.isRegularFile(Path.of(path)))inputs.add(path);
    }
    String origin(ITypeBinding binding) {
        if(binding==null || binding.isRecovered()) return "";
        ITypeBinding type=binding.getTypeDeclaration();String key=type.getKey();
        if(resolved.containsKey(key)) return resolved.get(key);
        String path=sourceTypes.get(key);
        if(path!=null) {resolved.put(key,path);return path;}
        if(type.isFromSource()) {
            ITypeBinding outer=type;while(outer.getDeclaringClass()!=null) outer=outer.getDeclaringClass();
            String relative=outer.getQualifiedName().replace('.','/')+".java";
            for(String root:sourcepath) if(Files.isRegularFile(Path.of(root,relative))) {
                path=canonical(Path.of(root,relative).toString());inputs.add(path);
                resolved.put(key,path);return path;
            }
            return "";
        }
        // JDT's Java element, when present, is the selected declaration's
        // package root. Prefer it over guessing from a simple type name.
        IJavaElement element=type.getJavaElement();
        if(element!=null) {
            IJavaElement root=element.getAncestor(IJavaElement.PACKAGE_FRAGMENT_ROOT);
            if(root!=null && root.getPath()!=null) {
                String candidate=root.getPath().toOSString();
                if(Files.exists(Path.of(candidate))) {
                    path=canonical(candidate);dependency(path,false,"");dependencyByType.put(key,path);resolved.put(key,path);return path;
                }
            }
        }
        String binary=type.getBinaryName();
        if(binary==null || binary.isEmpty()) return "";
        String entry=binary.replace('.','/')+".class";
        // The parser includes this JVM's boot image. Never load the target
        // class to discover its origin; read the JRT file system instead.
        try {
            var module=type.getModule();
            if(module!=null && !module.getName().isEmpty()) {
                Path jrt=FileSystems.getFileSystem(java.net.URI.create("jrt:/")).getPath("/modules",module.getName(),entry);
                if(Files.isRegularFile(jrt)) {
                    path=canonical(Path.of(System.getProperty("java.home"),"lib","modules").toString());
                    dependency(path,false,System.getProperty("java.runtime.version"));
                    dependencyByType.put(key,path);
                    inputs.add(canonical(Path.of(System.getProperty("java.home"),"release").toString()));
                    resolved.put(key,path);return path;
                }
            }
        } catch(RuntimeException ignored) { /* An unavailable origin remains explicitly unresolved. */ }
        // ASTParser's explicit classpath is ordered. This is a binding-backed
        // binary name, not a source-name allowlist. Preserve the first actual
        // candidate, including multi-release JAR selection by the running JDK.
        for(String root:classpath) {
            Path p=Path.of(root);
            if(Files.isDirectory(p) && Files.isRegularFile(p.resolve(entry))) {
                path=canonical(p.resolve(entry).toString());inputs.add(path);dependencyByType.put(key,root);resolved.put(key,path);return path;
            }
            if(Files.isRegularFile(p)) try(JarFile jar=new JarFile(p.toFile(),false,JarFile.OPEN_READ,Runtime.version())) {
                if(jar.getJarEntry(entry)!=null) {dependencyByType.put(key,root);resolved.put(key,root);return root;}
            } catch(IOException ignored) { /* JDT/environment diagnostics retain malformed classpaths. */ }
        }
        return "";
    }
    String typeId(ITypeBinding type) {
        if(type==null) return "";
        String origin=origin(type);return origin.isEmpty()?"":origin+"#"+type.getTypeDeclaration().getKey();
    }
    String methodId(IMethodBinding method) {
        if(method==null) return "";
        String origin=origin(method.getDeclaringClass());
        return origin.isEmpty()?"":origin+"#"+method.getMethodDeclaration().getKey();
    }
    String dependencyId(ITypeBinding type) {
        if(type==null || type.isFromSource())return "";
        origin(type);return dependencyByType.getOrDefault(type.getTypeDeclaration().getKey(),"");
    }
    boolean analyzed(String file) {return analyzedFiles.contains(canonical(file));}
    JsonArray dependencies() {JsonArray out=new JsonArray();dependencies.values().forEach(out::add);return out;}
    JsonArray inputs() {JsonArray out=new JsonArray();inputs.forEach(out::add);return out;}
    List<String> problems() {return problems;}
}
