package dev.topo;

import org.junit.jupiter.api.Test;

import static org.junit.jupiter.api.Assertions.assertDoesNotThrow;
import static org.junit.jupiter.api.Assertions.assertTrue;

/**
 * Verifies the Unsafe-free {@link Prefetch} migration: the cache-touch
 * hint methods stay best-effort no-throw, the capability is present on a
 * supported JDK, and out-of-range / null inputs are tolerated.
 */
class PrefetchTest {

    @Test
    void capabilityAvailableOnSupportedJdk() {
        // VarHandle is permanent since Java 9; runtime is pinned to JDK 25.
        assertTrue(Prefetch.isAvailable(),
                "VarHandle-based prefetch capability should resolve on JDK 25");
    }

    @Test
    void witnessIsNoThrowNoOp() {
        assertDoesNotThrow(Prefetch::accessStreamingWitness);
    }

    @Test
    void prefetchObjectArrayTouchesElementsWithoutThrowing() {
        Object[] arr = new Object[64];
        for (int i = 0; i < arr.length; i++) arr[i] = "elem-" + i;
        assertDoesNotThrow(() -> Prefetch.prefetchArray(arr, 0, arr.length));
        assertDoesNotThrow(() -> Prefetch.prefetchObjects(arr, 8, 16));
        // Over-count is clamped, not an exception.
        assertDoesNotThrow(() -> Prefetch.prefetchArray(arr, 60, 999));
        assertDoesNotThrow(() -> Prefetch.prefetchObjects(arr, 0, 999));
    }

    @Test
    void prefetchFloatArrayTouchesElementsWithoutThrowing() {
        float[] arr = new float[128];
        for (int i = 0; i < arr.length; i++) arr[i] = i * 1.5f;
        assertDoesNotThrow(() -> Prefetch.prefetchFloatArray(arr, 0, arr.length));
        assertDoesNotThrow(() -> Prefetch.prefetchFloatArray(arr, 100, 999));
    }

    @Test
    void prefetchReadTouchesHeaderWithoutThrowing() {
        Object o = new int[]{1, 2, 3};
        assertDoesNotThrow(() -> Prefetch.prefetchRead(o, 0L));
        // offset is retained-but-ignored; large/garbage offset must not crash.
        assertDoesNotThrow(() -> Prefetch.prefetchRead(o, Long.MAX_VALUE));
        // null target tolerated.
        assertDoesNotThrow(() -> Prefetch.prefetchRead(null, 0L));
    }
}
