package dev.topo.config;

import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertFalse;
import static org.junit.jupiter.api.Assertions.assertNotEquals;
import static org.junit.jupiter.api.Assertions.assertThrows;
import static org.junit.jupiter.api.Assertions.assertTrue;

import java.lang.reflect.Method;
import java.util.LinkedHashMap;
import java.util.List;
import java.util.Map;
import java.util.Set;

import org.junit.jupiter.api.Test;

/**
 * Acceptance for the tag system, the tag-query API, and the two orthogonal
 * multi-level permission roles (read-visibility tiering vs the write
 * mis-operation gate), plus the tiered-transparency invariant. Mirrors the
 * reference {@code test_config_tags_perm.py}.
 */
class ConfigTagsPermTest {

    // A store where items carry freely-combinable tags and a mix of read
    // tiers, so tag AND, the no-tag default, and the read tier can each be
    // exercised independently.
    private ConfigStore taggedStore() {
        Map<String, Object> inlined = new LinkedHashMap<>();
        inlined.put("log.level", "warn");
        inlined.put("net.timeout_ms", 5000L);
        inlined.put("net.retries", 3L);
        inlined.put("cache.size", 256L);
        inlined.put("db.dsn", "postgres://local");
        inlined.put("secret.api_key", "k-xxx");
        ConfigStore store = new ConfigStore(
                new LayeredConfig(inlined, Map.of(), Map.of()));
        store.declare("log.level", ItemPolicy.withTags("obs"));
        store.declare("net.timeout_ms",
                ItemPolicy.withTags("network", "tuning"));
        store.declare("net.retries", ItemPolicy.withTags("network"));
        store.declare("cache.size", ItemPolicy.withTags("tuning"));
        // Permission-gated: needs read level 2 to be enumerated/read.
        store.declare("db.dsn", new ItemPolicy(ImpactLevel.HIGH,
                Set.of("network"), 2));
        // Higher tier still: read level 3.
        store.declare("secret.api_key", new ItemPolicy(ImpactLevel.HIGH,
                Set.of("network"), 3));
        return store;
    }

    // -- TagQuery -------------------------------------------------------

    @Test
    void singleTagReturnsExactSubset() {
        // network items, but the two permission-gated ones are hidden
        // without a credential.
        assertEquals(List.of("net.retries", "net.timeout_ms"),
                taggedStore().query(Set.of("network")));
    }

    @Test
    void multiTagIsAndCombination() {
        ConfigStore store = taggedStore();
        // Only net.timeout_ms carries BOTH network and tuning.
        assertEquals(List.of("net.timeout_ms"),
                store.query(Set.of("network", "tuning")));
        // Order of the requested tags must not matter (set semantics).
        assertEquals(List.of("net.timeout_ms"),
                store.query(Set.of("tuning", "network")));
    }

    @Test
    void noTagReturnsAllNonPermissionItems() {
        // No tag, no credential: every non-permission item, and the
        // read-gated db.dsn / secret.api_key are excluded by default.
        assertEquals(
                List.of("cache.size", "log.level", "net.retries",
                        "net.timeout_ms"),
                taggedStore().query());
    }

    @Test
    void tagWithNoMatchReturnsEmpty() {
        assertEquals(List.of(),
                taggedStore().query(Set.of("does-not-exist")));
    }

    @Test
    void queryResolvedCarriesValuesAndProvenance() {
        Map<String, ResolvedValue> rv =
                taggedStore().queryResolved(Set.of("tuning"));
        assertEquals(Set.of("net.timeout_ms", "cache.size"), rv.keySet());
        assertEquals(256L, rv.get("cache.size").value());
    }

    // -- ReadVisibilityTiering ------------------------------------------

    @Test
    void gatedItemHiddenWithoutCredential() {
        ConfigStore store = taggedStore();
        assertFalse(store.query().contains("db.dsn"));
        assertFalse(store.query().contains("secret.api_key"));
        assertThrows(WriteProtectionError.class,
                () -> store.read("db.dsn")); // no credential -> unreachable
    }

    @Test
    void eachLevelSeesThatLevelsCompleteRange() {
        ConfigStore store = taggedStore();
        // Level 2 admits db.dsn (read_level 2) but still not the level-3
        // secret — each tier sees its own complete range.
        List<String> keysL2 = store.query(2);
        assertTrue(keysL2.contains("db.dsn"));
        assertFalse(keysL2.contains("secret.api_key"));
        assertEquals("postgres://local", store.read("db.dsn", 2));
        assertThrows(WriteProtectionError.class,
                () -> store.read("secret.api_key", 2));
    }

    @Test
    void tieredTransparencyHighestLevelEnumeratesEverything() {
        // The invariant: the highest read level can always enumerate EVERY
        // runtime item — no fragment is invisible at every level.
        ConfigStore store = taggedStore();
        int top = store.maxReadLevel();
        assertEquals(3, top);
        assertEquals(Set.copyOf(store.keys()),
                Set.copyOf(store.query(top)));
        // And every item is actually readable at the top level.
        for (String key : store.keys()) {
            store.read(key, top);
        }
    }

