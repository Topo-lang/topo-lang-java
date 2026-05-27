package dev.topo;

import java.util.concurrent.ConcurrentHashMap;

/**
 * Counts call-site hits for adaptive dispatch points and triggers
 * {@link Adaptive#invalidate(String)} when a name's cumulative hit count
 * crosses its threshold. AdaptivePass emits bytecode that increments a
 * per-method counter and calls {@link #tick(String, long, long)} every
 * {@code tickInterval} calls. Once the threshold is reached the entry
 * transitions from WARMUP to ACTIVE and the registered SwitchPoint is
 * invalidated — subsequent dispatches through the originally-returned
 * handle flip to the fallback.
 */
public final class AdaptiveMonitor {
    private static final ConcurrentHashMap<String, State> STATES = new ConcurrentHashMap<>();

    public enum Phase { WARMUP, ACTIVE }

    public static final class State {
        public volatile Phase phase = Phase.WARMUP;
        public volatile long hits;
    }

    private AdaptiveMonitor() {}

    public static void tick(String name, long batch, long threshold) {
        State s = STATES.computeIfAbsent(name, k -> new State());
        long newHits = (s.hits += batch);
        if (s.phase == Phase.WARMUP && newHits >= threshold) {
            synchronized (s) {
                if (s.phase == Phase.WARMUP) {
                    Adaptive.invalidate(name);
                    s.phase = Phase.ACTIVE;
                }
            }
        }
    }

    public static Phase phase(String name) {
        State s = STATES.get(name);
        return s == null ? Phase.WARMUP : s.phase;
    }

    public static long hits(String name) {
        State s = STATES.get(name);
        return s == null ? 0L : s.hits;
    }

    public static void reset() {
        STATES.clear();
    }
}
