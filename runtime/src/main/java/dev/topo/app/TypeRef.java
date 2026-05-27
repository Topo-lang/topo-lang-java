package dev.topo.app;

import java.util.ArrayList;
import java.util.List;
import java.util.Objects;

/**
 * A topo type as it will be spelled in {@code .topo}.
 *
 * <p>Exactly one of {@code scalar} / {@code record} is populated. {@code void}
 * (no input / terminal) is the <em>absence</em> of a TypeRef at the use site,
 * never a TypeRef instance — the same convention the Python projection uses so
 * the round-trip reasons about the graph as data, not sentinels.
 *
 * <p>The scalar vocabulary is the core stdlib spelling ({@code i64} / {@code
 * f64} / {@code bool} / {@code string}) the handler/flow {@code .topo} surface
 * uses bare, not the host-aliased {@code std::*} form that needs a {@code
 * using} preamble. That is the deliberate per-host spelling decision for the
 * Java projection: the handler_flow spec fixtures spell scalars exactly this
 * way and parse with no preamble, so emitting them directly keeps the file
 * both human-readable and round-trip safe.
 */
public final class TypeRef {

    /** An ordered, named field of a stdlib {@code record<...>}. */
    public record Field(String name, TypeRef type) {
        public Field {
            Objects.requireNonNull(name, "field name");
            Objects.requireNonNull(type, "field type");
        }
    }

    private final String scalar;
    private final List<Field> record;

    private TypeRef(String scalar, List<Field> record) {
        this.scalar = scalar;
        this.record = record;
    }

    public static TypeRef scalar(String name) {
        return new TypeRef(Objects.requireNonNull(name, "scalar"), null);
    }

    public static TypeRef record(List<Field> fields) {
        Objects.requireNonNull(fields, "fields");
        if (fields.isEmpty()) {
            // Mirrors core Sema: record<> with no field is rejected upstream;
            // fail early here so the user sees it before emission.
            throw new IllegalArgumentException(
                    "record type must declare at least one field");
        }
        return new TypeRef(null, List.copyOf(fields));
    }

    public boolean isRecord() {
        return record != null;
    }

    public String scalarName() {
        return scalar;
    }

    public List<Field> fields() {
        return record;
    }

    /** The {@code .topo} spelling of this type. */
    public String topo() {
        if (record != null) {
            List<String> parts = new ArrayList<>(record.size());
            for (Field f : record) {
                parts.add(f.name() + ": " + f.type().topo());
            }
            return "record<" + String.join(", ", parts) + ">";
        }
        return scalar;
    }

    @Override
    public boolean equals(Object o) {
        if (this == o) {
            return true;
        }
        if (!(o instanceof TypeRef other)) {
            return false;
        }
        return Objects.equals(scalar, other.scalar)
                && Objects.equals(record, other.record);
    }

    @Override
    public int hashCode() {
        return Objects.hash(scalar, record);
    }

    @Override
    public String toString() {
        return topo();
    }
}
