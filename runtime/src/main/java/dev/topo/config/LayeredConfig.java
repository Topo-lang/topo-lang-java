package dev.topo.config;

import java.util.ArrayList;
import java.util.LinkedHashMap;
import java.util.List;
import java.util.Map;
import java.util.NoSuchElementException;
import java.util.TreeSet;

/**
 * The a/b/c layers as plain decoded data plus the merge over them.
 *
 * <p>Each layer is a flat mapping of dotted-key -&gt; already-decoded plain
 * value (scalar / list / map). TOML parsing is a separate concern: a bridge
 * fills these maps; this model only merges and attributes them.
 *
 * <p>Frozen merge precedence (user-confirmed): inlined default (b) ◁ external
 * file (a) ◁ in-code injection (c). "More explicit wins": {@code c} overrides
 * {@code a} overrides {@code b}, per key. {@link Layer#D} never participates;
 * a {@code d} datum is promoted to a host constant with zero runtime config
 * footprint, so resolving an effective value is an a/b/c-only operation by
 * construction.
 */
public final class LayeredConfig {

    /** Layers that participate in the runtime merge, least to most explicit. */
    public static final List<Layer> RUNTIME_MERGE_ORDER =
            List.of(Layer.B, Layer.A, Layer.C);

    private Map<String, Object> inlined;   // layer b
    private final Map<String, Object> external;  // layer a
    private final Map<String, Object> injected;  // layer c

    public LayeredConfig() {
        this(Map.of(), Map.of(), Map.of());
    }

    public LayeredConfig(Map<String, Object> inlined,
            Map<String, Object> external, Map<String, Object> injected) {
        this.inlined = new LinkedHashMap<>(inlined);
        this.external = new LinkedHashMap<>(external);
        this.injected = new LinkedHashMap<>(injected);
    }

    /**
     * Register a block of already-decoded data as the inlined (b) layer — the
     * artifact-embedded default.
     *
     * <p>This is the model side of an explicit code-layer declaration that a
     * config block travels inside the artifact instead of as a scattered
     * external file. It is deliberately decode-only: the caller (a host
     * bridge) turns TOML text into this plain map and, symmetrically, restores
     * the map back to equivalent TOML. Embedding changes where the
     * <em>file</em> lives, never whether the <em>items</em> are browsable: the
     * installed keys merge as the ordinary {@code b} default. Build-toolchain
     * keys are rejected here too, so a misplaced key cannot sneak in through
     * the embedded layer any more than through the external file.
     */
    public void installInlined(Map<String, Object> data) {
        for (String key : data.keySet()) {
            ConfigModel.rejectIfBuildConfigKey(key);
        }
        this.inlined = new LinkedHashMap<>(data);
    }

    /** Mutable view of the external (a) layer — the write target. */
    public Map<String, Object> external() {
        return external;
    }

    /** Mutable view of the injected (c) layer. */
    public Map<String, Object> injected() {
        return injected;
    }

    /** Read-only view of the inlined (b) layer. */
    public Map<String, Object> inlined() {
        return inlined;
    }

    Map<String, Object> layerMap(Layer layer) {
        return switch (layer) {
            case B -> inlined;
            case A -> external;
            case C -> injected;
            // Layer.D never participates in the runtime merge by construction.
            case D -> throw new IllegalStateException(
                    layer + " is not a runtime merge layer");
        };
    }

    private void validateKeys() {
        for (Layer layer : RUNTIME_MERGE_ORDER) {
            for (String key : layerMap(layer).keySet()) {
                ConfigModel.rejectIfBuildConfigKey(key);
            }
        }
    }

    /**
     * Every key contributed by any runtime layer, sorted for a stable,
     * hand-checkable enumeration.
     */
    public List<String> keys() {
        TreeSet<String> seen = new TreeSet<>();
        for (Layer layer : RUNTIME_MERGE_ORDER) {
            seen.addAll(layerMap(layer).keySet());
        }
        return new ArrayList<>(seen);
    }

    /**
     * Effective value + provenance for one key. Walks the layers
     * least-to-most explicit; the last layer that carries the key wins, and
     * that layer is the recorded provenance.
     */
    public ResolvedValue resolve(String key) {
        ConfigModel.rejectIfBuildConfigKey(key);
        ResolvedValue winner = null;
        for (Layer layer : RUNTIME_MERGE_ORDER) {
            Map<String, Object> layerMap = layerMap(layer);
            if (layerMap.containsKey(key)) {
                winner = new ResolvedValue(layerMap.get(key), layer);
            }
        }
        if (winner == null) {
            throw new NoSuchElementException(key);
        }
        return winner;
    }

    /**
     * The unified result: every key -&gt; (effective value, provenance
     * layer). Build-toolchain keys are rejected up front so a misplaced key
     * fails loudly rather than appearing as a phantom entry. Insertion order
     * follows {@link #keys()} (sorted) for a stable enumeration.
     */
    public Map<String, ResolvedValue> resolveAll() {
        validateKeys();
        Map<String, ResolvedValue> out = new LinkedHashMap<>();
        for (String key : keys()) {
            out.put(key, resolve(key));
        }
        return out;
    }

    /**
     * Convenience: build a {@link LayeredConfig} from the three layer maps and
     * return the resolved key -&gt; value+provenance mapping.
     */
    public static Map<String, ResolvedValue> mergeLayers(
            Map<String, Object> inlined, Map<String, Object> external,
            Map<String, Object> injected) {
        return new LayeredConfig(
                inlined == null ? Map.of() : inlined,
                external == null ? Map.of() : external,
                injected == null ? Map.of() : injected).resolveAll();
    }

    /**
     * Flatten a resolved mapping to {@code (key, value, layer)} triples in
     * stable key order — the shape later browse/introspection paths read.
     */
    public static List<Provenance> iterProvenance(
            Map<String, ResolvedValue> resolved) {
        List<Provenance> out = new ArrayList<>();
        for (String key : new TreeSet<>(resolved.keySet())) {
            ResolvedValue rv = resolved.get(key);
            out.add(new Provenance(key, rv.value(), rv.layer()));
        }
        return out;
    }

    /** A flattened {@code (key, value, layer)} provenance triple. */
    public record Provenance(String key, Object value, Layer layer) {
    }
}
