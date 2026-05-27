package dev.topo.config;

import java.util.Collection;
import java.util.Objects;
import java.util.Set;

/**
 * Per-item declaration carrying three <em>orthogonal</em> dimensions.
 *
 * <ul>
 * <li>{@code tags} — a freely-combinable set of strings scoping
 * <em>retrieval</em>. A pure label set: tags never affect read or write
 * permission, only which filter a query matches. Stored as an immutable set so
 * the policy stays an immutable value and tag identity is order-independent.
 * <li>{@code readLevel} — the minimum permission level a caller must present
 * to have this item <em>enumerated or read</em>. Default {@code 0} means the
 * item is visible to everyone. A value above {@code 0} makes the item
 * permission-gated: hidden from any query below that level, listed only
 * at/above it.
 * <li>{@code impact} — independent of the two above: it drives the
 * <em>write</em> mis-operation gate (a wrong write's blast radius), not
 * visibility.
 * </ul>
 *
 * <p>The two permission roles (read-visibility tiering via {@code readLevel}
 * and the write gate via {@code impact}) ride the same integer scale but are
 * deliberately separate fields: an item can be freely readable yet
 * write-guarded, or read-gated yet low-impact to write. Tags are a third,
 * permission-independent axis.
 *
 * <p>Immutable so a policy cannot be mutated by a consumer (the reference
 * contract's frozen dataclass).
 */
public final class ItemPolicy {

    private final ImpactLevel impact;
    private final Set<String> tags;
    private final int readLevel;

    /** The unguarded default: LOW impact, no tags, open read tier. */
    public ItemPolicy() {
        this(ImpactLevel.LOW, Set.of(), 0);
    }

    public ItemPolicy(ImpactLevel impact, Collection<String> tags,
            int readLevel) {
        this.impact = Objects.requireNonNull(impact, "impact");
        // Accept any collection of tag strings at the call site but always
        // store an immutable, order-independent set.
        this.tags = Set.copyOf(tags);
        this.readLevel = readLevel;
    }

    // -- Builder-style helpers so a call site can name only the axes it sets
    //    (the reference uses keyword defaults; Java needs explicit factories).

    public static ItemPolicy of() {
        return new ItemPolicy();
    }

    public static ItemPolicy withImpact(ImpactLevel impact) {
        return new ItemPolicy(impact, Set.of(), 0);
    }

    public static ItemPolicy withTags(String... tags) {
        return new ItemPolicy(ImpactLevel.LOW, Set.of(tags), 0);
    }

    public static ItemPolicy withReadLevel(int readLevel) {
        return new ItemPolicy(ImpactLevel.LOW, Set.of(), readLevel);
    }

    public ItemPolicy impact(ImpactLevel newImpact) {
        return new ItemPolicy(newImpact, this.tags, this.readLevel);
    }

    public ItemPolicy tags(String... newTags) {
        return new ItemPolicy(this.impact, Set.of(newTags), this.readLevel);
    }

    public ItemPolicy readLevel(int newReadLevel) {
        return new ItemPolicy(this.impact, this.tags, newReadLevel);
    }

    public ImpactLevel impact() {
        return impact;
    }

    /** Immutable, order-independent tag set. */
    public Set<String> tagSet() {
        return tags;
    }

    public int readLevel() {
        return readLevel;
    }

    @Override
    public boolean equals(Object o) {
        if (this == o) {
            return true;
        }
        if (!(o instanceof ItemPolicy other)) {
            return false;
        }
        return readLevel == other.readLevel && impact == other.impact
                && tags.equals(other.tags);
    }

    @Override
    public int hashCode() {
        return Objects.hash(impact, tags, readLevel);
    }
}
