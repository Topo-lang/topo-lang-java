package dev.topo.config;

import java.util.Set;

/**
 * A pure-internal datum as seen <em>only during development</em>.
 *
 * <p>Carries the declared name, its constant value, and dev-phase retrieval
 * tags. It has no {@code readLevel}/{@code impact}: those gate runtime
 * visibility and write blast-radius, and {@code d} has neither a runtime
 * presence nor a runtime write path. This object lives solely in
 * {@link DevInternalRegistry}; the runtime config store has no field that can
 * hold it. A record, mirroring the reference's frozen item.
 *
 * @param name  the declared {@code d} name
 * @param value the promoted constant value
 * @param tags  dev-phase retrieval tags (immutable, order-independent)
 */
public record DevInternalItem(String name, Object value, Set<String> tags) {
}
