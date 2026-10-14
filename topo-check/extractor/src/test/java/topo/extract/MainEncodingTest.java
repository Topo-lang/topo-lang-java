package topo.extract;

import com.google.gson.JsonArray;
import com.google.gson.JsonObject;
import com.google.gson.JsonParser;
import org.junit.jupiter.api.Test;
import org.junit.jupiter.api.io.TempDir;

import java.nio.ByteBuffer;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.nio.file.Path;
import java.util.concurrent.TimeUnit;

import static org.junit.jupiter.api.Assertions.*;

class MainEncodingTest {
    @Test
    void protocolRemainsUtf8WithNonUtf8ProcessDefaults(@TempDir Path temporary) throws Exception {
        verifyProtocol(temporary, true);
    }

    @Test
    void responseRemainsUtf8WithNonUtf8Stdout(@TempDir Path temporary) throws Exception {
        verifyProtocol(temporary, false);
    }

    private void verifyProtocol(Path temporary, boolean changeFileEncoding) throws Exception {
        Path source = temporary.resolve("\u4e2d\u6587").resolve("Input.java");
        Files.createDirectories(source.getParent());
        Files.writeString(source, "package app; /** \u2014 caf\u00e9 \u4e2d\u6587 */ class Caf\u00e9 { public String value() { return \"\u4e2d\u6587\"; } }", StandardCharsets.UTF_8);
        JsonObject request = new JsonObject();
        JsonArray files = new JsonArray(); files.add(source.toString()); request.add("files", files);
        String classpath = System.getProperty("topo.extractor.test.classpath");
        assertNotNull(classpath, "Gradle must supply the real extractor runtime classpath");
        for (String encoding : new String[] {"windows-1252", "US-ASCII"}) {
            Path output = temporary.resolve(encoding + ".json");
            Path errors = temporary.resolve(encoding + ".stderr");
            Process process = new ProcessBuilder(
                Path.of(System.getProperty("java.home"), "bin", "java").toString(),
                "-Dfile.encoding=" + (changeFileEncoding ? encoding : "UTF-8"), "-Dstdout.encoding=" + encoding,
                "-cp", classpath, "topo.extract.Main")
                .redirectOutput(output.toFile()).redirectError(errors.toFile()).start();
            try {
                try (var input = process.getOutputStream()) {
                    input.write(request.toString().getBytes(StandardCharsets.UTF_8));
                }
                assertTrue(process.waitFor(60, TimeUnit.SECONDS), "extractor exceeded timeout: " + encoding);
                assertEquals(0, process.exitValue(), Files.readString(errors));
                String text = StandardCharsets.UTF_8.newDecoder()
                    .decode(ByteBuffer.wrap(Files.readAllBytes(output))).toString();
                JsonObject result = JsonParser.parseString(text).getAsJsonObject();
                assertTrue(result.get("analysisComplete").getAsBoolean(), text);
                assertEquals("app.Caf\u00e9", result.getAsJsonArray("types").get(0)
                    .getAsJsonObject().get("qualifiedName").getAsString());
                assertTrue(text.contains("\u2014 caf\u00e9 \u4e2d\u6587"), "Unicode source comment must survive the subprocess protocol");
            } finally {
                if (process.isAlive()) process.destroyForcibly().waitFor(10, TimeUnit.SECONDS);
            }
        }
    }
}
