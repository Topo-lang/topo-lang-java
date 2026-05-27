package dev.topo;

import jdk.jfr.Category;
import jdk.jfr.Description;
import jdk.jfr.Event;
import jdk.jfr.Label;
import jdk.jfr.Name;
import jdk.jfr.StackTrace;

/**
 * Per-Pass profile JFR events.
 *
 * <p>Four ASM passes (AdaptivePass / ArenaPass / ParallelPass /
 * PipelinePass) inject a single static call to one of the {@code emit*}
 * helpers below at their existing instrumentation site. Each helper
 * commits a custom, stackless JFR event whose {@code @Name} is
 * {@code topo.pass.<PassName>}; the {@code topo-profile-jvm-jfr} bridge
 * passes it through (it already forwards every {@code topo.*} event), and
 * topo-core's {@code JfrNdjsonConverter} routes it into
 * {@code pass_events.<PassName>[]} of the profile trace.
 *
 * <p>Stackless ({@code @StackTrace(false)}) mirrors {@link Observe}:
 * pass events mark a transform-introduced control point, not a sampled
 * call site, so a stack trace would be noise. The bridge's NDJSON shape
 * carries the event-specific fields under a {@code "fields"} object so the
 * converter can surface them verbatim without a per-event C++ schema.
 *
 * <p>The {@code begin()/commit()} dance from {@link Observe} is
 * unnecessary here — these are instantaneous events, so a single
 * {@code commit()} after setting fields is the JFR-idiomatic form.
 */
public final class PassEvents {
    private PassEvents() {}

    /** AdaptivePass: a SwitchPoint dispatch transition (from -> to). */
    @Name("topo.pass.AdaptivePass")
    @Label("Topo AdaptivePass Transition")
    @Description("AdaptivePass SwitchPoint dispatch transition")
    @Category({"Topo", "Pass", "AdaptivePass"})
    @StackTrace(false)
    public static final class AdaptiveTransition extends Event {
        @Label("Method")    public String method;
        @Label("From")      public String from;
        @Label("To")        public String to;
    }

    /** ArenaPass: an arena scope open/close with its size. */
    @Name("topo.pass.ArenaPass")
    @Label("Topo ArenaPass Lifecycle")
    @Description("ArenaPass scope lifecycle (open/close + size)")
    @Category({"Topo", "Pass", "ArenaPass"})
    @StackTrace(false)
    public static final class ArenaLifecycle extends Event {
        @Label("Method")    public String method;
        @Label("Scope")     public String scope;
        @Label("Size")      public long size;
        @Label("Phase")     public String phase; // "open" | "close"
    }

    /** ParallelPass: a Parallel.spawn() fan-out site entered. */
    @Name("topo.pass.ParallelPass")
    @Label("Topo ParallelPass Spawn")
    @Description("ParallelPass parallel spawn fan-out")
    @Category({"Topo", "Pass", "ParallelPass"})
    @StackTrace(false)
    public static final class ParallelSpawn extends Event {
        @Label("Method")     public String method;
        @Label("Spawn Site") public String spawnSite;
    }

    /** PipelinePass: a lowered pipeline orchestrator composed. */
    @Name("topo.pass.PipelinePass")
    @Label("Topo PipelinePass Compose")
    @Description("PipelinePass CompletableFuture chain composition")
    @Category({"Topo", "Pass", "PipelinePass"})
    @StackTrace(false)
    public static final class PipelineCompose extends Event {
        @Label("Method")   public String method;
        @Label("Topology") public String topology;
    }

    // --- static emit helpers (the bytecode-injected call targets) ------

    public static void emitAdaptiveTransition(String method, String from,
                                              String to) {
        var e = new AdaptiveTransition();
        e.method = method;
        e.from = from;
        e.to = to;
        e.commit();
    }

    public static void emitArenaLifecycle(String method, String scope,
                                          long size, String phase) {
        var e = new ArenaLifecycle();
        e.method = method;
        e.scope = scope;
        e.size = size;
        e.phase = phase;
        e.commit();
    }

    public static void emitParallelSpawn(String method, String spawnSite) {
        var e = new ParallelSpawn();
        e.method = method;
        e.spawnSite = spawnSite;
        e.commit();
    }

    public static void emitPipelineCompose(String method, String topology) {
        var e = new PipelineCompose();
        e.method = method;
        e.topology = topology;
        e.commit();
    }
}
