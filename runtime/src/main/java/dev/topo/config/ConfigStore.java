package dev.topo.config;

import java.util.ArrayList;
import java.util.LinkedHashMap;
import java.util.List;
import java.util.Map;
import java.util.NoSuchElementException;
import java.util.Set;
import java.util.TreeSet;
import java.util.concurrent.ConcurrentHashMap;

/**
 * Read/write façade over {@link LayeredConfig}.
 *
 * <p>Reads honour the frozen {@code b ◁ a ◁ c} precedence. Writes land in the
 * <em>external</em> layer ({@code a}) — the user-managed file's in-memory
 * image — because that is the layer a user/agent is allowed to author; the
 * inlined default ({@code b}) and in-code injection ({@code c}) are owned by
 * other mechanisms. This class stays language-agnostic: it mutates the decoded
 * external map and reports the new value; turning that map into
 * {@code topo-app.toml} bytes is a host-bridge concern, not the model's. A
 * host bridge calls {@link #pendingExternal()} to obtain the map to serialise
 * after a write.
 *
 * <p>One query API, two orthogonal filter dimensions, <em>zero</em> ambient
 * state: it takes the filter (tags, level) as arguments and reads no identity.
 * There is intentionally no principal/user/agent argument anywhere — the scale
 * is consulted by level only.
 *
 * <p><strong>Thread safety.</strong> The per-item policy map is a
 * {@link ConcurrentHashMap}; concurrent {@link #declare} and {@link #policyOf}
 * calls are safe. Mutations of the underlying {@link LayeredConfig} layer
 * maps (driven by {@link #set}) and reads (driven by {@link #query} /
 * {@link #resolve}) compete on those maps' iteration order; serialise
 * {@link #set} calls externally if reads run on another thread, or accept
 * that a concurrent reader may observe a half-installed key.
 */
public final class ConfigStore {

    private final LayeredConfig cfg;
    private final ConcurrentHashMap<String, ItemPolicy> policies;

    public ConfigStore() {
        this(new LayeredConfig(), Map.of());
    }

    public ConfigStore(LayeredConfig layered) {
        this(layered, Map.of());
    }

    public ConfigStore(LayeredConfig layered,
            Map<String, ItemPolicy> policies) {
        this.cfg = layered != null ? layered : new LayeredConfig();
        // Unlisted items default to LOW impact: writes are unguarded unless an
        // item is explicitly declared high-impact.
        this.policies = new ConcurrentHashMap<>(
                policies != null ? policies : Map.of());
    }

    /** The underlying layered model (bridge hook for inline install etc.). */
    public LayeredConfig layered() {
        return cfg;
    }

    // -- declaration -----------------------------------------------------

    /** Attach a write-protection / tag / read-tier policy to {@code key}. */
    public void declare(String key, ItemPolicy policy) {
        ConfigModel.rejectIfBuildConfigKey(key);
        policies.put(key, policy);
    }

    /** The item's declared policy, or the LOW-impact unguarded default. */
    public ItemPolicy policyOf(String key) {
        return policies.getOrDefault(key, new ItemPolicy());
    }

    // -- tag + read-visibility query ------------------------------------

    /**
     * The highest read-level any runtime item requires.
     *
     * <p>A caller presenting this level (or above) can enumerate every runtime
     * item — there is no level at which some runtime fragment stays invisible.
     * This is what makes the tiered-transparency invariant checkable: the top
     * of the scale always sees the whole runtime range. {@code 0} when nothing
     * is permission-gated.
     */
    public int maxReadLevel() {
        int max = 0;
        for (String key : cfg.keys()) {
            max = Math.max(max, policyOf(key).readLevel());
        }
        return max;
    }

    private boolean visible(String key, int credentialLevel) {
        return credentialLevel >= policyOf(key).readLevel();
    }

