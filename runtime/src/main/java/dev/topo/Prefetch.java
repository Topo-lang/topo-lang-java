package dev.topo;

import java.lang.invoke.MethodHandles;
import java.lang.invoke.VarHandle;

/**
 * Hardware prefetch hints implemented with permanent JDK APIs only
 * ({@link java.lang.invoke.VarHandle}) — no {@code sun.misc.Unsafe}.
 *
 * <p>The JVM exposes no public software-prefetch intrinsic, so this class
 * uses a "touch-to-cache" pattern: reading a memory location forces the
 * cache line into L1/L2. Reads are routed through {@code VarHandle}
 * array-element accessors and consumed by a black-hole sink so C2 cannot
 * dead-code-eliminate the touch.</p>
 *
 * <p>Migration note: the original implementation resolved
 * {@code sun.misc.Unsafe} reflectively and called its memory-access API
 * ({@code getInt}/{@code getObject}/{@code getFloat}/{@code arrayBaseOffset}/
 * {@code arrayIndexScale}). Those methods are terminally deprecated
 * (marked for removal). They are replaced here by {@code VarHandle}
 * array-element access (permanent API). The arbitrary-byte-offset variant
 * {@link #prefetchRead(Object, long)} can no longer address into a raw
 * object layout without Unsafe; it now touches the target object's header
 * line (class word) instead, which preserves the "bring target resident"
 * intent for the realistic callers (array/object bases). See javadoc on
 * that method.</p>
 */
public final class Prefetch {
    private static final boolean AVAILABLE;

    /** VarHandle over {@code Object[]} elements (plain read). */
    private static final VarHandle OBJ_ARRAY;
    /** VarHandle over {@code float[]} elements (plain read). */
    private static final VarHandle FLOAT_ARRAY;

    /**
     * Black-hole sink. Volatile so the JIT must materialise every store and
     * therefore cannot eliminate the preceding load (the actual cache touch).
     */
    @SuppressWarnings("unused")
    private static volatile int blackHole;

    /** Set once when the prefetch capability is found absent. */
    private static volatile boolean degradationLogged;

    static {
        VarHandle objArr = null;
        VarHandle floatArr = null;
        boolean ok = false;
        try {
            objArr = MethodHandles.arrayElementVarHandle(Object[].class);
            floatArr = MethodHandles.arrayElementVarHandle(float[].class);
            ok = true;
        } catch (RuntimeException | Error e) {
            // Capability unavailable — methods become no-ops. This is not
            // expected on any supported JDK (VarHandle is permanent since
            // Java 9); the visible one-time log fires on first use.
            logDegradationOnce(e);
        }
        OBJ_ARRAY = objArr;
        FLOAT_ARRAY = floatArr;
        AVAILABLE = ok;
    }

    private Prefetch() {}

    /** @return true if hardware prefetch (cache-touch) hinting is available */
    public static boolean isAvailable() { return AVAILABLE; }

    /**
     * Emit, exactly once, a visible signal that the prefetch capability is
     * absent and all hint methods are degrading to no-ops.
     *
     * <p>Per the project's no-silent-degradation principle: a fallback path
     * must announce itself at least once on a visible channel rather than
     * silently doing nothing. This is a best-effort performance hint, so the
     * level is a warning (correctness is unaffected; only the optional speed
     * hint is lost). Subsequent calls are suppressed to avoid log floods on
     * the hot prefetch path.</p>
     */
    private static void logDegradationOnce(Throwable cause) {
        if (degradationLogged) return;
        synchronized (Prefetch.class) {
            if (degradationLogged) return;
            degradationLogged = true;
        }
        System.getLogger(Prefetch.class.getName()).log(
                System.Logger.Level.WARNING,
                "dev.topo.Prefetch: hardware prefetch hint capability "
                        + "unavailable; all prefetch* methods degrade to "
                        + "no-ops for the remainder of this JVM. Performance "
                        + "hint only — correctness is unaffected.",
                cause);
    }

    private static void degradeNoOp() {
        if (!degradationLogged) {
            logDegradationOnce(null);
        }
    }

