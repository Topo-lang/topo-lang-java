package dev.topo.app;

import java.nio.file.Files;
import java.nio.file.Path;
import java.nio.file.Paths;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.List;
import java.util.Locale;

/**
 * Locate the built Topo toolchain binaries.
 *
 * <p>topo-app is a product layer that <em>consumes</em> the existing
 * toolchain; it never reimplements parsing or checking. Resolution order:
 *
 * <ol>
 *   <li>explicit {@code TOPO_BIN_DIR} env var (used by tests/CI and by
 *       users with a non-standard install location)
 *   <li>{@code PATH} lookup for bare {@code topo} / {@code topo-check}
 *       executables (the layout {@code cmake --install}, Homebrew, and
 *       the post-monorepo-split per-package installs all ship into)
 *   <li>a sibling monorepo {@code build/} tree of this checkout (dev
 *       convenience while working inside the Topo source tree)
 * </ol>
 *
 * <p>The PATH probe was added to make this resolver work in the
 * open-source release: the monorepo-tree walk only succeeds inside a
 * working Topo source checkout, so a user who installs the published
 * package via Homebrew / a system package manager / {@code topo backend
 * install} would otherwise hit a misleading
 * {@link IllegalStateException} pointing them at {@code cmake --build}.
 *
 * <p>The stale {@code build-no-llvm/} tree is deliberately <em>not</em>
 * a fallback: it predates the handler/flow grammar and would reject
 * valid emitted {@code .topo} with a spurious parse failure (a tracked
 * environmental issue). Silently degrading a correctness tool would
 * defeat the point, so a missing binary is a hard error with a list of
 * probed locations.
 */
public final class Toolchain {

    // This file lives at
    // topo-lang-java/runtime/src/main/java/dev/topo/app/Toolchain.java;
    // the repository root is seven parents up.
    private static final Path REPO_ROOT =
            Paths.get(System.getProperty("user.dir"))
                    .toAbsolutePath()
                    .normalize();

    private static final boolean IS_WINDOWS =
            System.getProperty("os.name", "").toLowerCase(Locale.ROOT).contains("win");

    private Toolchain() {
    }

    public static Path topoBin() {
        return find("topo-core/tools/topo/topo", "topo");
    }

    public static Path topoCheckBin() {
        return find("topo-cli/tools/topo-check/topo-check", "topo-check");
    }

    /**
     * @param rel  the source-tree relative path used inside the
     *             monorepo build tree (e.g. {@code topo-core/tools/topo/topo}).
     * @param bare the bare binary name as installed on {@code PATH}
     *             (e.g. {@code topo}).
     */
    private static Path find(String rel, String bare) {
        // Track every location we tried so the eventual error message is
        // actionable — "I looked in X, Y, Z" beats "could not locate" by
        // a wide margin when the user is debugging an install layout.
        List<String> probed = new ArrayList<>();

        // 1. Explicit override always wins outright.
        String env = System.getenv("TOPO_BIN_DIR");
        if (env != null && !env.isBlank()) {
            Path base = Paths.get(env);
            for (Path cand : execCandidates(base, rel, bare)) {
                probed.add(cand.toString());
                if (isExec(cand)) {
                    return cand.toAbsolutePath().normalize();
                }
            }
        }

        // 2. PATH probe — the installed-package layout (Homebrew /
        //    cmake --install / topo backend install / system package
        //    manager) puts the binaries on PATH by design. This is the
        //    only resolution path that works outside a monorepo checkout.
        Path onPath = findOnPath(bare);
        if (onPath != null) {
            return onPath.toAbsolutePath().normalize();
        }
        probed.add("PATH lookup for '" + bare + "'");

        // 3. Sibling monorepo build tree — dev convenience while
        //    working inside the Topo source tree.
        Path repoRoot = repoRoot();
        for (Path cand : execCandidates(repoRoot.resolve("build"), rel, bare)) {
            probed.add(cand.toString());
            if (isExec(cand)) {
                return cand.toAbsolutePath().normalize();
            }
        }

        StringBuilder msg = new StringBuilder();
        msg.append("could not locate '").append(rel).append("'. Probed:\n");
        for (String p : probed) {
            msg.append("  - ").append(p).append('\n');
        }
        msg.append("Install the Topo toolchain (Homebrew / system package "
                + "manager / `topo backend install` / `cmake --install`), "
                + "set TOPO_BIN_DIR, or build from source "
                + "(cmake --build build --target topo topo-check).");
        throw new IllegalStateException(msg.toString());
    }

