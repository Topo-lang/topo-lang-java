package dev.topo.config;

/**
 * Sentinel marking "this item has no built-in (inlined {@code b}) default".
 *
 * <p>Distinct from a stored {@code null} so a browse consumer can tell "no
 * default exists" from "default is a null-like value". {@link #INSTANCE} is a
 * single shared instance so identity comparison ({@code ==}) is stable for
 * consumers asking "is this row's default the no-default marker?" — the
 * reference relies on the same singleton-identity check.
 */
public final class NoDefault {

    /** The single shared no-default marker. */
    public static final NoDefault INSTANCE = new NoDefault();

    private NoDefault() {
    }

    @Override
    public String toString() {
        return "<no default>";
    }
}
