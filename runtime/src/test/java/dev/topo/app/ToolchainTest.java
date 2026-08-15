package dev.topo.app;

import static org.junit.jupiter.api.Assertions.assertNotNull;
import static org.junit.jupiter.api.Assertions.assertThrows;
import static org.junit.jupiter.api.Assertions.assertTrue;

import java.io.IOException;
import java.nio.file.Files;
import java.nio.file.Path;
import java.util.Locale;
import org.junit.jupiter.api.Test;
import org.junit.jupiter.api.io.TempDir;

/**
 * Unit tests for {@link Toolchain}'s canonical resolution order
 * (strict {@code TOPO_BIN_DIR} &gt; {@code PATH} &gt; sibling build
 * trees, {@code build-no-llvm/} excluded), driven through the
 * package-private injectable seam — the JVM exposes no portable
 * {@code setenv}, so the environment is passed in rather than mutated.
 */
class ToolchainTest {

    private static final boolean IS_WINDOWS =
            System.getProperty("os.name", "").toLowerCase(Locale.ROOT).contains("win");

    private static final String REL = "topo-core/tools/topo/topo-fake";
    private static final String BARE = "topo-fake";

    /**
     * Stage a file {@code isExec} accepts on this platform: exec-bit
     * script on POSIX, {@code .exe}-suffixed regular file on Windows
     * (the resolver requires a PATHEXT-matching suffix there).
     */
    private static Path stageExecutable(Path dir, String name) throws IOException {
        Files.createDirectories(dir);
        Path file = dir.resolve(IS_WINDOWS ? name + ".exe" : name);
        Files.writeString(file, "#!/bin/sh\nexit 0\n");
        if (!IS_WINDOWS) {
            assertTrue(file.toFile().setExecutable(true), "chmod +x failed");
        }
        return file;
    }

    private static void assertResolvesTo(Path expected, Path actual) throws IOException {
        assertTrue(Files.isSameFile(expected, actual),
                "expected " + expected + ", resolved " + actual);
    }

    private static Path noCheckout(Path tmp) {
        return tmp.resolve("no-such-checkout");
    }

    // ── Tier 1: TOPO_BIN_DIR (strict) ─────────────────────────────

    @Test
    void envBinDirFlatLayoutResolves(@TempDir Path tmp) throws IOException {
        Path staged = stageExecutable(tmp, BARE);
        Path got = Toolchain.find(REL, BARE, tmp.toString(), null, noCheckout(tmp));
        assertResolvesTo(staged, got);
    }

    @Test
    void envBinDirNestedLayoutResolves(@TempDir Path tmp) throws IOException {
        Path staged = stageExecutable(tmp.resolve("topo-core/tools/topo"), BARE);
        Path got = Toolchain.find(REL, BARE, tmp.toString(), null, noCheckout(tmp));
        assertResolvesTo(staged, got);
    }

    @Test
    void envBinDirMultiConfigLayoutResolves(@TempDir Path tmp) throws IOException {
        Path staged = stageExecutable(tmp.resolve("Release"), BARE);
        Path got = Toolchain.find(REL, BARE, tmp.toString(), null, noCheckout(tmp));
        assertResolvesTo(staged, got);
    }

    @Test
    void envBinDirIsStrictEvenWhenPathCouldResolve(@TempDir Path tmp) throws IOException {
        // The override is the CI/test pinning contract: set-but-
        // unresolvable must fail hard, never silently swap in a PATH
        // binary.
        Path emptyBinDir = Files.createDirectories(tmp.resolve("empty"));
        Path pathDir = stageExecutable(tmp.resolve("onpath"), BARE).getParent();

        IllegalStateException ex = assertThrows(IllegalStateException.class, () ->
                Toolchain.find(REL, BARE, emptyBinDir.toString(),
                        pathDir.toString(), noCheckout(tmp)));
        assertTrue(ex.getMessage().contains("TOPO_BIN_DIR"),
                "strict override error must name TOPO_BIN_DIR: " + ex.getMessage());
        assertTrue(ex.getMessage().contains("Probed:"),
                "error must list probed locations: " + ex.getMessage());
    }

    // ── Tier 2: PATH ──────────────────────────────────────────────

    @Test
    void pathTierResolvesBareName(@TempDir Path tmp) throws IOException {
        Path emptyDir = Files.createDirectories(tmp.resolve("empty"));
        Path staged = stageExecutable(tmp.resolve("onpath"), BARE);
        String envPath = emptyDir + java.io.File.pathSeparator + staged.getParent();

        Path got = Toolchain.find(REL, BARE, null, envPath, noCheckout(tmp));
        assertResolvesTo(staged, got);
    }

    // ── Tier 3: sibling build trees ───────────────────────────────

    @Test
    void buildTreeResolves(@TempDir Path tmp) throws IOException {
        Path staged = stageExecutable(tmp.resolve("build"), BARE);
        Path got = Toolchain.find(REL, BARE, null, null, tmp);
        assertResolvesTo(staged, got);
    }

    @Test
    void buildAsanResolvesWhenBuildAbsent(@TempDir Path tmp) throws IOException {
        Path staged = stageExecutable(tmp.resolve("build-asan"), BARE);
        Path got = Toolchain.find(REL, BARE, null, null, tmp);
        assertResolvesTo(staged, got);
    }

    @Test
    void buildPreferredOverBuildAsan(@TempDir Path tmp) throws IOException {
        Path inBuild = stageExecutable(tmp.resolve("build"), BARE);
        stageExecutable(tmp.resolve("build-asan"), BARE);
        Path got = Toolchain.find(REL, BARE, null, null, tmp);
        assertResolvesTo(inBuild, got);
    }

    @Test
    void buildNoLlvmIsExcluded(@TempDir Path tmp) throws IOException {
        // The distrusted tree must never satisfy resolution even when it
        // is the only one present (see Toolchain javadoc).
        stageExecutable(tmp.resolve("build-no-llvm"), BARE);
        assertThrows(IllegalStateException.class, () ->
                Toolchain.find(REL, BARE, null, null, tmp));
    }

    // ── Failure shape ─────────────────────────────────────────────

    @Test
    void findRaisesActionableErrorWhenNothingResolves(@TempDir Path tmp) {
        // The error message must list every probed location so the user
        // knows what to fix.
        IllegalStateException ex = assertThrows(IllegalStateException.class, () ->
                Toolchain.find(REL, BARE, null, null, noCheckout(tmp)));
        String msg = ex.getMessage();
        assertNotNull(msg);
        assertTrue(msg.contains("PATH lookup"),
                "error message must mention PATH probe: " + msg);
        assertTrue(msg.contains("Probed:"),
                "error message must list probed locations: " + msg);
        // Open-source release contract: the error must guide the user
        // toward Homebrew / system package / topo backend install
        // rather than only the cmake build step.
        assertTrue(msg.contains("Homebrew") || msg.contains("backend install"),
                "error must mention install paths besides cmake: " + msg);
    }
}
