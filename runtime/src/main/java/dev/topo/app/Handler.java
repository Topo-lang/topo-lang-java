package dev.topo.app;

import java.util.Objects;

/**
 * A registered logic unit. {@code in} is {@code null} for a source handler
 * (no input — the zero-input form is legal, spec handler/flow §7a).
 */
public final class Handler {

    private final String name;
    private final TypeRef in;
    private final TypeRef out;

    public Handler(String name, TypeRef in, TypeRef out) {
        this.name = Objects.requireNonNull(name, "name");
        this.in = in;
        this.out = Objects.requireNonNull(out, "out");
    }

    public String name() {
        return name;
    }

    /** Null for a source handler. */
    public TypeRef in() {
        return in;
    }

    public TypeRef out() {
        return out;
    }

    /**
     * The {@code handler ...;} declaration line. The single input parameter is
     * conventionally named {@code in} to match the spec's HandlerInput form; a
     * source handler emits empty parentheses.
     */
    public String signature() {
        String param = in == null ? "" : in.topo() + " in";
        return "handler " + name + "(" + param + ") -> " + out.topo() + ";";
    }
}