    /**
     * Keys matching a tag filter <em>and</em> within the caller's read tier.
     *
     * <p>Filtering composes two independent axes: when {@code tags} is null or
     * empty every item matches the tag axis; otherwise an item matches only if
     * its tag set is a <em>superset</em> of the requested set (tag AND, freely
     * combinable). {@code credentialLevel} lists an item only when this level
     * meets the item's read level; with no credential every permission-gated
     * item is hidden. Returns sorted keys for a stable enumeration.
     */
    public List<String> query(Set<String> tags, int credentialLevel) {
        Set<String> wanted = tags == null ? Set.of() : tags;
        List<String> out = new ArrayList<>();
        for (String key : cfg.keys()) {
            if (!visible(key, credentialLevel)) {
                continue;
            }
            if (!wanted.isEmpty()
                    && !policyOf(key).tagSet().containsAll(wanted)) {
                continue;
            }
            out.add(key);
        }
        return new ArrayList<>(new TreeSet<>(out));
    }

    public List<String> query() {
        return query(null, WriteProtection.NO_CREDENTIAL_LEVEL);
    }

    public List<String> query(Set<String> tags) {
        return query(tags, WriteProtection.NO_CREDENTIAL_LEVEL);
    }

    public List<String> query(int credentialLevel) {
        return query(null, credentialLevel);
    }

    /**
     * {@link #query} but returning effective value + provenance for each
     * matched key — the read counterpart of the filter, so a caller within
     * its tier gets values too, not just names.
     */
    public Map<String, ResolvedValue> queryResolved(Set<String> tags,
            int credentialLevel) {
        Map<String, ResolvedValue> out = new LinkedHashMap<>();
        for (String key : query(tags, credentialLevel)) {
            out.put(key, cfg.resolve(key));
        }
        return out;
    }

    public Map<String, ResolvedValue> queryResolved() {
        return queryResolved(null, WriteProtection.NO_CREDENTIAL_LEVEL);
    }

    public Map<String, ResolvedValue> queryResolved(Set<String> tags) {
        return queryResolved(tags, WriteProtection.NO_CREDENTIAL_LEVEL);
    }

    public Map<String, ResolvedValue> queryResolved(int credentialLevel) {
        return queryResolved(null, credentialLevel);
    }

    // -- read ------------------------------------------------------------

    /** Every key any runtime layer contributes, sorted. */
    public List<String> keys() {
        return cfg.keys();
    }

    /**
     * Effective value honouring {@code b ◁ a ◁ c}. A missing key raises
     * {@link NoSuchElementException} (no silent {@code null}).
     */
    public Object get(String key) {
        return cfg.resolve(key).value();
    }

    /**
     * Effective value honouring {@code b ◁ a ◁ c}; returns {@code default}
     * when the key is set by no layer.
     */
    public Object get(String key, Object defaultValue) {
        try {
            return cfg.resolve(key).value();
        } catch (NoSuchElementException e) {
            return defaultValue;
        }
    }

    /** Effective value + which layer it came from. Tier-blind. */
    public ResolvedValue resolve(String key) {
        return cfg.resolve(key);
    }

    /** Every key -&gt; (effective value, provenance layer). Tier-blind. */
    public Map<String, ResolvedValue> resolveAll() {
        return cfg.resolveAll();
    }

    /**
     * Read honouring the read-visibility tier.
     *
     * <p>Below the item's read level the item is treated as not listable, so
     * a read is refused the same way enumeration hides it — a permission-gated
     * item must not be reachable by value either. {@link #get} stays the raw,
     * tier-blind accessor lower layers and the write path rely on; this is the
     * tier-aware door.
     */
    public Object read(String key, int credentialLevel) {
        if (!visible(key, credentialLevel)) {
            int needed = policyOf(key).readLevel();
            throw new WriteProtectionError(
                    "config key '" + key + "' requires read level >= " + needed
                            + " to be listed or read; the request presented "
                            + "level " + credentialLevel + ". Permission-gated "
                            + "items are hidden below their tier; re-issue with "
                            + "a sufficient level.");
        }
        return cfg.resolve(key).value();
    }

    public Object read(String key) {
        return read(key, WriteProtection.NO_CREDENTIAL_LEVEL);
    }

