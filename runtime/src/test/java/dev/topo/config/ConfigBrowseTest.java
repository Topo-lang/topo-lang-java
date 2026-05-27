package dev.topo.config;

import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertFalse;
import static org.junit.jupiter.api.Assertions.assertNotEquals;
import static org.junit.jupiter.api.Assertions.assertNull;
import static org.junit.jupiter.api.Assertions.assertSame;
import static org.junit.jupiter.api.Assertions.assertTrue;

import java.lang.reflect.Method;
import java.util.HashMap;
import java.util.LinkedHashMap;
import java.util.List;
import java.util.Map;
import java.util.Set;
import java.util.TreeSet;

import org.junit.jupiter.api.Test;

/**
 * Acceptance for the unified browse + agent-introspection entry: one call
 * returning, within the caller's read tier, a self-describing row per runtime
 * config item, plus a structurally separate dev-phase listing of the
 * pure-internal (d) band. Mirrors the reference {@code test_config_browse.py}.
 */
class ConfigBrowseTest {

    // A store with values arriving from all three runtime layers, a mix of
    // impact levels, read tiers, and freely-combinable tags.
    private ConfigStore layeredStore() {
        Map<String, Object> inlined = new LinkedHashMap<>();
        inlined.put("log.level", "warn");
        inlined.put("net.timeout_ms", 1000L);
        inlined.put("cache.size", 256L);
        inlined.put("db.dsn", "postgres://default");
        Map<String, Object> external = new LinkedHashMap<>();
        external.put("net.timeout_ms", 5000L);
        Map<String, Object> injected = new LinkedHashMap<>();
        injected.put("cache.size", 512L);
        injected.put("feature.flag", true);
        ConfigStore store = new ConfigStore(
                new LayeredConfig(inlined, external, injected));
        store.declare("log.level", ItemPolicy.withTags("obs"));
        store.declare("net.timeout_ms", new ItemPolicy(ImpactLevel.HIGH,
                Set.of("network", "tuning"), 0));
        store.declare("cache.size", ItemPolicy.withTags("tuning"));
        store.declare("feature.flag", ItemPolicy.withTags("features"));
        // Permission-gated: hidden below read level 2.
        store.declare("db.dsn", new ItemPolicy(ImpactLevel.HIGH,
                Set.of("network"), 2));
        return store;
    }

    private Map<String, BrowseEntry> byKey(List<BrowseEntry> rows) {
        Map<String, BrowseEntry> m = new HashMap<>();
        for (BrowseEntry r : rows) {
            m.put(r.key(), r);
        }
        return m;
    }

    // -- FullPerItemSchema ----------------------------------------------

    @Test
    void everyDocumentedFieldPresentAndCorrect() {
        ConfigStore store = layeredStore();
        // Browse at the top tier so every item, including the gated one, is
        // in range and can be schema-checked.
        List<BrowseEntry> rows = store.browse(store.maxReadLevel());
        Map<String, BrowseEntry> by = byKey(rows);

        // b-sourced: log.level. default == effective == the inlined b value;
        // provenance B; low impact, open read tier.
        BrowseEntry log = by.get("log.level");
        assertEquals("str", log.type());
        assertEquals("warn", log.defaultValue());
        assertEquals("warn", log.effective());
        assertSame(Layer.B, log.layer());
        assertSame(ImpactLevel.LOW, log.impact());
        assertEquals(0, log.requiredWriteLevel());
        assertEquals(0, log.requiredReadLevel());
        assertEquals(Set.of("obs"), log.tags());

        // a-sourced: external overrode the inlined default.
        BrowseEntry net = by.get("net.timeout_ms");
        assertEquals("int", net.type());
        assertEquals(1000L, net.defaultValue());
        assertEquals(5000L, net.effective());
        assertSame(Layer.A, net.layer());
        assertSame(ImpactLevel.HIGH, net.impact());
        assertEquals(1, net.requiredWriteLevel());
        assertEquals(0, net.requiredReadLevel());
        assertEquals(Set.of("network", "tuning"), net.tags());

        // c-sourced over a b default.
        BrowseEntry cache = by.get("cache.size");
        assertEquals("int", cache.type());
        assertEquals(256L, cache.defaultValue());
        assertEquals(512L, cache.effective());
        assertSame(Layer.C, cache.layer());

        // c-sourced with NO inlined default -> the no-default sentinel.
        BrowseEntry flag = by.get("feature.flag");
        assertEquals("bool", flag.type());
        assertSame(NoDefault.INSTANCE, flag.defaultValue());
        assertEquals(true, flag.effective());
        assertSame(Layer.C, flag.layer());

        // Gated item: both permission roles exposed.
        BrowseEntry dsn = by.get("db.dsn");
        assertEquals(2, dsn.requiredReadLevel());
        assertEquals(1, dsn.requiredWriteLevel());
        assertEquals("str", dsn.type());
    }

