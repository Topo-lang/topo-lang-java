package dev.topo.app;

import java.io.IOException;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.nio.file.Path;
import java.util.ArrayList;
import java.util.LinkedHashMap;
import java.util.List;
import java.util.Map;

/**
 * The single unified configuration entry: {@code config(app)}.
 *
 * <p>{@code snapshot()} and {@code emitTopo()} are two views of the same logic
 * structure — the snapshot is the human/agent overview, the {@code .topo} is
 * the toolchain-consumable contract. They stay consistent by construction
 * because both derive from the same {@link Graph}.
 */
public final class Config {

    private final App app;

    private Config(App app) {
        this.app = app;
    }

    /** The one {@code config(app)} entry the topo-app design names. */
    public static Config of(App app) {
        return new Config(app);
    }

    public Graph graph() {
        return app.graph();
    }

    /**
     * The full graph: every handler with In/Out, every connection — one
     * place, the whole picture. Insertion-ordered so the overview reads in
     * declaration order.
     */
    public Map<String, Object> snapshot() {
        Graph g = app.graph();
        Map<String, Object> snap = new LinkedHashMap<>();
        snap.put("namespace", g.namespace());
        List<Map<String, Object>> hs = new ArrayList<>();
        for (Handler h : g.handlers()) {
            Map<String, Object> hm = new LinkedHashMap<>();
            hm.put("name", h.name());
            hm.put("in", h.in() == null ? null : h.in().topo());
            hm.put("out", h.out().topo());
            hs.add(hm);
        }
        snap.put("handlers", hs);
        Flow flow = g.flow();
        if (flow == null) {
            snap.put("flow", null);
        } else {
            Map<String, Object> fm = new LinkedHashMap<>();
            fm.put("name", flow.name());
            List<Map<String, Object>> es = new ArrayList<>();
            for (Edge e : flow.edges()) {
                Map<String, Object> em = new LinkedHashMap<>();
                em.put("from", e.source());
                em.put("to", e.isTerminal() ? "void" : e.target());
                es.add(em);
            }
            fm.put("edges", es);
            snap.put("flow", fm);
        }
        return snap;
    }

    /** The round-trippable {@code .topo} view of the same structure. */
    public String emitTopo() {
        return Emitter.emit(app.graph());
    }

    public String emitTopo(Path path) {
        String text = emitTopo();
        try {
            Files.writeString(path, text, StandardCharsets.UTF_8);
        } catch (IOException e) {
            throw new IllegalStateException(
                    "failed writing .topo to " + path, e);
        }
        return text;
    }

    /** Emit then read back through the real parser. Returns graph'. */
    public Graph roundtrip() {
        return Readback.read(emitTopo());
    }
}
