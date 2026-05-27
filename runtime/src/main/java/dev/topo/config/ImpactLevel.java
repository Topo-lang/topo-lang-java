package dev.topo.config;

/**
 * How disruptive a wrong write to a config item is.
 *
 * <p>Modelled as an <em>ordered</em> scale (not a boolean) from the start so a
 * later multi-tier permission slice can introduce intermediate levels and a
 * per-item required-credential-level without reshaping callers: today only the
 * LOW/HIGH endpoints are used and the gate compares the presented credential
 * level against the item's required level.
 */
public enum ImpactLevel {
    /** Routine; a wrong value is easily noticed and reverted. */
    LOW(0),
    /** Outsized blast radius; a careless write must be deliberate. */
    HIGH(1);

    private final int rank;

    ImpactLevel(int rank) {
        this.rank = rank;
    }

    /** Ordinal position on the ordered impact scale. */
    public int rank() {
        return rank;
    }
}
