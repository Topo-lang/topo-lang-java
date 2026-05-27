package dev.topo.config;

/**
 * Which runtime layer a configuration value originates from.
 *
 * <p>The enum {@code precedence} encodes the merge ordering (higher wins) so
 * the merge never hard-codes an ordering separate from the layer identity.
 * {@code D} is listed for vocabulary completeness but is never produced by the
 * runtime merge: a {@code d} datum is promoted to a plain host constant by the
 * toolchain and has zero configuration-system footprint at runtime, so there is
 * nothing to merge.
 */
public enum Layer {
    /** Inlined / hidden TOML default embedded in the artifact. */
    B(1),
    /** External topo-app.toml the user manages. */
    A(2),
    /** In-code explicit injection through the topo interface. */
    C(3),
    /** Pure-internal; promoted to code, never merged at runtime. */
    D(0);

    private final int precedence;

    Layer(int precedence) {
        this.precedence = precedence;
    }

    /** Merge precedence; a higher value overrides a lower one for the same key. */
    public int precedence() {
        return precedence;
    }
}
