package dev.topo.config;

import java.util.Set;

/**
 * One self-describing row of the unified browse.
 *
 * <p>Carries everything a human or an agent needs to judge a config item
 * without a second query: its identity and contract type, the built-in default
 * and the current effective value with the layer that produced it, the write
 * blast-radius ({@code impact}) and <em>both</em> permission thresholds —
 * {@code requiredWriteLevel} (the mis-operation gate) and
 * {@code requiredReadLevel} (the read-visibility tier) — kept as separate
 * fields because the two roles are orthogonal, plus the freely-combinable
 * retrieval {@code tags}. {@code defaultValue} is the {@link NoDefault#INSTANCE}
 * sentinel when the item has no inlined default. A record, so a browse row
 * cannot be mutated by a consumer and equality is structural (the reference's
 * frozen entry).
 *
 * @param key                identity of the config item
 * @param type               stdlib-contract spelling of the effective/default
 * @param defaultValue       the inlined {@code b} default, or
 *                           {@link NoDefault#INSTANCE}
 * @param effective          the current effective value
 * @param layer              provenance layer (a/b/c)
 * @param impact             write blast-radius level
 * @param requiredWriteLevel the write mis-operation gate threshold
 * @param requiredReadLevel  the read-visibility tier threshold
 * @param tags               freely-combinable retrieval tags
 */
public record BrowseEntry(
        String key,
        String type,
        Object defaultValue,
        Object effective,
        Layer layer,
        ImpactLevel impact,
        int requiredWriteLevel,
        int requiredReadLevel,
        Set<String> tags) {
}