    // -- write -----------------------------------------------------------

    /**
     * Write {@code value} for {@code key} into the external layer ({@code a}).
     *
     * <p>Order of checks: a build-toolchain key is a category error (rejected
     * first); then the value must have a stdlib contract; then the
     * write-protection gate. Only after all three pass is the external map
     * mutated, so a rejected write never leaves a partial state.
     */
    public void set(String key, Object value, int credentialLevel) {
        ConfigModel.rejectIfBuildConfigKey(key);
        ConfigModel.validateValue(key, value);
        WriteProtection.authorizeWrite(key, policyOf(key), credentialLevel);
        cfg.external().put(key, value);
    }

    public void set(String key, Object value) {
        set(key, value, WriteProtection.NO_CREDENTIAL_LEVEL);
    }

    // -- unified browse + introspection ---------------------------------
    //
    // A single call that yields, within the caller's read tier, a
    // self-describing row per config item. It is built strictly on the
    // tier-aware door (queryResolved -> query -> policyOf); it never calls the
    // tier-blind resolveAll/resolve/get, so a permission-gated item cannot
    // leak into a lower-level caller's view. The row set is derived live on
    // every call, so a key declared after construction appears with no list to
    // maintain.

    /**
     * Self-describing rows for every item in the caller's read tier.
     *
     * <p>Routes through {@link #queryResolved} (the tier-aware door), so the
     * result is exactly this level's complete range: at {@link #maxReadLevel}
     * every runtime item is present (the tiered-transparency invariant);
     * below an item's read level that item is wholly absent, value included.
     * The signature takes a credential <em>level</em> only — no
     * principal/identity argument. {@code d} is not a runtime item and never
     * appears here; it has its own dev-phase listing on the registry.
     */
    public List<BrowseEntry> browse(Set<String> tags, int credentialLevel) {
        Map<String, ResolvedValue> resolved =
                queryResolved(tags, credentialLevel);
        List<BrowseEntry> rows = new ArrayList<>();
        for (String key : new TreeSet<>(resolved.keySet())) {
            ResolvedValue rv = resolved.get(key);
            ItemPolicy policy = policyOf(key);
            // Default = the inlined (b) built-in when the key has one; absent
            // otherwise. Read from the b layer map directly (not via the
            // tier-blind resolve), so this stays a pure lookup that cannot
            // widen visibility.
            boolean hasDefault = cfg.inlined().containsKey(key);
            Object defaultValue = hasDefault
                    ? cfg.inlined().get(key)
                    : NoDefault.INSTANCE;
            // Type from the contract that already governs every stored value;
            // prefer the effective value, fall back to the default so a row
            // still types when both exist.
            Object typeSource = rv.value() != null ? rv.value() : defaultValue;
            String valueType;
            try {
                valueType = ConfigModel.stdlibTypeOf(typeSource);
            } catch (UnbridgedValueError e) {
                valueType = ConfigModel.stdlibTypeOf(rv.value());
            }
            rows.add(new BrowseEntry(
                    key,
                    valueType,
                    defaultValue,
                    rv.value(),
                    rv.layer(),
                    policy.impact(),
                    WriteProtection.requiredCredentialLevel(policy),
                    WriteProtection.requiredReadLevel(policy),
                    policy.tagSet()));
        }
        return rows;
    }

    public List<BrowseEntry> browse() {
        return browse(null, WriteProtection.NO_CREDENTIAL_LEVEL);
    }

    public List<BrowseEntry> browse(Set<String> tags) {
        return browse(tags, WriteProtection.NO_CREDENTIAL_LEVEL);
    }

    public List<BrowseEntry> browse(int credentialLevel) {
        return browse(null, credentialLevel);
    }

    // -- bridge hook -----------------------------------------------------

    /**
     * The external-layer map a host bridge serialises to the user-managed
     * config file after a write. Returned by reference so the bridge sees the
     * post-write image; the model never touches files itself.
     */
    public Map<String, Object> pendingExternal() {
        return cfg.external();
    }
}
