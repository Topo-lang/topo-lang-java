package topo.extract;

import com.google.gson.*;
import org.junit.jupiter.api.*;
import org.junit.jupiter.api.io.TempDir;
import java.nio.file.*;
import java.util.*;
import javax.tools.ToolProvider;
import static org.junit.jupiter.api.Assertions.*;

class Java25AnalysisTest {
    @TempDir Path root;
    @BeforeEach void reset() { Main.resetDegradationStateForTests(); }
    Path source(String name,String code)throws Exception {Path p=root.resolve(name);Files.createDirectories(p.getParent());Files.writeString(p,code);return p;}
    JsonObject run(Path...files)throws Exception {
        JsonObject request=new JsonObject();JsonArray paths=new JsonArray();for(Path p:files)paths.add(p.toString());request.add("files",paths);return Main.extract(request);
    }
    List<JsonObject> nodes(JsonElement tree,String kind) {
        List<JsonObject> out=new ArrayList<>();walk(tree,kind,out);return out;
    }
    void walk(JsonElement e,String kind,List<JsonObject> out) {
        if(e.isJsonArray())for(JsonElement item:e.getAsJsonArray())walk(item,kind,out);
        if(e.isJsonObject()) {JsonObject o=e.getAsJsonObject();if(o.has("kind")&&kind.equals(o.get("kind").getAsString()))out.add(o);
            for(var entry:o.entrySet())if(!entry.getKey().equals("facts"))walk(entry.getValue(),kind,out);}
    }
    @Test void java25CompactSourceAndModuleImportsAreAccepted()throws Exception {
        JsonObject result=run(source("Hello.java","import module java.base;\nvoid main() { IO.println(\"hello\"); }"));
        assertTrue(result.get("analysisComplete").getAsBoolean(),result.toString());
        assertEquals("25",result.get("sourceLevel").getAsString());
        assertEquals(1,result.getAsJsonArray("functions").size(),"implicit class methods must be collected once");
    }
    @Test void crossFileInstanceBindingAndOverloadSignature()throws Exception {
        Path a=source("demo/A.java","package demo; public class A { public int f(int x) {return x;} public String f(String x){return x;} }");
        Path b=source("demo/B.java","package demo; class B { int run(A a) { return a.f(3); } }");
        JsonObject result=run(a,b);assertTrue(result.get("analysisComplete").getAsBoolean(),result.toString());
        JsonObject call=nodes(result,"call").stream().filter(n->n.get("callee").getAsString().equals("f")).findFirst().orElseThrow();
        assertEquals("demo.A.f(int):int",call.get("resolvedSignature").getAsString());
        assertEquals("a",call.getAsJsonObject("receiver").get("name").getAsString());
        assertTrue(result.getAsJsonObject("facts").getAsJsonArray("calls").toString().contains("demo.A.f"));
    }
    @Test void builderReceiverChainAndLocationsSurvive()throws Exception {
        String code="class Chain { Chain step(){return this;} int done(){return 2;} int f(){return step().step().done();} }";
        Path file=source("Chain.java",code);JsonObject result=run(file);
        JsonObject done=nodes(result,"call").stream().filter(n->n.get("callee").getAsString().equals("done")).findFirst().orElseThrow();
        assertEquals("call",done.getAsJsonObject("receiver").get("kind").getAsString());
        assertEquals("call",done.getAsJsonObject("receiver").getAsJsonObject("receiver").get("kind").getAsString());
        JsonObject span=done.getAsJsonObject("sourceSpan");
        assertEquals("step().step().done()",code.substring(span.get("startOffset").getAsInt(),span.get("endOffset").getAsInt()));
        assertEquals(file.toString(),span.get("file").getAsString());
    }
    @Test void lambdaAndBoundMethodReferenceRetainEvaluationTime()throws Exception {
        JsonObject result=run(source("Callbacks.java","import java.util.function.*; class Callbacks { Callbacks obtain(){return this;} int apply(int x){return x;} IntUnaryOperator f(int k){ return x -> x+k; } IntUnaryOperator g(){return obtain()::apply;} }"));
        assertTrue(result.get("analysisComplete").getAsBoolean(),result.toString());
        JsonObject lambda=nodes(result,"lambda").getFirst();assertTrue(lambda.getAsJsonArray("captures").toString().contains("k"));
        JsonObject ref=nodes(result,"methodref").getFirst();assertEquals("bound",ref.get("referenceKind").getAsString());
        assertTrue(ref.get("evaluateReceiverAtCreation").getAsBoolean());assertEquals("call",ref.getAsJsonObject("receiver").get("kind").getAsString());
        assertEquals("Callbacks.apply",ref.get("resolvedQualifiedName").getAsString());
    }
    @Test void externalClasspathResolvesAndMissingClasspathIsError()throws Exception {
        Path dep=source("dep/Api.java","package dep; public class Api {public int read(){return 7;}}"),classes=root.resolve("classes");Files.createDirectories(classes);
        assertEquals(0,ToolProvider.getSystemJavaCompiler().run(null,null,null,"-d",classes.toString(),dep.toString()));
        Path app=source("client/App.java","package client; class App {int run(dep.Api api){return api.read();}}");
        JsonObject request=new JsonObject();JsonArray files=new JsonArray();files.add(app.toString());request.add("files",files);
        JsonArray roots=new JsonArray();roots.add(root.resolve("client").toString());request.add("sourcepath",roots);
        JsonObject missing=Main.extract(request);assertFalse(missing.get("analysisComplete").getAsBoolean());assertFalse(missing.getAsJsonArray("diagnostics").isEmpty());
        JsonArray cp=new JsonArray();cp.add(classes.toString());request.add("classpath",cp);
        JsonObject ok=Main.extract(request);assertTrue(ok.get("analysisComplete").getAsBoolean(),ok.toString());
        assertTrue(ok.getAsJsonObject("facts").getAsJsonArray("calls").toString().contains("dep.Api.read"));
    }
    @Test void unsupportedNestedExpressionAndParseErrorsAreVisible()throws Exception {
        JsonObject result=run(source("Loss.java","class Loss { Object f(){return new int[]{1,2};} }"));
        JsonObject fn=result.getAsJsonArray("functions").get(0).getAsJsonObject();
        assertFalse(fn.getAsJsonArray("unsupported").isEmpty());assertEquals("inferred",fn.get("fidelity").getAsString());
        JsonObject bad=run(source("Bad.java","class Bad { void f( { }"));assertFalse(bad.get("analysisComplete").getAsBoolean());assertFalse(bad.getAsJsonArray("diagnostics").isEmpty());
    }
    @Test void explicitOlderLanguageLevelRejectsJava25Syntax()throws Exception {
        Path file=source("Hello.java","void main(){ IO.println(\"x\"); }");JsonObject request=new JsonObject();JsonArray files=new JsonArray();files.add(file.toString());request.add("files",files);request.addProperty("sourceLevel","21");
        JsonObject out=Main.extract(request);assertFalse(out.get("analysisComplete").getAsBoolean());
    }
    @Test void malformedEnvironmentRequestReturnsStructuredError()throws Exception {
        for(String json:List.of("{\"files\":\"File.java\"}","{\"sourceLevel\":25}","{\"sourcepath\":[\"\"]}")) {
            JsonObject out=Main.extract(JsonParser.parseString(json).getAsJsonObject());
            assertFalse(out.get("analysisComplete").getAsBoolean());assertFalse(out.getAsJsonArray("diagnostics").isEmpty());
        }
    }

    @Test void varargsEnumAndRecordLossesAreExplicit() throws Exception {
        JsonObject out=run(source("Shapes.java", "class Shapes { void log(int... xs){} } enum Color { RED, BLUE } record Point(int x,int y) {}"));
        assertTrue(out.get("analysisComplete").getAsBoolean(),out.toString());
        JsonObject fn=out.getAsJsonArray("functions").get(0).getAsJsonObject();
        assertEquals("inferred",fn.get("fidelity").getAsString());
        assertTrue(fn.getAsJsonArray("unsupported").toString().contains("variadic"));
        assertEquals("int[]",fn.getAsJsonArray("params").get(0).getAsJsonObject().getAsJsonObject("type").getAsJsonArray("nameParts").get(0).getAsString());
        for(JsonElement e:out.getAsJsonArray("types")) {
            JsonObject type=e.getAsJsonObject();String name=type.get("qualifiedName").getAsString();
            if(name.equals("Color") || name.equals("Point")) {
                assertEquals("inferred",type.get("fidelity").getAsString());assertFalse(type.getAsJsonArray("unsupported").isEmpty());assertTrue(type.has("rawSource"));
            }
        }
    }

}
