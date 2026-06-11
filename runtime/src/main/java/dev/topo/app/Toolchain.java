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
 * toolchain; it never reimplements parsing or checking. Canonical
 * resolution order (kept in lockstep with the Python and TypeScript
 * runtimes' locators):
 *
 * <ol>
 *   <li>explicit {@code TOPO_BIN_DIR} env var — <em>strict</em>: when
 *       set, the binary must resolve under it or resolution fails with
 *       the probed paths listed. The override is the CI/test pinning
 *       contract; silently falling through to PATH would swap the
 *       binary under test.
 *   <li>{@code PATH} lookup for bare {@code topo} / {@code topo-check}
 *       executables, {@code PATHEXT}-aware on Windows (the layout
 *       {@code cmake --install}, Homebrew, and the per-package installs
 *       all ship into)
 *   <li>known sibling build trees of this checkout, fixed order,
 *       first hit wins: {@code build/} then {@code build-asan/} (dev
 *       convenience while working inside the Topo source tree)
 * </ol>
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

    // Tier-3 sibling build trees, probed in this order, first hit wins.
    // build-no-llvm/ is excluded by policy (see class javadoc).
    private static final List<String> BUILD_DIRS = List.of("build", "build-asan");

    private Toolchain() {
    }

    public static Path topoBin() {
        return find("topo-core/tools/topo/topo", "topo");
    }

    public static Path topoCheckBin() {
        return find("topo-cli/tools/topo-check/topo-check", "topo-check");
    }

    private static Path find(String rel, String bare) {
        return find(rel, bare,
                System.getenv("TOPO_BIN_DIR"), System.getenv("PATH"), repoRoot());
    }

    /**
     * Resolution core with the environment injected — package-private so
     * unit tests can exercise all three tiers without {@code setenv}
     * (which the JVM does not expose).
     *
     * @param rel       the source-tree relative path used inside the
     *                  monorepo build tree (e.g. {@code topo-core/tools/topo/topo}).
     * @param bare      the bare binary name as installed on {@code PATH}
     *                  (e.g. {@code topo}).
     * @param envBinDir value of {@code TOPO_BIN_DIR}, or {@code null}.
     * @param envPath   value of {@code PATH}, or {@code null}.
     * @param repoRoot  checkout root for the tier-3 build-tree probe.
     */
    static Path find(String rel, String bare,
                     String envBinDir, String envPath, Path repoRoot) {
        // Track every location we tried so the eventual error message is
        // actionable — "I looked in X, Y, Z" beats "could not locate" by
        // a wide margin when the user is debugging an install layout.
        List<String> probed = new ArrayList<>();

        // 1. Explicit override — strict: set-but-unresolvable is a hard
        //    error, never a fall-through to a different binary.
        if (envBinDir != null && !envBinDir.isBlank()) {
            Path base = Paths.get(envBinDir);
            for (Path cand : execCandidates(base, rel, bare)) {
                probed.add(cand.toString());
                if (isExec(cand)) {
                    return cand.toAbsolutePath().normalize();
                }
            }
            StringBuilder msg = new StringBuilder();
            msg.append("TOPO_BIN_DIR is set (").append(envBinDir)
                    .append(") but '").append(rel)
                    .append("' did not resolve under it. Probed:\n");
            for (String p : probed) {
                msg.append("  - ").append(p).append('\n');
            }
            msg.append("Point TOPO_BIN_DIR at a directory containing the "
                    + "binary, or unset it to fall back to PATH / the "
                    + "sibling build tree.");
            throw new IllegalStateException(msg.toString());
        }

        // 2. PATH probe — the installed-package layout (Homebrew /
        //    cmake --install / topo backend install / system package
        //    manager) puts the binaries on PATH by design. This is the
        //    only resolution path that works outside a monorepo checkout.
        Path onPath = findOnPath(bare, envPath);
        if (onPath != null) {
            return onPath.toAbsolutePath().normalize();
        }
        probed.add("PATH lookup for '" + bare + "'");

        // 3. Sibling monorepo build trees — dev convenience while
        //    working inside the Topo source tree.
        for (String buildDir : BUILD_DIRS) {
            for (Path cand : execCandidates(repoRoot.resolve(buildDir), rel, bare)) {
                probed.add(cand.toString());
                if (isExec(cand)) {
                    return cand.toAbsolutePath().normalize();
                }
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
     * {@code build} or {@code build-asan} dir alongside
     * {@code topo-core}). Gradle runs the JVM with a working directory
     * of {@code topo-lang-java/runtime}; the checkout root is a few
     * parents up but the exact depth is not assumed. Returns
     * {@link #REPO_ROOT} as the fallback so the next resolution step
     * can still produce a probe-list entry.
     */
    private static Path repoRoot() {
        Path p = REPO_ROOT;
        for (int i = 0; i < 12 && p != null; i++) {
            if (Files.isDirectory(p.resolve("topo-core"))
                    && (Files.isDirectory(p.resolve("build"))
                        || Files.isDirectory(p.resolve("build-asan")))) {
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
     * Cross-platform {@code which}: walk the given {@code PATH} value
     * for the bare binary name, honouring {@code PATHEXT} on Windows so
     * a request for {@code "topo"} correctly finds {@code topo.exe}.
     * Returns {@code null} if not found.
     */
    private static Path findOnPath(String name, String pathEnv) {
        if (pathEnv == null || pathEnv.isBlank()) {
            return null;
        }
        String[] dirs = pathEnv.split(java.io.File.pathSeparator);

        List<String> suffixes = new ArrayList<>();
        suffixes.add("");
        if (IS_WINDOWS) {
            suffixes.addAll(pathext());
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

    /** {@code PATHEXT} extensions (Windows), with the standard default. */
    private static List<String> pathext() {
        String pathExt = System.getenv("PATHEXT");
        if (pathExt == null || pathExt.isBlank()) {
            pathExt = ".COM;.EXE;.BAT;.CMD";
        }
        List<String> exts = new ArrayList<>();
        for (String ext : pathExt.split(";")) {
            if (!ext.isBlank()) exts.add(ext.trim());
        }
        return exts;
    }

    private static boolean isExec(Path p) {
        // Files.isExecutable is unreliable on Windows for .exe files
        // (it consults POSIX bits, which Windows does not maintain).
        // There, a regular file qualifies only with a PATHEXT-matching
        // suffix — the same rule the Python and TypeScript locators
        // enforce; accepting any regular file would resolve
        // non-executables that no shell could launch.
        if (!Files.isRegularFile(p)) return false;
        if (IS_WINDOWS) {
            String name = p.getFileName().toString().toLowerCase(Locale.ROOT);
            for (String ext : pathext()) {
                if (name.endsWith(ext.toLowerCase(Locale.ROOT))) {
                    return true;
                }
            }
            return false;
        }
        return Files.isExecutable(p);
    }
}
