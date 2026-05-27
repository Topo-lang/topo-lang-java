package dev.topo.app;

import java.util.Objects;

/**
 * A pipeline edge inside a flow. {@code target} is {@code null} for a terminal
 * edge ({@code source -> void;}).
 */
public record Edge(String source, String target) {

    public Edge {
        Objects.requireNonNull(source, "source");
    }

    public boolean isTerminal() {
        return target == null;
    }
}