    // -- TieredTransparencyInvariant ------------------------------------

    @Test
    void gatedItemAbsentBelowTierPresentAtAndAbove() {
        ConfigStore store = layeredStore();
        Set<String> below = keySet(store.browse(0));
        assertFalse(below.contains("db.dsn"));
        Set<String> at = keySet(store.browse(2));
        assertTrue(at.contains("db.dsn"));
        Set<String> above = keySet(store.browse(5));
        assertTrue(above.contains("db.dsn"));
    }

    @Test
    void topLevelBrowseEqualsCompleteRuntimeKeySet() {
        ConfigStore store = layeredStore();
        int top = store.maxReadLevel();
        List<String> browsed = new java.util.ArrayList<>(
                new TreeSet<>(keySet(store.browse(top))));
        assertEquals(store.keys(), browsed);
    }

    @Test
    void eachLevelIsExactlyThatLevelsCompleteRange() {
        ConfigStore store = layeredStore();
        List<String> zero = new java.util.ArrayList<>(
                new TreeSet<>(keySet(store.browse(0))));
        List<String> expectedZero = store.keys().stream()
                .filter(k -> store.policyOf(k).readLevel() == 0)
                .sorted().toList();
        assertEquals(expectedZero, zero);
    }

    // -- RoutesThroughTierAwareDoor -------------------------------------

    @Test
    void browseDoesNotUseResolveAllToLeak() {
        ConfigStore store = layeredStore();
        // resolveAll is tier-blind: it would surface the gated key at any
        // level. The browse at level 0 must NOT — proving it goes through the
        // tier-aware door.
        Set<String> tierBlind = store.resolveAll().keySet();
        assertTrue(tierBlind.contains("db.dsn"));

        Set<String> browsed = keySet(store.browse(0));
        assertFalse(browsed.contains("db.dsn"));
        assertNotEquals(browsed, tierBlind);
        for (BrowseEntry r : store.browse(0)) {
            assertNotEquals("db.dsn", r.key());
        }
    }

    // -- IdentityIndependence -------------------------------------------

    @Test
    void signatureHasNoPrincipalIdentityParam() {
        // Java retains no parameter names at runtime; assert the structural
        // fact instead — no browse method takes a reference/identity object,
        // only the primitive int credential level (plus tag Set / key).
        for (Class<?> cls : List.of(ConfigStore.class,
                ProductConfig.class)) {
            for (Method m : cls.getMethods()) {
                if (!m.getName().equals("browse")) {
                    continue;
                }
                for (Class<?> p : m.getParameterTypes()) {
                    boolean allowed = p == int.class || p == Set.class;
                    assertTrue(allowed,
                            cls.getSimpleName()
                                    + ".browse takes a forbidden param " + p);
                }
            }
        }
    }

    @Test
    void sameLevelYieldsIdenticalBrowse() {
        ConfigStore store = layeredStore();
        List<BrowseEntry> a = store.browse(1);
        List<BrowseEntry> b = store.browse(1);
        // Same level -> structurally identical rows; BrowseEntry is a record
        // so equality is structural.
        assertEquals(a, b);
    }

    // -- LiveDerivedNoStaticList ----------------------------------------

    @Test
    void keyAddedAfterConstructionAutoAppears() {
        ConfigStore store = layeredStore();
        assertFalse(keySet(store.browse(0)).contains("late.added"));
        store.set("late.added", "hi"); // lands in the external (a) layer
        assertTrue(keySet(store.browse(0)).contains("late.added"));
    }

