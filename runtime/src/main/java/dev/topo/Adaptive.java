package dev.topo;

import java.lang.invoke.*;
import java.util.concurrent.ConcurrentHashMap;

/**
 * Adaptive dispatch using MethodHandle and SwitchPoint.
 * Allows runtime hot-swapping of method implementations based on profiling.
 */
public final class Adaptive {
    private static final ConcurrentHashMap<String, DispatchEntry> dispatchers = new ConcurrentHashMap<>();

    private Adaptive() {}

    private static class DispatchEntry {
        volatile SwitchPoint switchPoint;
        volatile MethodHandle currentTarget;
        volatile MethodHandle fallback;

        DispatchEntry(SwitchPoint sp, MethodHandle target, MethodHandle fallback) {
            this.switchPoint = sp;
            this.currentTarget = target;
            this.fallback = fallback;
        }
    }

    /**
     * Register an adaptive dispatch point.
     * @param name Qualified method name
     * @param target Current implementation
     * @param fallback Fallback implementation
     * @return A MethodHandle that dispatches adaptively
     */
    public static MethodHandle register(String name, MethodHandle target, MethodHandle fallback) {
        var sp = new SwitchPoint();
        var entry = new DispatchEntry(sp, target, fallback);
        dispatchers.put(name, entry);
        return sp.guardWithTest(target, fallback);
    }

    /**
     * Invalidate the current dispatch for a method, causing fallback to be used
     * until a new target is registered.
     */
    public static void invalidate(String name) {
        var entry = dispatchers.get(name);
        if (entry != null && entry.switchPoint != null) {
            SwitchPoint.invalidateAll(new SwitchPoint[]{ entry.switchPoint });
            // Create new switch point for future use
            entry.switchPoint = new SwitchPoint();
        }
    }

    /**
     * Check dispatch (called by instrumented code to trigger adaptive behavior).
     * This is a lightweight check that the dispatch system uses as a hook point.
     */
    public static void checkDispatch(String name) {
        // Intentionally lightweight — the real dispatch happens via MethodHandle
        // This method serves as an instrumentation hook for the AdaptivePass
    }

    /**
     * Create an adaptive dispatch for a Supplier-style function.
     * Returns a MethodHandle that can be switched at runtime.
     */
    public static <T> MethodHandle adaptiveSupplier(String name,
                                                      java.util.function.Supplier<T> primary,
                                                      java.util.function.Supplier<T> fallback) {
        try {
            var lookup = MethodHandles.lookup();
            var primaryMH = lookup.findVirtual(java.util.function.Supplier.class, "get",
                MethodType.methodType(Object.class)).bindTo(primary);
            var fallbackMH = lookup.findVirtual(java.util.function.Supplier.class, "get",
                MethodType.methodType(Object.class)).bindTo(fallback);
            return register(name, primaryMH, fallbackMH);
        } catch (NoSuchMethodException | IllegalAccessException e) {
            throw new RuntimeException("Failed to create adaptive dispatch for " + name, e);
        }
    }
}
