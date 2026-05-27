package dev.topo.config;

import java.io.IOException;
import java.io.UncheckedIOException;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.nio.file.NoSuchFileException;
import java.nio.file.Path;
import java.util.ArrayList;
import java.util.LinkedHashMap;
import java.util.List;
import java.util.Map;
import java.util.Set;

/**
 * Java projection of the product runtime config entry.
 *
 * <p>Wraps a language-agnostic {@link ConfigStore}; this class only adds the
 * Java ecosystem's file I/O (the minimal {@link TomlReader} / {@link
 * TomlWriter}). {@code set} updates the external layer via the model and
 * re-serialises the user-managed file so a write is immediately reflected on
 * disk and in the next {@code get}.
 *
 * <p>core/bridge split: every semantic decision (merge, provenance, type
 * contract, tags, permission tiers, browse routing, the {@code d} band) lives
 * in the model classes; this bridge owns only TOML decode/encode and file
 * persistence. The pure-internal ({@code d}) catalogue is created lazily and
 * kept on the side — it is <em>not</em> wired into the {@link ConfigStore}, so
 * the runtime read/merge path provably cannot reach a {@code d} item, and a
 * production projection could skip building it entirely.
 */
public final class ProductConfig {

    private final Path path;
    private final ConfigStore store;
    private DevInternalRegistry devInternal;

    public ProductConfig() {
        this(null, Map.of(), Map.of(), Map.of());
    }

    public ProductConfig(String path) {
        this(path, Map.of(), Map.of(), Map.of());
    }

    public ProductConfig(Map<String, Object> inlined,
            Map<String, Object> injected) {
        this(null, inlined, injected, Map.of());
    }

    public ProductConfig(String path, Map<String, Object> inlined,
            Map<String, Object> injected,
            Map<String, ItemPolicy> policies) {
        this.path = path == null ? null : Path.of(path);
        Map<String, Object> external = new LinkedHashMap<>();
        if (this.path != null) {
            try {
                String text = Files.readString(this.path,
                        StandardCharsets.UTF_8);
                external = TomlWriter.flattenNested(TomlReader.parse(text));
            } catch (NoSuchFileException e) {
                external = new LinkedHashMap<>();
            } catch (IOException e) {
                throw new UncheckedIOException(e);
            }
        }
        LayeredConfig layered = new LayeredConfig(
                inlined != null ? inlined : Map.of(),
                external,
                injected != null ? injected : Map.of());
        this.store = new ConfigStore(layered, policies);
    }

    public ConfigStore store() {
        return store;
    }

    /** {@code null} when this config is in-memory only (no file backing). */
    public Path path() {
        return path;
    }

    public void declare(String key, ItemPolicy policy) {
        store.declare(key, policy);
    }

    // -- code-layer inline / hidden TOML (layer b) ----------------------

    /**
     * Embed a TOML config block as the inlined ({@code b}) default.
     *
     * <p>{@code source} may be TOML text ({@code String}) — decoded here with
     * the minimal {@link TomlReader} — or an already-decoded nested
     * {@code Map}. After this call the product needs no external file for
     * these defaults, yet every embedded item still enumerates through
     * {@code keys}/{@code query}/{@code queryResolved} exactly like any
     * {@code b} value: embedding hides the <em>file</em>, never the
     * <em>items</em>. {@code a} and {@code c} keep overriding {@code b}
     * unchanged.
     */
    @SuppressWarnings("unchecked")
    public void declareInlinedToml(Object source) {
        Map<String, Object> decoded;
        if (source instanceof String text) {
            decoded = TomlReader.parse(text);
        } else if (source instanceof byte[] bytes) {
            decoded = TomlReader.parse(
                    new String(bytes, StandardCharsets.UTF_8));
        } else if (source instanceof Map<?, ?> map) {
            decoded = (Map<String, Object>) map;
        } else {
            throw new IllegalArgumentException(
                    "declareInlinedToml expects TOML text (String/byte[]) or "
                            + "an already-decoded mapping, got "
                            + (source == null ? "null"
                                    : source.getClass().getSimpleName()));
        }
        store.layered().installInlined(TomlWriter.flattenNested(decoded));
    }

    /**
     * Reconstruct the embedded ({@code b}) layer as equivalent TOML text.
     *
     * <p>Embedding is not opacity: the inlined block is always recoverable to
     * readable, hand-editable TOML. "Equivalent" means re-parsing the returned
     * text yields the same decoded data the layer holds — guaranteed because
     * it reuses the very same deterministic emitter the external file uses,
     * over the same flat→nested transform, so encode∘decode is the identity
     * for the scalar/array/table config vocabulary.
     */
    public String restoreInlinedToml() {
        return TomlWriter.emit(store.layered().inlined());
    }

    // -- pure-internal (d) declaration ----------------------------------

    /**
     * The dev-phase-only catalogue of {@code d} declarations. Created on
     * demand and kept off the runtime path; a runtime-only build may never
     * touch it.
     */
    public DevInternalRegistry devInternal() {
        if (devInternal == null) {
            devInternal = new DevInternalRegistry();
        }
        return devInternal;
    }

