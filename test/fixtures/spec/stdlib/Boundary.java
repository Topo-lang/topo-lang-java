// Matching Java host source for minimal.topo.
//
// Each method signature corresponds 1:1 to the .topo declaration via the
// JavaEmitter stdlib mapping. This file is reference material for the spec
// fixture; topo-check L1 will treat these as the host implementations when
// the surrounding harness runs.
//
// Type-mapping notes:
//   - `optional<T>` uses boxed wrapper types (Long, Boolean) because Java
//     generics cannot take primitive type arguments. `null` denotes absent.
//   - `slice<T>` lowers to `List<Boxed<T>>` for Batch 1; future revisions
//     may specialize numeric T to MemorySegment (Panama FFM, Java 22+).
//   - `string` is Java's UTF-16 `String`; transcoding to/from the Topo
//     UTF-8 boundary is provided by `dev.topo.StringBoundary` (not invoked
//     here — these signatures only verify the type shape).

package stdlib_smoke;

import java.util.List;

public class Boundary {

    public static boolean isReady() {
        return true;
    }

    public static long nextId() {
        return 42L;
    }

    public static double averageScore() {
        return 0.75;
    }

    public static String label() {
        return "topo";
    }

    public static Boolean maybeFlag() {
        return null;
    }

    public static List<Double> samples() {
        return List.of(1.0, 2.0, 3.0);
    }

    public static Long boundary(long id,
                                String name,
                                Boolean flags,
                                List<Double> values) {
        if (flags == null || !flags) return null;
        if (name.isEmpty() || values.isEmpty()) return null;
        return id;
    }
}