    /**
     * No-op witness for {@code access(streaming)} declarations.
     *
     * <p>PrefetchPass is classified {@code ENHANCE} on JVM (declaration-class,
     * no speedup path — HotSpot has no public software-prefetch intrinsic).
     * Forced-mode injection must still leave an observable bytecode diff to
     * satisfy the absolute rule "forced without bytecode diff is an ERROR".
     * This method is the minimal-overhead witness — an empty static call that
     * C2 inlines to nothing at runtime while the {@code INVOKESTATIC}
     * instruction remains in the classfile as the declaration marker.</p>
     */
    public static void accessStreamingWitness() {
        // Intentionally empty — see javadoc.
    }

    /**
     * Touch an object so its header cache line (64B, covering the class word
     * and first fields) is pulled into L1/L2.
     *
     * <p><b>Semantic change vs. the {@code sun.misc.Unsafe} implementation:</b>
     * the original read a raw byte {@code offset} into the object's memory
     * layout, which is only expressible via {@code Unsafe} (removal-marked).
     * Without it there is no safe way to address an arbitrary offset; this
     * implementation touches the object's header line (via its class word)
     * regardless of {@code offset}. For the realistic callers — array/object
     * bases — this preserves the "bring target resident" intent. The
     * {@code offset} parameter is retained for source/binary compatibility
     * but no longer selects a sub-object location.</p>
     *
     * @param target Object to prefetch (its header line is touched)
     * @param offset Retained for compatibility; no longer interpreted
     */
    public static void prefetchRead(Object target, long offset) {
        if (!AVAILABLE) { degradeNoOp(); return; }
        if (target == null) return;
        // Reading the class word forces the object's header cache line
        // resident; the volatile store stops the JIT folding it away.
        blackHole = target.getClass().hashCode();
    }

    /**
     * Prefetch ahead in an {@code Object[]} array from {@code fromIndex} for
     * {@code count} elements. Each element reference slot is touched to bring
     * it into L1/L2 cache.
     *
     * @param array     The array to prefetch from
     * @param fromIndex Starting index for prefetch
     * @param count     Number of elements to prefetch
     */
    public static void prefetchArray(Object[] array, int fromIndex, int count) {
        if (!AVAILABLE) { degradeNoOp(); return; }
        int end = Math.min(fromIndex + count, array.length);
        int acc = 0;
        for (int i = Math.max(fromIndex, 0); i < end; i++) {
            Object ref = OBJ_ARRAY.get(array, i);
            acc += (ref == null) ? 0 : 1;
        }
        blackHole = acc;
    }

    /**
     * Prefetch objects referenced by an {@code Object[]} array by following the
     * pointer chain: reads each reference, then touches the referenced object's
     * header cache line (64B, covering header + first fields).
     *
     * <p>Unlike {@link #prefetchArray}, which touches the reference slots
     * themselves (already covered by hardware sequential prefetch), this method
     * chases the indirection to bring the <em>actual heap objects</em> into
     * cache — the real bottleneck when objects are scattered across the heap.
     * The header line is reached safely by reading the object's class word
     * instead of an {@code Unsafe} raw-offset load.</p>
     *
     * @param array     The array whose referenced objects should be prefetched
     * @param fromIndex Starting index for prefetch
     * @param count     Number of elements to prefetch
     */
    public static void prefetchObjects(Object[] array, int fromIndex, int count) {
        if (!AVAILABLE) { degradeNoOp(); return; }
        int end = Math.min(fromIndex + count, array.length);
        int acc = 0;
        for (int i = Math.max(fromIndex, 0); i < end; i++) {
            Object ref = OBJ_ARRAY.get(array, i);
            if (ref != null) {
                // Dereference into the object's header line (class word).
                acc += ref.getClass().hashCode();
            }
        }
        blackHole = acc;
    }

    /**
     * Prefetch ahead in a {@code float[]} array.
     *
     * @param array     The array to prefetch from
     * @param fromIndex Starting index for prefetch
     * @param count     Number of elements to prefetch
     */
    public static void prefetchFloatArray(float[] array, int fromIndex, int count) {
        if (!AVAILABLE) { degradeNoOp(); return; }
        int end = Math.min(fromIndex + count, array.length);
        float acc = 0f;
        for (int i = Math.max(fromIndex, 0); i < end; i++) {
            acc += (float) FLOAT_ARRAY.get(array, i);
        }
        blackHole = Float.floatToRawIntBits(acc);
    }
}
