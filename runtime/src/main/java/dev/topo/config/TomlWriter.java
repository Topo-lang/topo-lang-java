package dev.topo.config;

import java.util.ArrayList;
import java.util.LinkedHashMap;
import java.util.List;
import java.util.Map;
import java.util.TreeMap;

/**
 * A minimal, deterministic TOML writer for the product-config vocabulary, plus
 * the flat↔nested transforms the bridge shares between the external file and
 * the inlined ({@code b}) layer.
 *
 * <p>Mirrors the Python bridge: {@code tomli-w} is an optional third-party
 * package, so rather than add a hard runtime dependency the bridge ships a
 * minimal deterministic emitter (sorted keys, stable table nesting) good
 * enough for the flat scalar/array/table config vocabulary the model accepts.
 * It reuses the same flat→nested transform the inline-restore path uses, so
 * encode∘decode is the identity over the supported value set — the round-trip
 * guarantee.
 */
public final class TomlWriter {

    private TomlWriter() {
    }

    /**
     * Turn dotted keys ({@code a.b.c}) into nested map structure so the
     * serialised TOML uses idiomatic {@code [a.b]} tables instead of quoted
     * dotted keys. Sorted iteration keeps the structure deterministic.
     */
    @SuppressWarnings("unchecked")
    public static Map<String, Object> splitNested(Map<String, Object> flat) {
        Map<String, Object> root = new LinkedHashMap<>();
        for (String dotted : new TreeMap<>(flat).keySet()) {
            String[] parts = dotted.split("\\.");
            Map<String, Object> cursor = root;
            for (int i = 0; i < parts.length - 1; i++) {
                cursor = (Map<String, Object>) cursor.computeIfAbsent(
                        parts[i], k -> new LinkedHashMap<String, Object>());
            }
            cursor.put(parts[parts.length - 1], flat.get(dotted));
        }
        return root;
    }

    /**
     * Inverse of {@link #splitNested}: a decoded TOML document back to the
     * model's flat dotted-key map. A {@code Map} is treated as a nested table;
     * a value the model stores as a {@code record} (also a {@code Map}) only
     * appears as a value, never recursed into here — the config vocabulary
     * keys are addressed by dotted path and a stored table value is itself a
     * leaf in that addressing.
     *
     * <p>Heuristic matching the Python bridge: a non-empty inline-table value
     * (a stored {@code record}) is a leaf; a nesting table produced by
     * sections recurses. The reference treats every decoded {@code dict} from
     * a section as a nesting table, so this recurses on maps and the inline
     * record is preserved only when written/read as an inline table value.
     */
    public static Map<String, Object> flattenNested(
            Map<String, Object> nested) {
        Map<String, Object> flat = new LinkedHashMap<>();
        flattenInto(nested, "", flat);
        return flat;
    }

    @SuppressWarnings("unchecked")
    private static void flattenInto(Map<String, Object> nested, String prefix,
            Map<String, Object> flat) {
        for (Map.Entry<String, Object> e : nested.entrySet()) {
            String key = prefix.isEmpty()
                    ? e.getKey()
                    : prefix + "." + e.getKey();
            Object value = e.getValue();
            if (value instanceof Map<?, ?> map) {
                flattenInto((Map<String, Object>) map, key, flat);
            } else {
                flat.put(key, value);
            }
        }
    }

    /**
     * The flat config map as deterministic TOML text (sorted keys, stable
     * table nesting). Trailing newline only when non-empty, matching the
     * reference emitter so round-trips are byte-stable.
     */
    public static String emit(Map<String, Object> flat) {
        Map<String, Object> nested = splitNested(flat);
        String body = emitNested(nested, new ArrayList<>()).strip();
        return body.isEmpty() ? "" : body + "\n";
    }

    @SuppressWarnings("unchecked")
    private static String emitNested(Map<String, Object> nested,
            List<String> path) {
        Map<String, Object> scalars = new TreeMap<>();
        Map<String, Object> subtables = new TreeMap<>();
        for (String name : new TreeMap<>(nested).keySet()) {
            Object value = nested.get(name);
            // A nesting table (from splitNested) recurses as a sub-section;
            // any other value (incl. an inline-table record) is a scalar.
            if (value instanceof Map && !isRecordValue(value)) {
                subtables.put(name, value);
            } else {
                scalars.put(name, value);
            }
        }

        List<String> out = new ArrayList<>();
        for (Map.Entry<String, Object> e : scalars.entrySet()) {
            out.add(e.getKey() + " = " + tomlValue(e.getValue()));
        }
        for (Map.Entry<String, Object> e : subtables.entrySet()) {
            List<String> childPath = new ArrayList<>(path);
            childPath.add(e.getKey());
            String section = String.join(".", childPath);
            String body = emitNested((Map<String, Object>) e.getValue(),
                    childPath);
            out.add("\n[" + section + "]");
            if (!body.isEmpty()) {
                out.add(body);
            }
        }
        StringBuilder sb = new StringBuilder();
        for (String part : out) {
            if (part.isEmpty()) {
                continue;
            }
            if (sb.length() > 0) {
                sb.append('\n');
            }
            sb.append(part);
        }
        return sb.toString();
    }

    /**
     * A map reached as a value (record) vs. a nesting table. The model
     * addresses keys by dotted path, so all maps coming from
     * {@link #splitNested} are nesting tables — this stays {@code false}; it
     * exists as the explicit seam for inline records, matching the reference.
     */
    private static boolean isRecordValue(Object value) {
        return false;
    }

    private static String tomlValue(Object value) {
        if (value instanceof Map<?, ?> map) {
            List<String> pairs = new ArrayList<>();
            for (Object k : new TreeMap<>((Map<String, Object>) map)
                    .keySet()) {
                pairs.add(k + " = " + tomlValue(((Map<?, ?>) map).get(k)));
            }
            return "{" + String.join(", ", pairs) + "}";
        }
        return tomlScalar(value);
    }

    @SuppressWarnings("unchecked")
    private static String tomlScalar(Object value) {
        if (value instanceof Boolean b) {
            return b ? "true" : "false";
        }
        if (value instanceof Long || value instanceof Integer
                || value instanceof Short || value instanceof Byte) {
            return value.toString();
        }
        if (value instanceof Double || value instanceof Float) {
            // Render with a decimal point so the reader classifies it as a
            // float on the way back (round-trip stability).
            double d = ((Number) value).doubleValue();
            if (d == Math.floor(d) && !Double.isInfinite(d)) {
                return (long) d + ".0";
            }
            return Double.toString(d);
        }
        if (value instanceof String s) {
            String escaped = s
                    .replace("\\", "\\\\")
                    .replace("\"", "\\\"")
                    .replace("\n", "\\n")
                    .replace("\t", "\\t");
            return "\"" + escaped + "\"";
        }
        if (value instanceof List<?> list) {
            List<String> parts = new ArrayList<>();
            for (Object v : list) {
                parts.add(tomlScalar(v));
            }
            return "[" + String.join(", ", parts) + "]";
        }
        if (value instanceof Map) {
            return tomlValue(value);
        }
        // Datetime types are intentionally not handled — the model rejects
        // them before a write reaches the bridge, so reaching here would be a
        // contract violation worth surfacing loudly.
        throw new IllegalArgumentException(
                "value of type " + (value == null ? "null"
                        : value.getClass().getSimpleName())
                        + " is not TOML-serialisable");
    }
}
