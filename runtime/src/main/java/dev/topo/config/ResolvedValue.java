package dev.topo.config;

/**
 * An effective value plus the layer it came from.
 *
 * <p>Provenance travels with every value so any consumer (a human, an agent, a
 * later read/write path) can answer "which layer set this?" without re-running
 * the merge. Immutable.
 *
 * @param value the effective decoded value
 * @param layer the runtime layer that produced it
 */
public record ResolvedValue(Object value, Layer layer) {
}
