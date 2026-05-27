package dev.topo.app;

import java.io.IOException;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.nio.file.Path;
import java.util.List;

/**
 * Zero-declaration check: hand the existing {@code topo-check} the emitted
 * {@code .topo}.
 *
 * <p>The user writes no {@code .topo} by hand. A throwaway project (Topo.toml
 * + emitted {@code .topo} + the host Java sources) is materialised, the
 * existing fresh {@code topo-check} binary is run against it, and its verdict
 * is surfaced. No checking logic is reimplemented — pure orchestration.
 *
 * <p>The holder class is hidden from CompletenessCheck via an {@code
 * ignore_patterns} glob: a Java handler must physically live in a class, but
 * the handler/flow contract declares only the handler symbols, so the
 * enclosing class is a host-language artefact, not an undeclared logic unit.
 */
public final class Check {

    /** The verdict of a zero-declaration {@code topo-check} run. */
    public record Result(boolean passed, int returnCode,
                         String stdout, String stderr) {
    }

    private Check() {
    }

    private static final String TOPO_TOML = """
            [project]
            name = "%s"

            [topo]
            root = "topo/app.topo"

            [build]
            language = "java"
            sources = ["src/**/*.java"]

            [purity]
            mode = "force"

            [completeness]
            ignore_constructors = true
            ignore_main = true
            ignore_patterns = ["%s"]
            """;

    /**
     * Run {@code topo-check} on the framework-emitted {@code .topo} against
     * the given Java source files. {@code holderGlob} is a CompletenessCheck
     * ignore glob for the class(es) that merely host the handler methods
     * (e.g. {@code "*::App"}); the {@code .topo} never names them.
     */
    public static Result run(App app, List<Path> javaSources,
                             String holderGlob) {
        String name = app.graph().namespace().isBlank()
                ? "topo_app" : app.graph().namespace();
        Path root = null;
        try {
            root = Files.createTempDirectory("topo-app-check");
            Path topoDir = root.resolve("topo");
            Path srcDir = root.resolve("src").resolve("main")
                    .resolve("java").resolve("app");
            Files.createDirectories(topoDir);
            Files.createDirectories(srcDir);
            Files.writeString(
                    root.resolve("Topo.toml"),
                    TOPO_TOML.formatted(name, holderGlob),
                    StandardCharsets.UTF_8);
            Files.writeString(
                    topoDir.resolve("app.topo"),
                    Emitter.emit(app.graph()),
                    StandardCharsets.UTF_8);
            for (Path s : javaSources) {
                Files.copy(s, srcDir.resolve(s.getFileName()));
            }

            Process proc = new ProcessBuilder(
                    Toolchain.topoCheckBin().toString(),
                    "--project", root.toString())
                    .start();
            String stdout = new String(
                    proc.getInputStream().readAllBytes(),
                    StandardCharsets.UTF_8);
            String stderr = new String(
                    proc.getErrorStream().readAllBytes(),
                    StandardCharsets.UTF_8);
            int code = proc.waitFor();
            // topo-check exits 0 on PASS; the textual verdict is the source
            // of truth (logical FAIL is not always a non-zero exit).
            boolean passed = stdout.contains("Result: PASS");
            return new Result(passed, code, stdout, stderr);
        } catch (IOException | InterruptedException e) {
            if (e instanceof InterruptedException) {
                Thread.currentThread().interrupt();
            }
            throw new IllegalStateException("zero-decl check failed", e);
        } finally {
            if (root != null) {
                deleteTree(root);
            }
        }
    }

    private static void deleteTree(Path root) {
        try (var walk = Files.walk(root)) {
            walk.sorted(java.util.Comparator.reverseOrder())
                    .forEach(p -> {
                        try {
                            Files.deleteIfExists(p);
                        } catch (IOException ignored) {
                            // best-effort temp cleanup
                        }
                    });
        } catch (IOException ignored) {
            // best-effort temp cleanup
        }
    }
}
