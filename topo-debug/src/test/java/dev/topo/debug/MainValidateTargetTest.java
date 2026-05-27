package dev.topo.debug;

import org.junit.jupiter.api.Test;
import org.junit.jupiter.api.io.TempDir;

import java.io.IOException;
import java.nio.file.Files;
import java.nio.file.Path;
import java.util.Collections;
import java.util.List;

import static org.junit.jupiter.api.Assertions.*;

/**
 * Regression tests for {@code Main.validateTarget} — guards the
 * {@code --target} argument before {@code launchTarget} spawns a
 * subprocess.
 *
 * Audit issue: {@code topo-debug-java-spawns-subprocess-trusting-target-arg}.
 */
class MainValidateTargetTest {

    @Test
    void rejectsEmptyString() {
        StringBuilder err = new StringBuilder();
        Path r = Main.validateTarget("", Collections.emptyList(), err);
        assertNull(r);
        assertTrue(err.toString().contains("--target is empty"), err.toString());
    }

    @Test
    void rejectsControlCharacter() {
        StringBuilder err = new StringBuilder();
        Path r = Main.validateTarget("main.Cls\nflag", Collections.emptyList(), err);
        assertNull(r);
        assertTrue(err.toString().contains("control character"), err.toString());
    }

    @Test
    void rejectsLeadingDashAsClassName() {
        StringBuilder err = new StringBuilder();
        Path r = Main.validateTarget("-XshowSettings:properties",
            Collections.emptyList(), err);
        assertNull(r);
        assertTrue(err.toString().contains("cannot start with '-'"), err.toString());
    }

    @Test
    void rejectsInvalidJlsIdentifier() {
        StringBuilder err = new StringBuilder();
        Path r = Main.validateTarget("1Bad.Name", Collections.emptyList(), err);
        assertNull(r);
        assertTrue(err.toString().contains("must start with"), err.toString());
    }

    @Test
    void rejectsConsecutiveDots() {
        StringBuilder err = new StringBuilder();
        Path r = Main.validateTarget("dev..topo.App", Collections.emptyList(), err);
        assertNull(r);
        assertTrue(err.toString().contains("empty segment"), err.toString());
    }

    @Test
    void acceptsValidClassName() {
        StringBuilder err = new StringBuilder();
        Path r = Main.validateTarget("dev.topo.App", Collections.emptyList(), err);
        assertNull(r, "class name returns null path (handled by synthesise-java branch)");
        assertEquals(0, err.length(), "no diagnostic on a valid identifier: " + err);
    }

    @Test
    void acceptsExecutableLauncherWhenNoAllowlist(@TempDir Path tmp) throws IOException {
        Path launcher = tmp.resolve("run_target.sh");
        Files.writeString(launcher, "#!/bin/sh\necho hi\n");
        launcher.toFile().setExecutable(true);

        StringBuilder err = new StringBuilder();
        Path r = Main.validateTarget(launcher.toString(), Collections.emptyList(), err);
        assertNotNull(r, err.toString());
        assertTrue(r.endsWith("run_target.sh"));
    }

    @Test
    void rejectsExecutableLauncherOutsideAllowlist(@TempDir Path tmp) throws IOException {
        Path allowed = tmp.resolve("allowed");
        Files.createDirectories(allowed);
        Path outside = tmp.resolve("outside.sh");
        Files.writeString(outside, "#!/bin/sh\n");
        outside.toFile().setExecutable(true);

        StringBuilder err = new StringBuilder();
        Path r = Main.validateTarget(outside.toString(), List.of(allowed), err);
        assertNull(r);
        assertTrue(err.toString().contains("outside the launcher allowlist"),
            err.toString());
    }

    @Test
    void acceptsExecutableLauncherInsideAllowlist(@TempDir Path tmp) throws IOException {
        Path allowed = tmp.resolve("allowed");
        Files.createDirectories(allowed);
        Path launcher = allowed.resolve("run_target.sh");
        Files.writeString(launcher, "#!/bin/sh\n");
        launcher.toFile().setExecutable(true);

        StringBuilder err = new StringBuilder();
        Path r = Main.validateTarget(launcher.toString(), List.of(allowed), err);
        assertNotNull(r, err.toString());
    }
}