    @Test
    void tagFilterAndReadTierAreOrthogonal() {
        ConfigStore store = taggedStore();
        int top = store.maxReadLevel();
        // tag=network at the top level reveals the gated network items too;
        // the tag axis did not change, the permission axis did.
        assertEquals(
                List.of("db.dsn", "net.retries", "net.timeout_ms",
                        "secret.api_key"),
                store.query(Set.of("network"), top));
        // Same tag, no credential: only the non-gated network items.
        assertEquals(List.of("net.retries", "net.timeout_ms"),
                store.query(Set.of("network")));
    }

    // -- SameQueryDifferentSites ----------------------------------------

    @Test
    void twoCallsitesDifferentArgsDifferentVisibility() {
        ConfigStore store = taggedStore();
        // Call-site one: a restricted surface, no credential.
        List<String> siteOne = store.query(Set.of("network"));
        // Call-site two: a privileged surface, top credential.
        List<String> siteTwo = store.query(Set.of("network"),
                store.maxReadLevel());

        assertNotEquals(siteOne, siteTwo);
        assertFalse(siteOne.contains("db.dsn"));
        assertTrue(siteTwo.contains("db.dsn"));
    }

    @Test
    void querySignatureTakesNoIdentity() {
        // Java retains no parameter names at runtime by default, so this
        // asserts the structural fact instead: no query-path method takes a
        // reference/identity argument — every credential parameter is the
        // primitive int level, never a principal object.
        for (String name : List.of("query", "queryResolved", "read",
                "maxReadLevel")) {
            for (Method m : ConfigStore.class.getMethods()) {
                if (!m.getName().equals(name)) {
                    continue;
                }
                for (Class<?> p : m.getParameterTypes()) {
                    boolean allowed = p == String.class || p == int.class
                            || p == Set.class;
                    assertTrue(allowed,
                            name + " takes an unexpected param type " + p);
                }
            }
        }
    }

    // -- WriteGateGeneralizedMultiLevel ---------------------------------

    @Test
    void midLevelThresholdViaRequiredCredentialTable() {
        // The write gate is the orthogonal twin of read tiering. Insert a mid
        // threshold by extending the explicit table, not by rewriting logic —
        // proves the scale is multi-level. Snapshot/restore so the global
        // table is left clean for sibling tests.
        Map<ImpactLevel, Integer> original =
                WriteProtection.snapshotRequiredCredentialLevels();
        try {
            // Reuse the HIGH enum slot; map it to level 2 — a table edit.
            WriteProtection.REQUIRED_CREDENTIAL_LEVEL
                    .put(ImpactLevel.HIGH, 2);
            ConfigStore store = new ConfigStore();
            store.declare("db.dsn",
                    ItemPolicy.withImpact(ImpactLevel.HIGH));
            assertThrows(WriteProtectionError.class,
                    () -> store.set("db.dsn", "x", 1)); // below 2
            store.set("db.dsn", "ok", 2); // meets 2
            assertEquals("ok", store.get("db.dsn"));
        } finally {
            WriteProtection.restoreRequiredCredentialLevels(original);
        }
    }

    @Test
    void readLevelAndWriteGateAreIndependentFields() {
        // An item freely readable but write-guarded, and one read-gated but
        // cheap to write — proves the two roles do not collapse.
        ConfigStore store = new ConfigStore();
        store.declare("public.but.guarded",
                new ItemPolicy(ImpactLevel.HIGH, Set.of(), 0));
        store.declare("gated.but.cheap",
                new ItemPolicy(ImpactLevel.LOW, Set.of(), 2));

        // Freely readable, but a write needs a credential.
        assertEquals(0, WriteProtection.requiredReadLevel(
                store.policyOf("public.but.guarded")));
        assertThrows(WriteProtectionError.class,
                () -> store.set("public.but.guarded", 1L));

        // Read-gated, but writing it needs no credential.
        assertEquals(2, WriteProtection.requiredReadLevel(
                store.policyOf("gated.but.cheap")));
        store.set("gated.but.cheap", 1L); // write gate does not bite
        assertThrows(WriteProtectionError.class,
                () -> store.read("gated.but.cheap")); // read tier still bites
    }

    @Test
    void authorizeWriteStillIdentityIndependent() {
        for (Method m : WriteProtection.class.getMethods()) {
            if (m.getName().equals("authorizeWrite")) {
                for (Class<?> p : m.getParameterTypes()) {
                    boolean allowed = p == String.class
                            || p == ItemPolicy.class || p == int.class;
                    assertTrue(allowed,
                            "authorizeWrite takes unexpected param " + p);
                }
            }
        }
    }

    // -- BridgeExposesOneQueryAPI ---------------------------------------

    @Test
    void productConfigQueryPassthrough() {
        ProductConfig pc = new ProductConfig(
                Map.of("a", 1L, "b", 2L), Map.of());
        pc.declare("b", ItemPolicy.withTags("x"));
        pc.declare("a", ItemPolicy.withReadLevel(2));
        assertEquals(List.of("b"), pc.query()); // a is read-gated
        assertEquals(List.of("b"), pc.query(Set.of("x")));
        assertEquals(2, pc.maxReadLevel());
        assertEquals(List.of("a", "b"), pc.query(2));
        assertEquals(1L, pc.read("a", 2));
    }
}
