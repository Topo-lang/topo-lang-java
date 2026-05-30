package topo.extract;

import com.google.gson.JsonObject;
import com.google.gson.JsonParser;
import org.junit.jupiter.api.AfterEach;
import org.junit.jupiter.api.BeforeEach;
import org.junit.jupiter.api.Test;
import org.junit.jupiter.api.io.TempDir;

import java.io.*;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.nio.file.Path;

import static org.junit.jupiter.api.Assertions.*;

/**
 * Regression tests for the {@code runDegraded} JSON envelope signal —
 * guards the sticky-but-previously-untested OOM-degradation flag.
 *
 * Strategy: drive {@link Main#main} with a stdin JSON request, capture
 * stdout, parse the emitted JSON, and assert the {@code runDegraded}
 * field is present and correct in both the happy path and the
 * post-OOM-degradation path. The actual OOM path is simulated through
 * a package-private reset / inspect pair to keep the test fast and
 * deterministic (a real {@code -Xmx16m} subprocess test would be
 * fragile across CI hosts and slow).
 */
class MainDegradationTest {

    private final InputStream originalIn = System.in;
    private final PrintStream originalOut = System.out;

    @BeforeEach
    void resetState() {
        Main.resetDegradationStateForTests();
    }

    @AfterEach
    void restoreStreams() {
        System.setIn(originalIn);
        System.setOut(originalOut);
        Main.resetDegradationStateForTests();
    }

    private JsonObject runExtractWithStdin(String request) throws Exception {
        var stdin = new ByteArrayInputStream(request.getBytes(StandardCharsets.UTF_8));
        var stdoutBuf = new ByteArrayOutputStream();
        System.setIn(stdin);
        System.setOut(new PrintStream(stdoutBuf, true, StandardCharsets.UTF_8));
        Main.main(new String[0]);
        return JsonParser.parseString(stdoutBuf.toString(StandardCharsets.UTF_8))
                          .getAsJsonObject();
    }

    private Path writeJava(Path dir, String fileName, String source) throws IOException {
        Path p = dir.resolve(fileName);
        Files.writeString(p, source);
        return p;
    }

    @Test
    void happyPathRunDegradedIsFalse(@TempDir Path tmp) throws Exception {
        Path java = writeJava(tmp, "Hello.java",
            "public class Hello { public static void main(String[] a) {} }");
        String request = "{\"files\":[\"" + java.toString().replace("\\", "\\\\") + "\"]}";

        JsonObject module = runExtractWithStdin(request);

        assertTrue(module.has("runDegraded"),
            "module envelope must always carry runDegraded so the C++ consumer "
          + "can rely on the signal being present");
        assertFalse(module.get("runDegraded").getAsBoolean(),
            "fresh run on a tiny file should not flip the degradation flag");
        assertFalse(module.has("degradationReason"),
            "happy path must not emit degradationReason: " + module);
    }

    @Test
    void degradedRunReportsRunDegradedAndReason(@TempDir Path tmp) throws Exception {
        // Simulate the OOM-degraded path by flipping the sticky flag
        // before the run. The static flag is the same one the catch
        // block in parseSource flips, so the JSON-envelope downstream
        // behaviour is identical to a real OOM-triggered run.
        Path java = writeJava(tmp, "Hello.java",
            "public class Hello { public static void main(String[] a) {} }");
        String request = "{\"files\":[\"" + java.toString().replace("\\", "\\\\") + "\"]}";

        // Drive the sticky state into "degraded" before main runs.
        Main.resetDegradationStateForTests();
        java.lang.reflect.Field field = Main.class.getDeclaredField("bindingResolutionDegraded");
        field.setAccessible(true);
        field.setBoolean(null, true);

        JsonObject module = runExtractWithStdin(request);

        assertTrue(module.has("runDegraded"));
        assertTrue(module.get("runDegraded").getAsBoolean(),
            "post-degradation run must report runDegraded=true so the C++ "
          + "consumer downgrades the suite-level fidelity");
        assertTrue(module.has("degradationReason"),
            "degraded run must carry a human-readable reason: " + module);
        String reason = module.get("degradationReason").getAsString();
        assertTrue(reason.contains("OutOfMemoryError") &&
                   reason.contains("binding-disabled"),
            "reason must explain the cause and the fallback shape: " + reason);
    }
}
