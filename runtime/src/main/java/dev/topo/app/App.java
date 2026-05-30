package dev.topo.app;

import java.util.ArrayList;
import java.util.LinkedHashMap;
import java.util.List;
import java.util.Map;
import java.util.function.Function;
import java.util.function.Supplier;

/**
 * A topo-app program: the in-memory logic graph plus the callables.
 *
 * <p>The topo-app design fixes the philosophy (a handler is a pure In→Out Functor;
 * a flow is a DAG of them) and leaves each host to project it onto its own
 * idioms. The Java projection is an explicit registration call. Java erases
 * generics, so In/Out cannot be recovered from a lambda's type the way the
 * Python projection reads annotations — instead the signature is stated at
 * registration via {@link Schema}, which is the Java-idiomatic equivalent and
 * the same explicit-type discipline the config port uses.
 *
 * <p>A registered handler is returned unchanged: it stays an ordinary callable
 * and remains independently invocable and unit-testable with zero framework
 * bootstrap — a free consequence of the Functor model, not extra design.
 *
 * <p>One App owns one namespace and one flow — enough to exercise every
 * mapping rule without productionising.
 */
public final class App {

    private final Graph graph;
    private final Map<String, Object> fns = new LinkedHashMap<>();

    public App(String namespace) {
        this.graph = new Graph(namespace);
    }

    // --- registration -----------------------------------------------------

    /**
     * Register a single-input handler. The returned {@link Function} is the
     * one passed in, unchanged, so callers keep an ordinary callable.
     */
    public <I, O> Function<I, O> handler(
            String name, TypeRef in, TypeRef out, Function<I, O> fn) {
        if (in == null) {
            throw new IllegalArgumentException(
                    "handler '" + name + "' declared with a Function but no "
                            + "input type; use the Supplier overload for a "
                            + "source handler");
        }
        register(name, in, out, fn);
        return fn;
    }

    /**
     * Register a source handler (no input — the zero-input form is legal).
     * The returned {@link Supplier} is the one passed in, unchanged.
     */
    public <O> Supplier<O> handler(
            String name, TypeRef out, Supplier<O> fn) {
        register(name, null, out, fn);
        return fn;
    }

    private void register(String name, TypeRef in, TypeRef out, Object fn) {
        if (graph.handler(name) != null) {
            throw new IllegalArgumentException(
                    "handler '" + name + "' already registered");
        }
        graph.handlers().add(new Handler(name, in, out));
        fns.put(name, fn);
    }

    /**
     * Declare a linear logic chain: {@code flow("p", "a", "b", "c")} becomes
     * edges a→b→c→void. A {@link Parallel} stage fans in/out from the same
     * neighbours (same-source / same-sink == same-stage parallel candidates,
     * per the topo-app mapping table).
     */
    public void flow(String name, Object... stages) {
        if (stages.length == 0) {
            throw new IllegalArgumentException(
                    "flow '" + name + "' needs at least one stage");
        }
        List<List<String>> resolved = new ArrayList<>(stages.length);
        for (Object stage : stages) {
            resolved.add(stageNames(stage));
        }
        Flow f = new Flow(name);
        for (int i = 0; i < resolved.size() - 1; i++) {
            for (String src : resolved.get(i)) {
                for (String tgt : resolved.get(i + 1)) {
                    f.addEdge(new Edge(src, tgt));
                }
            }
        }
        for (String src : resolved.get(resolved.size() - 1)) {
            f.addEdge(new Edge(src, null)); // terminal -> void
        }
        graph.setFlow(f);
    }

    private List<String> stageNames(Object stage) {
        if (stage instanceof Parallel p) {
            return p.members();
        }
        if (stage instanceof String s) {
            if (graph.handler(s) == null) {
                throw new IllegalArgumentException(
                        "flow references unregistered handler '" + s + "'");
            }
            return List.of(s);
        }
        throw new IllegalArgumentException(
                "flow stage must be a handler name (String) or parallel(...); "
                        + "got " + stage);
    }

    // --- introspection / round-trip --------------------------------------

    public Graph graph() {
        return graph;
    }

    /** The plain callable registered under {@code name}, or {@code null}. */
    public Object callableFor(String name) {
        return fns.get(name);
    }

    /**
     * Independent units fed by the same input == same-stage parallel
     * candidates. Purity of these is enforced by core PurityCheck after
     * emission, never self-asserted here.
     */
    public static Parallel parallel(String... members) {
        return new Parallel(List.of(members));
    }

    /** A same-stage group of parallel handler-name candidates. */
    public record Parallel(List<String> members) {
    }
}
