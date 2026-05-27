package dev.topo.config;

import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertFalse;
import static org.junit.jupiter.api.Assertions.assertThrows;
import static org.junit.jupiter.api.Assertions.assertTrue;

import java.lang.reflect.Method;
import java.nio.file.Files;
import java.nio.file.Path;
import java.time.LocalDateTime;
import java.util.List;
import java.util.Map;
import java.util.NoSuchElementException;

import org.junit.jupiter.api.Test;
import org.junit.jupiter.api.io.TempDir;

/**
 * Acceptance for the layered product-config read/write API: get/set over the
 * frozen b ◁ a ◁ c precedence, stdlib-contract value validation, and the
 * identity-independent high-impact write gate. Mirrors the reference
 * {@code test_config_rw.py}.
 */
class ConfigRwTest {

    // -- ReadWriteRoundTrip ---------------------------------------------

    @Test
    void setThenGetThroughStore() {
        ConfigStore store = new ConfigStore(
                new LayeredConfig(Map.of("log.level", "warn"), Map.of(),
                        Map.of()));
        // An external write must override the inlined default for the same
        // key and report A as the provenance.
        store.set("log.level", "debug");
        assertEquals("debug", store.get("log.level"));
        assertEquals(Layer.A, store.resolve("log.level").layer());
        // An injected (c) value still wins over the external write.
        store.layered().injected().put("log.level", "trace");
        assertEquals("trace", store.get("log.level"));
        assertEquals(Layer.C, store.resolve("log.level").layer());
    }

    @Test
    void getDefaultOnlyWhenNoLayerSetsKey() {
        ConfigStore store = new ConfigStore(
                new LayeredConfig(Map.of("present", 1L), Map.of(), Map.of()));
        assertEquals(42L, store.get("absent", 42L));
        assertThrows(NoSuchElementException.class,
                () -> store.get("absent")); // no default -> no silent null
        assertEquals(1L, store.get("present", 99L));
    }

    @Test
    void setReflectedInSerializedExternalToml(@TempDir Path td)
            throws Exception {
        Path path = td.resolve("topo-app.toml");
        ProductConfig pc = new ProductConfig(path.toString());
        pc.set("cache.size", 256L);
        pc.set("log.level", "debug");
        pc.set("feature.flags", List.of("a", "b"));

        // Round-trips through the same minimal TOML reader.
        Map<String, Object> reloaded =
                TomlReader.parse(Files.readString(path));
        @SuppressWarnings("unchecked")
        Map<String, Object> cache =
                (Map<String, Object>) reloaded.get("cache");
        assertEquals(256L, cache.get("size"));
        @SuppressWarnings("unchecked")
        Map<String, Object> log =
                (Map<String, Object>) reloaded.get("log");
        assertEquals("debug", log.get("level"));
        @SuppressWarnings("unchecked")
        Map<String, Object> feature =
                (Map<String, Object>) reloaded.get("feature");
        assertEquals(List.of("a", "b"), feature.get("flags"));

        // A fresh ProductConfig over the same file reads it back.
        ProductConfig pc2 = new ProductConfig(path.toString());
        assertEquals(256L, pc2.get("cache.size"));
        assertEquals(List.of("a", "b"), pc2.get("feature.flags"));
    }

    @Test
    void keysEnumeratesAllLayers() {
        ConfigStore store = new ConfigStore(new LayeredConfig(
                Map.of("a.x", 1L), Map.of(), Map.of("c.z", 3L)));
        store.set("b.y", 2L);
        assertEquals(List.of("a.x", "b.y", "c.z"), store.keys());
    }

    // -- ValueTypeContract ----------------------------------------------

    @Test
    void stdlibScalarsAccepted() {
        ConfigStore store = new ConfigStore();
        store.set("s", "str");
        store.set("i", 7L);
        store.set("f", 1.5);
        store.set("b", true);
        store.set("arr", List.of(1L, 2L, 3L));
        store.set("rec", Map.of("id", 1L, "amount", 2.0));
        assertEquals(Map.of("id", 1L, "amount", 2.0), store.get("rec"));
    }

    @Test
    void datetimeRejectedPointsToBridgingGap() {
        ConfigStore store = new ConfigStore();
        UnbridgedValueError ex = assertThrows(UnbridgedValueError.class,
                () -> store.set("event.at",
                        LocalDateTime.of(2026, 5, 16, 12, 0, 0)));
        String msg = ex.getMessage();
        assertTrue(msg.contains("event.at"));            // locates the key
        assertTrue(msg.contains("stdlib-bridging"));     // names the gap topic
        assertTrue(msg.contains("time_*"));              // names the missing family
    }