    /**
     * Walk up until a directory looks like the repo root (has a
     * {@code build} dir alongside {@code topo-core}). Gradle runs the
     * JVM with a working directory of {@code topo-lang-java/runtime};
     * the checkout root is a few parents up but the exact depth is not
     * assumed. Returns {@link #REPO_ROOT} as the fallback so the next
     * resolution step can still produce a probe-list entry.
     */
    private static Path repoRoot() {
        Path p = REPO_ROOT;
        for (int i = 0; i < 12 && p != null; i++) {
            if (Files.isDirectory(p.resolve("topo-core"))
                    && Files.isDirectory(p.resolve("build"))) {
                return p;
            }
            p = p.getParent();
        }
        return REPO_ROOT;
    }

    /**
     * Candidate executable paths under {@code base} for the given
     * {@code rel} (nested layout) and {@code bare} (flat bin layout).
     * Probes {@code .exe} suffixes on Windows so a CMake build under
     * {@code build/Release/topo.exe} is discoverable.
     */
    private static List<Path> execCandidates(Path base, String rel, String bare) {
        List<Path> out = new ArrayList<>();
        List<String> suffixes = IS_WINDOWS
                ? Arrays.asList("", ".exe")
                : List.of("");
        List<String> configs = List.of("", "Release", "RelWithDebInfo", "Debug");

        for (String sfx : suffixes) {
            for (String cfg : configs) {
                Path root = cfg.isEmpty() ? base : base.resolve(cfg);
                out.add(root.resolve(rel + sfx));
                out.add(root.resolve(bare + sfx));
            }
        }
        return out;
    }

    /**
     * Cross-platform {@code which}: walk {@code PATH} for the bare
     * binary name, honouring {@code PATHEXT} on Windows so a request
     * for {@code "topo"} correctly finds {@code topo.exe}. Returns
     * {@code null} if not found.
     */
    private static Path findOnPath(String name) {
        String pathEnv = System.getenv("PATH");
        if (pathEnv == null || pathEnv.isBlank()) {
            return null;
        }
        String[] dirs = pathEnv.split(java.io.File.pathSeparator);

        List<String> suffixes = new ArrayList<>();
        suffixes.add("");
        if (IS_WINDOWS) {
            String pathExt = System.getenv("PATHEXT");
            if (pathExt == null || pathExt.isBlank()) {
                pathExt = ".COM;.EXE;.BAT;.CMD";
            }
            for (String ext : pathExt.split(";")) {
                if (!ext.isBlank()) suffixes.add(ext.trim());
            }
        }

        for (String dir : dirs) {
            if (dir.isBlank()) continue;
            Path base = Paths.get(dir);
            for (String sfx : suffixes) {
                Path cand = base.resolve(name + sfx);
                if (isExec(cand)) {
                    return cand;
                }
            }
        }
        return null;
    }

    private static boolean isExec(Path p) {
        // Files.isExecutable is unreliable on Windows for .exe files
        // (it consults POSIX bits, which Windows does not maintain); a
        // regular file with a PATHEXT-matching suffix is the right test
        // there. We let isRegularFile do the existence check; the
        // suffix filtering above (PATHEXT) gates which paths we even
        // get here on Windows.
        if (!Files.isRegularFile(p)) return false;
        if (IS_WINDOWS) return true;
        return Files.isExecutable(p);
    }
}
