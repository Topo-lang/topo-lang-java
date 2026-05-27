package dev.topo.config;

import java.util.ArrayList;
import java.util.Collection;
import java.util.LinkedHashMap;
import java.util.List;
import java.util.Map;
import java.util.Set;
import java.util.TreeSet;

/**
 * A development-phase-only catalogue of {@code d} declarations.
 *
 * <p>{@code d} is the innermost band. Unlike a/b/c it is <em>not</em> a
 * runtime config value: after toolchain processing it is promoted to a plain
 * host variable/constant with zero configuration-system footprint, which is
 * why {@link Layer#D} is excluded from
 * {@link LayeredConfig#RUNTIME_MERGE_ORDER} and the runtime merge never sees
 * it. Its tags exist for one purpose only — being discoverable
 * <em>while developing</em> — so it gets its own registry that is structurally
 * disjoint from {@link ConfigStore}/{@link LayeredConfig}. Nothing on the
 * runtime read/merge path holds a reference to this type or its instances; a
 * runtime build can drop this registry entirely without changing any resolved
 * value.
 */
public final class DevInternalRegistry {

    private final Map<String, DevInternalItem> items = new LinkedHashMap<>();

    /**
     * Record a pure-internal datum for dev-phase discovery and return the
     * plain value to be bound as a host constant.
     *
     * <p>The value still must satisfy the stdlib contract (same schema
     * vocabulary as everything else), but it is <em>not</em> stored as a
     * config item anywhere: the return value is what the caller binds,
     * identity-equivalent to a hand-written constant. The build-toolchain
     * boundary guard applies to the name as well, so {@code d} cannot be used
     * to smuggle a build key either.
     */
    public Object declare(String name, Object value, Collection<String> tags) {
        ConfigModel.rejectIfBuildConfigKey(name);
        ConfigModel.validateValue(name, value);
        items.put(name,
                new DevInternalItem(name, value, Set.copyOf(tags)));
        return value;
    }

    public Object declare(String name, Object value) {
        return declare(name, value, List.of());
    }

    /** Every declared {@code d} name, sorted — dev-phase enumeration. */
    public List<String> names() {
        return new ArrayList<>(new TreeSet<>(items.keySet()));
    }

    /** The dev-phase record for {@code name} (raises if undeclared). */
    public DevInternalItem get(String name) {
        DevInternalItem item = items.get(name);
        if (item == null) {
            throw new java.util.NoSuchElementException(name);
        }
        return item;
    }

    /**
     * {@code d} names whose tag set is a superset of {@code tags} (tag AND,
     * same freely-combinable semantics as the runtime tag query) — this is
     * the <em>only</em> retrieval {@code d}'s tags ever serve.
     */
    public List<String> search(Collection<String> tags) {
        Set<String> wanted = Set.copyOf(tags);
        List<String> out = new ArrayList<>();
        for (Map.Entry<String, DevInternalItem> e : items.entrySet()) {
            if (e.getValue().tags().containsAll(wanted)) {
                out.add(e.getKey());
            }
        }
        return new ArrayList<>(new TreeSet<>(out));
    }
}