    @Test
    void datetimeNestedInArrayRejected() {
        ConfigStore store = new ConfigStore();
        UnbridgedValueError ex = assertThrows(UnbridgedValueError.class,
                () -> store.set("schedule.points",
                        List.of(LocalDateTime.of(2026, 1, 1, 0, 0))));
        assertTrue(ex.getMessage().contains("schedule.points"));
    }

    @Test
    void nonStdlibObjectRejectedAndLocated() {
        ConfigStore store = new ConfigStore();
        class Custom {
        }
        UnbridgedValueError ex = assertThrows(UnbridgedValueError.class,
                () -> store.set("weird.value", new Custom()));
        String msg = ex.getMessage();
        assertTrue(msg.contains("weird.value"));
        assertTrue(msg.contains("stdlib-bridging"));
    }

    @Test
    void buildToolchainKeyStillRejectedOnWrite() {
        ConfigStore store = new ConfigStore();
        BuildConfigKeyError ex = assertThrows(BuildConfigKeyError.class,
                () -> store.set("build.standard", "c++20"));
        assertTrue(ex.getMessage().contains("Topo.toml"));
    }

    // -- WriteProtectionGate --------------------------------------------

    private ConfigStore guardedStore() {
        ConfigStore store = new ConfigStore();
        store.declare("db.dsn", ItemPolicy.withImpact(ImpactLevel.HIGH));
        store.declare("ui.theme", ItemPolicy.withImpact(ImpactLevel.LOW));
        return store;
    }

    @Test
    void highImpactWriteWithoutCredentialRejected() {
        ConfigStore store = guardedStore();
        WriteProtectionError ex = assertThrows(WriteProtectionError.class,
                () -> store.set("db.dsn", "postgres://prod"));
        String msg = ex.getMessage();
        assertTrue(msg.contains("db.dsn"));
        assertTrue(msg.contains("HIGH"));
        // The guard message is about credentials, never about identity.
        assertFalse(msg.toLowerCase().contains("human"));
        assertFalse(msg.toLowerCase().contains("agent"));
    }

    @Test
    void highImpactWriteWithCredentialSucceeds() {
        ConfigStore store = guardedStore();
        store.set("db.dsn", "postgres://prod", 1);
        assertEquals("postgres://prod", store.get("db.dsn"));
    }

    @Test
    void lowImpactWriteNeedsNoCredential() {
        ConfigStore store = guardedStore();
        store.set("ui.theme", "dark"); // no credential argument at all
        assertEquals("dark", store.get("ui.theme"));
    }

    @Test
    void undeclaredItemDefaultsToLowImpact() {
        ConfigStore store = new ConfigStore();
        store.set("anything.unlisted", 1L); // no declare(), no credential
        assertEquals(1L, store.get("anything.unlisted"));
    }

    @Test
    void gateIsIdentityIndependent() {
        // The authorize/set surface takes a credential *level* and no
        // principal. Java parameter names are not reliably retained at
        // runtime, so identity-independence is asserted structurally: no
        // parameter on the gate is a reference/identity object — the only
        // non-key/value parameter is the primitive int credential level.
        for (Method m : WriteProtection.class.getMethods()) {
            if (m.getName().equals("authorizeWrite")) {
                Class<?>[] p = m.getParameterTypes();
                assertEquals(3, p.length);
                assertEquals(String.class, p[0]);
                assertEquals(ItemPolicy.class, p[1]);
                assertEquals(int.class, p[2]); // a level, never a principal
            }
        }
        boolean sawSet = false;
        for (Method m : ConfigStore.class.getMethods()) {
            if (m.getName().equals("set")
                    && m.getParameterCount() == 3) {
                sawSet = true;
                Class<?>[] p = m.getParameterTypes();
                assertEquals(String.class, p[0]);
                assertEquals(Object.class, p[1]);
                assertEquals(int.class, p[2]); // credential level only
            }
        }
        assertTrue(sawSet);

        // Behavioural equivalence: two callers, same level, same result.
        ConfigStore storeA = guardedStore();
        ConfigStore storeB = guardedStore();
        assertThrows(WriteProtectionError.class,
                () -> storeA.set("db.dsn", "x", 0)); // "the human"
        assertThrows(WriteProtectionError.class,
                () -> storeB.set("db.dsn", "x", 0)); // "the agent"
        storeA.set("db.dsn", "ok", 1);
        storeB.set("db.dsn", "ok", 1);
        assertEquals(storeA.get("db.dsn"), storeB.get("db.dsn"));
    }
}