    /** Test-visibility accessor: the side registry, or {@code null} if the
     *  dev band was never touched (a runtime-only projection). */
    DevInternalRegistry devInternalOrNull() {
        return devInternal;
    }

    /**
     * Declare a pure-internal datum and return the plain value.
     *
     * <p>The return value is what the caller binds — identity-equivalent to a
     * hand-written constant, carrying no config-system reference. Its only
     * visibility is dev-phase tag/name lookup via {@link #devInternal()}; it
     * never enters the runtime store, so it is absent from
     * {@code keys}/{@code query}/{@code resolve}.
     */
    public Object declareInternal(String name, Object value,
            List<String> tags) {
        return devInternal().declare(name, value, tags);
    }

    public Object declareInternal(String name, Object value) {
        return devInternal().declare(name, value, List.of());
    }

    // -- runtime passthrough --------------------------------------------
    //
    // Pure passthrough to the language-agnostic store: the Java bridge adds no
    // filtering or row logic of its own, it only exposes the model's one API.

    public List<String> keys() {
        return store.keys();
    }

    public List<String> query() {
        return store.query();
    }

    public List<String> query(Set<String> tags) {
        return store.query(tags);
    }

    public List<String> query(int credentialLevel) {
        return store.query(credentialLevel);
    }

    public List<String> query(Set<String> tags, int credentialLevel) {
        return store.query(tags, credentialLevel);
    }

    public Map<String, ResolvedValue> queryResolved() {
        return store.queryResolved();
    }

    public Map<String, ResolvedValue> queryResolved(int credentialLevel) {
        return store.queryResolved(credentialLevel);
    }

    public int maxReadLevel() {
        return store.maxReadLevel();
    }

    public Object read(String key, int credentialLevel) {
        return store.read(key, credentialLevel);
    }

    /**
     * Self-describing rows for every runtime item within the caller's read
     * tier. Takes a credential <em>level</em> only — no principal/identity —
     * so the same level always yields the same browse. At
     * {@link #maxReadLevel} this is the complete runtime key set; {@code d}
     * items are never included (they have no runtime presence).
     */
    public List<BrowseEntry> browse() {
        return store.browse();
    }

    public List<BrowseEntry> browse(int credentialLevel) {
        return store.browse(credentialLevel);
    }

    public List<BrowseEntry> browse(Set<String> tags, int credentialLevel) {
        return store.browse(tags, credentialLevel);
    }

    /**
     * The dev-phase-only catalogue of pure-internal ({@code d}) data.
     *
     * <p>Explicitly <em>not</em> part of the runtime browse: {@code d} is
     * promoted to a plain host constant and has zero runtime config footprint,
     * so it is absent from {@link #browse} at every level. When {@code tags}
     * is given only {@code d} items whose tag set is a superset match (same
     * freely-combinable tag-AND as the runtime query); otherwise every
     * declared {@code d} item is listed. Returns records distinct in shape
     * from a runtime {@link BrowseEntry} so the two ranges never blur. When no
     * {@code d} was declared, returns empty <em>without</em> creating the side
     * registry as a side effect.
     */
    public List<Map<String, Object>> devBrowse(List<String> tags) {
        if (devInternal == null) {
            return List.of();
        }
        List<String> names = (tags != null && !tags.isEmpty())
                ? devInternal.search(tags)
                : devInternal.names();
        List<Map<String, Object>> out = new ArrayList<>();
        for (String name : names) {
            DevInternalItem item = devInternal.get(name);
            Map<String, Object> record = new LinkedHashMap<>();
            record.put("name", item.name());
            record.put("value", item.value());
            record.put("tags", item.tags());
            out.add(record);
        }
        return out;
    }

    public List<Map<String, Object>> devBrowse() {
        return devBrowse(null);
    }

    public Object get(String key) {
        return store.get(key);
    }

    public Object get(String key, Object defaultValue) {
        return store.get(key, defaultValue);
    }

    public ResolvedValue resolve(String key) {
        return store.resolve(key);
    }

    /**
     * Validate + write through the model, then re-serialise the external
     * layer to the user-managed file (when a path is set).
     */
    public void set(String key, Object value, int credentialLevel) {
        store.set(key, value, credentialLevel);
        if (path != null) {
            writeExternal();
        }
    }

    public void set(String key, Object value) {
        set(key, value, WriteProtection.NO_CREDENTIAL_LEVEL);
    }

    /**
     * The external ({@code a}) layer as deterministic TOML text — the exact
     * bytes {@link #set} writes to {@code topo-app.toml}.
     */
    public String serializeExternal() {
        return TomlWriter.emit(store.pendingExternal());
    }

    private void writeExternal() {
        // Only reachable when a file-backed config is in use; a pathless
        // (in-memory) config never persists.
        if (path == null) {
            throw new IllegalStateException(
                    "cannot persist external layer: this config has no file "
                            + "path");
        }
        try {
            Files.writeString(path, serializeExternal(),
                    StandardCharsets.UTF_8);
        } catch (IOException e) {
            throw new UncheckedIOException(e);
        }
    }
}