    // -- DevPhaseDListing -----------------------------------------------

    @Test
    void dAbsentFromRuntimeBrowseAtEveryLevel() {
        ProductConfig cfg = new ProductConfig(
                Map.of("log.level", "warn"), Map.of());
        cfg.declareInternal("BUILD_SALT", "abc123", List.of("crypto"));
        cfg.declareInternal("MAX_WIDGETS", 64L, List.of("limits"));
        for (int level : new int[] {0, 1, 99}) {
            Set<String> keys = keySet(cfg.browse(level));
            assertFalse(keys.contains("BUILD_SALT"));
            assertFalse(keys.contains("MAX_WIDGETS"));
        }
        assertFalse(cfg.keys().contains("BUILD_SALT"));
    }

    @Test
    void dPresentOnlyInDevListingAndTagSearchable() {
        ProductConfig cfg = new ProductConfig();
        cfg.declareInternal("BUILD_SALT", "abc123", List.of("crypto"));
        cfg.declareInternal("MAX_WIDGETS", 64L, List.of("limits"));

        Set<String> listed = new java.util.HashSet<>();
        for (Map<String, Object> r : cfg.devBrowse()) {
            listed.add((String) r.get("name"));
        }
        assertEquals(Set.of("BUILD_SALT", "MAX_WIDGETS"), listed);

        List<Map<String, Object>> crypto =
                cfg.devBrowse(List.of("crypto"));
        assertEquals(1, crypto.size());
        assertEquals("BUILD_SALT", crypto.get(0).get("name"));
        assertEquals("abc123", crypto.get(0).get("value"));
        assertEquals(Set.of("crypto"), crypto.get(0).get("tags"));
    }

    @Test
    void devBrowseShapeIsDistinctFromRuntimeEntry() {
        ProductConfig cfg = new ProductConfig();
        cfg.declareInternal("BUILD_SALT", "abc123", List.of("crypto"));
        Object rec = cfg.devBrowse().get(0);
        // A dev record is a plain map, never a BrowseEntry — the two ranges
        // are kept structurally disjoint. The dev-listing return type
        // (Map) cannot even be a BrowseEntry, so the disjointness the
        // reference asserts dynamically is here a static guarantee; pin it
        // by shape.
        assertFalse(rec instanceof BrowseEntry);
        assertTrue(rec instanceof Map);
        assertEquals(Set.of("name", "value", "tags"),
                ((Map<?, ?>) rec).keySet());
    }

    @Test
    void noDDeclaredYieldsEmptyListingWithoutRegistry() {
        ProductConfig cfg = new ProductConfig();
        // Browsing the empty dev band must not even create the side registry
        // (a runtime-only build never builds it).
        assertEquals(List.of(), cfg.devBrowse());
        assertNull(cfg.devInternalOrNull());
    }

    // -- ProductConfigBrowseParity --------------------------------------

    @Test
    void bridgeBrowseIsPassthroughToModel() {
        ProductConfig cfg = new ProductConfig(
                Map.of("a.x", 1L, "b.y", "two"),
                Map.of("a.x", 9L));
        cfg.declare("b.y", new ItemPolicy(ImpactLevel.LOW, Set.of("t"), 1));
        // Below tier: gated key absent.
        Set<String> low = keySet(cfg.browse(0));
        assertEquals(Set.of("a.x"), low);
        // At tier: full range, rows are model BrowseEntry objects.
        List<BrowseEntry> full = cfg.browse(cfg.maxReadLevel());
        assertEquals(Set.of("a.x", "b.y"), keySet(full));
        BrowseEntry ax = byKey(full).get("a.x");
        assertEquals(1L, ax.defaultValue());
        assertEquals(9L, ax.effective());
        assertSame(Layer.C, ax.layer());
    }

    private Set<String> keySet(List<BrowseEntry> rows) {
        Set<String> s = new java.util.HashSet<>();
        for (BrowseEntry r : rows) {
            s.add(r.key());
        }
        return s;
    }
}
