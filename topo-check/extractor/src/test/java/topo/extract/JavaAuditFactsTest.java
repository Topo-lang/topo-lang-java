package topo.extract;

import com.google.gson.*;
import org.junit.jupiter.api.*;
import org.junit.jupiter.api.io.TempDir;
import javax.tools.ToolProvider;
import java.nio.file.*;
import java.util.*;
import java.util.jar.*;
import static org.junit.jupiter.api.Assertions.*;

class JavaAuditFactsTest {
    @TempDir Path root;
    @BeforeEach void reset() {Main.resetDegradationStateForTests();}
    Path source(String file,String code)throws Exception {
        Path path=root.resolve(file);Files.createDirectories(path.getParent());Files.writeString(path,code);return path;
    }
    JsonObject request(Path file,Path...classpath) {
        JsonObject request=new JsonObject();JsonArray files=new JsonArray();files.add(file.toString());request.add("files",files);
        JsonArray cp=new JsonArray();for(Path path:classpath)cp.add(path.toString());request.add("classpath",cp);
        JsonArray roots=new JsonArray();roots.add(file.getParent().toString());request.add("sourcepath",roots);return request;
    }
    JsonObject run(Path file,Path...classpath)throws Exception {
        JsonObject out=Main.extract(request(file,classpath));assertTrue(out.get("analysisComplete").getAsBoolean(),out.toString());
        return out.getAsJsonObject("facts").getAsJsonObject("audit");
    }
    List<JsonObject> entries(JsonObject audit,String name) {
        List<JsonObject> out=new ArrayList<>();for(JsonElement e:audit.getAsJsonArray(name))out.add(e.getAsJsonObject());return out;
    }
    JsonObject nativeCall(JsonObject audit,String suffix) {
        return entries(audit,"nativeBindings").stream().filter(e->e.get("operation").getAsString().endsWith(suffix)).findFirst().orElseThrow(()->new AssertionError(audit));
    }
    @Test void compilerModelRejectsDifferentBootImageAndUnrepresentedReleaseLibraries()throws Exception {
        Path file=source("OldRelease.java","class OldRelease {int value(){return java.util.List.of(1).getFirst();}}\n");
        Path classes=root.resolve("classes");Files.createDirectories(classes);
        java.io.ByteArrayOutputStream compilerErrors=new java.io.ByteArrayOutputStream();
        assertNotEquals(0,ToolProvider.getSystemJavaCompiler().run(null,null,compilerErrors,
            "--release","17","-d",classes.toString(),file.toString()));
        assertTrue(compilerErrors.toString().contains("getFirst"));
        JsonObject request=request(file),compiler=new JsonObject();
        compiler.addProperty("javaHome",System.getProperty("java.home"));
        compiler.addProperty("targetRelease",Integer.toString(Runtime.version().feature()));
        request.add("compiler",compiler);
        assertTrue(Main.extract(request).get("analysisComplete").getAsBoolean());
        request.addProperty("sourceLevel","17");compiler.addProperty("targetRelease","17");
        JsonObject oldRelease=Main.extract(request);
        assertFalse(oldRelease.get("analysisComplete").getAsBoolean());
        assertTrue(oldRelease.getAsJsonArray("diagnostics").toString().contains("compiler-release-unverified"));
        request.addProperty("sourceLevel","25");compiler.addProperty("targetRelease","25");
        compiler.addProperty("javaHome",root.resolve("another-jdk").toString());
        JsonObject otherJdk=Main.extract(request);
        assertFalse(otherJdk.get("analysisComplete").getAsBoolean());
        assertTrue(otherJdk.getAsJsonArray("diagnostics").toString().contains("compiler-jdk-mismatch"));
    }
    @Test void fixedLookupDescriptorDowncallAndHandleUseRemainBoundWithoutLoadingLibrary()throws Exception {
        String missing=root.resolve("never-load-this-library.dylib").toString();
        Path file=source("NativeApi.java","""
            import java.lang.foreign.*; import java.lang.invoke.*; import java.nio.file.Path;
            class NativeApi {
                static final SymbolLookup LIB=SymbolLookup.libraryLookup(Path.of("%s"),Arena.global());
                static final FunctionDescriptor DESC=FunctionDescriptor.of(ValueLayout.JAVA_INT,ValueLayout.JAVA_INT);
                static final MethodHandle HANDLE=Linker.nativeLinker().downcallHandle(LIB.find("increment").orElseThrow(),DESC);
                static int apply(int x)throws Throwable {return (int)HANDLE.invokeExact(x);}
            }
            """.formatted(missing.replace("\\","\\\\")));
        JsonObject audit=run(file);
        assertTrue(audit.get("complete").getAsBoolean(),audit.toString());
        assertTrue(entries(audit,"dependencies").stream().anyMatch(e->e.get("path").getAsString().endsWith("/lib/modules")),audit.toString());
        for(String op:List.of("downcallHandle","invokeExact")) {
            JsonObject call=nativeCall(audit,op);assertTrue(call.get("resolved").getAsBoolean(),call.toString());
            assertEquals(missing,call.get("library").getAsString());assertEquals("increment",call.get("symbol").getAsString());
            assertEquals("JAVA_INT(JAVA_INT)",call.get("descriptor").getAsString());
        }
        assertTrue(nativeCall(audit,"libraryLookup").get("caller").getAsString().contains("<clinit>"));
        assertTrue(entries(audit,"identities").stream().anyMatch(e->e.get("kind").getAsString().equals("initializer")));
        String canonicalFile=file.toRealPath().toString();
        assertTrue(entries(audit,"identities").stream().allMatch(e->e.get("file").getAsString().equals(canonicalFile)));
        assertFalse(Files.exists(Path.of(missing)),"audit parses the FFM chain without opening the native library");
    }
    @Test void upcallRetainsExactStaticJavaCallbackIdentity()throws Exception {
        Path file=source("Callbacks.java","""
            import java.lang.foreign.*; import java.lang.invoke.*;
            class Callbacks {
                static int accept(int value){return value;}
                static long accept(long value){return value;}
                static MemorySegment callback()throws ReflectiveOperationException {
                    var method=MethodHandles.lookup().findStatic(Callbacks.class,"accept",MethodType.methodType(int.class,int.class));
                    return Linker.nativeLinker().upcallStub(method,FunctionDescriptor.of(ValueLayout.JAVA_INT,ValueLayout.JAVA_INT),Arena.global());
                }
            }
            """);
        JsonObject audit=run(file),call=nativeCall(audit,"upcallStub");assertTrue(call.get("resolved").getAsBoolean(),call.toString());
        String callback=call.get("callback").getAsString();
        JsonObject id=entries(audit,"identities").stream().filter(e->e.get("id").getAsString().equals(callback)).findFirst().orElseThrow();
        assertEquals("Callbacks.accept(int):int",id.get("signature").getAsString());
        assertFalse(id.get("owner").getAsString().isEmpty());
    }
    @Test void dynamicAddressesMutableValuesAndLoaderLookupStayUnresolved()throws Exception {
        Path file=source("Unknown.java","""
            import java.lang.foreign.*; import java.lang.invoke.*;
            class Unknown {
                static MethodHandle address(long value){return Linker.nativeLinker().downcallHandle(MemorySegment.ofAddress(value),FunctionDescriptor.ofVoid());}
                static MethodHandle mutable(String name){String symbol="first";symbol=name;return Linker.nativeLinker().downcallHandle(SymbolLookup.loaderLookup().find(symbol).orElseThrow(),FunctionDescriptor.ofVoid());}
            }
            """);
        JsonObject audit=run(file);
        for(JsonObject call:entries(audit,"nativeBindings"))if(call.get("operation").getAsString().endsWith("downcallHandle")) {
            assertFalse(call.get("resolved").getAsBoolean(),call.toString());assertFalse(call.get("unresolvedReason").getAsString().isEmpty());
        }
        assertTrue(entries(audit,"nativeBindings").stream().anyMatch(e->!e.get("resolved").getAsBoolean()));
    }
    Path dependency(String directory,int value)throws Exception {
        Path src=source(directory+"/source/dep/Log.java","package dep; public final class Log { public static int write(){return "+value+";} }");
        Path classes=root.resolve(directory+"/classes");Files.createDirectories(classes);
        assertEquals(0,ToolProvider.getSystemJavaCompiler().run(null,null,null,"-proc:none","-d",classes.toString(),src.toString()));
        Path jar=root.resolve(directory+"/log.jar");
        try(JarOutputStream out=new JarOutputStream(Files.newOutputStream(jar))) {
            out.putNextEntry(new JarEntry("dep/Log.class"));Files.copy(classes.resolve("dep/Log.class"),out);out.closeEntry();
        }
        return jar;
    }
    @Test void sameNamedClasspathClassesBindToTheSelectedJarAndChangeIdentityOnReplacement()throws Exception {
        Path first=dependency("first",1),second=dependency("second",2);
        Path file=source("client/App.java","class App {int run(){return dep.Log.write();}}");
        JsonObject a=run(file,first,second),b=run(file,second,first);
        String aTarget=entries(a,"references").stream().map(e->e.get("target").getAsString()).filter(s->s.contains("dep/Log")).findFirst().orElseThrow(()->new AssertionError(a));
        String bTarget=entries(b,"references").stream().map(e->e.get("target").getAsString()).filter(s->s.contains("dep/Log")).findFirst().orElseThrow();
        assertTrue(aTarget.startsWith(first.toRealPath()+"#"),aTarget);assertTrue(bTarget.startsWith(second.toRealPath()+"#"),bTarget);
        assertNotEquals(aTarget,bTarget);assertEquals(2,entries(a,"dependencies").stream().filter(e->e.get("path").getAsString().endsWith(".jar")).count());
        assertTrue(entries(a,"identities").stream().noneMatch(e->e.get("name").getAsString().startsWith("dep.")),"opaque dependencies are summarized, not copied into workspace identities");
    }
    @Test void staticInitializersAndLambdaOwnersRemainSeparateAndNeverExecute()throws Exception {
        Path sentinel=root.resolve("initializer-ran");
        Path file=source("Initializers.java","""
            import java.nio.file.*; class Initializers {
                static {try {Files.writeString(Path.of("%s"),"ran");}catch(Exception e){throw new RuntimeException(e);}}
                Runnable callback(){return () -> System.out.println("callback");}
            }
            """.formatted(sentinel.toString().replace("\\","\\\\")));
        JsonObject audit=run(file);assertFalse(Files.exists(sentinel));
        assertTrue(entries(audit,"identities").stream().anyMatch(e->e.get("kind").getAsString().equals("initializer")));
        assertTrue(entries(audit,"identities").stream().anyMatch(e->e.get("kind").getAsString().equals("lambda")));
        assertTrue(entries(audit,"references").stream().allMatch(e->!e.get("caller").getAsString().isEmpty()));
    }
    @Test void enumConstructionGetsAStaticOwnerAndInputMutationInvalidatesSnapshot()throws Exception {
        Path file=source("Mode.java","enum Mode { FIRST(1); Mode(int value){} }");
        JsonObject audit=run(file);
        assertTrue(audit.get("complete").getAsBoolean(),audit.toString());
        assertTrue(entries(audit,"references").stream().allMatch(e->!e.get("caller").getAsString().isEmpty()));
        assertEquals("bounded-process",audit.get("analysisIsolation").getAsString());
        assertTrue(audit.getAsJsonObject("inputDigests").has(file.toRealPath().toString()));
        AuditSnapshot snapshot=new AuditSnapshot(Map.of(file.toString(),Files.readString(file)),new String[0],new String[0]);
        Files.writeString(file,"enum Mode { REPLACED }");
        snapshot.finish(audit);
        assertFalse(audit.get("complete").getAsBoolean());
        assertTrue(audit.getAsJsonArray("unresolved").toString().contains("Input changed during Java analysis"));
    }
    @Test void dependencyOwnershipPreservesExternalCoordinatesAndRejectsProjectBinaryEscape()throws Exception {
        Path jar=dependency("third-party",1);
        Path file=source("client/Client.java","class Client {int run(){return dep.Log.write();}}");
        JsonObject request=request(file,jar),metadata=new JsonObject();
        metadata.addProperty("path",jar.toString());metadata.addProperty("kind","external");
        metadata.addProperty("coordinate","example:log");metadata.addProperty("version","1.0");
        JsonArray dependencies=new JsonArray();dependencies.add(metadata);request.add("dependencies",dependencies);
        JsonObject audit=Main.extract(request).getAsJsonObject("facts").getAsJsonObject("audit");
        assertTrue(audit.get("complete").getAsBoolean(),audit.toString());
        assertTrue(entries(audit,"dependencies").stream().anyMatch(d->d.get("coordinate").getAsString().equals("example:log")));
        request.remove("dependencies");
        JsonObject unknown=Main.extract(request).getAsJsonObject("facts").getAsJsonObject("audit");
        assertFalse(unknown.get("complete").getAsBoolean());
        assertTrue(unknown.get("unresolved").toString().contains("ownership is unresolved"));
        Path local=file.getParent().resolve("vendor/log.jar");Files.createDirectories(local.getParent());Files.copy(jar,local);
        JsonObject localRequest=request(file,local);metadata.addProperty("path",local.toString());localRequest.add("dependencies",dependencies);
        JsonObject project=Main.extract(localRequest).getAsJsonObject("facts").getAsJsonObject("audit");
        assertFalse(project.get("complete").getAsBoolean());
        assertTrue(entries(project,"dependencies").stream().anyMatch(d->d.get("project").getAsBoolean()));
    }
}
