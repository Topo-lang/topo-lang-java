package dev.topo.app;

import java.util.ArrayList;
import java.util.List;
import java.util.Objects;

/** A named DAG of handlers, expressed as pipeline edges. */
public final class Flow {

    private final String name;
    private final List<Edge> edges;

    public Flow(String name) {
        this(name, new ArrayList<>());
    }

    public Flow(String name, List<Edge> edges) {
        this.name = Objects.requireNonNull(name, "name");
        this.edges = new ArrayList<>(Objects.requireNonNull(edges, "edges"));
    }

    public String name() {
        return name;
    }

    public List<Edge> edges() {
        return edges;
    }

    void addEdge(Edge e) {
        edges.add(e);
    }
}
