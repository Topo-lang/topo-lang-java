package dev.topo.config;

import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertFalse;
import static org.junit.jupiter.api.Assertions.assertNull;
import static org.junit.jupiter.api.Assertions.assertSame;
import static org.junit.jupiter.api.Assertions.assertThrows;
import static org.junit.jupiter.api.Assertions.assertTrue;

import java.lang.reflect.Field;
import java.time.LocalDate;
import java.util.List;
import java.util.Map;
import java.util.NoSuchElementException;
import java.util.Set;

import org.junit.jupiter.api.Test;

/**
 * Acceptance for two innermost-band mechanisms: code-layer inlined / hidden
 * TOML (the embedded {@code b} default) and the pure-internal band. Mirrors
 * the reference {@code test_config_inline_internal.py}.
 */
class ConfigInlineInternalTest {

    private static final String TOML_SRC = """
            log_level = "info"
            retries = 3
            ratio = 0.5
            enabled = true

            [net]
            host = "example.com"
            ports = [80, 443]
            """;

    // -- InlineDeclareNoExternalFileNeeded ------------------------------

    @Test
    void inlineDeclaredDefaultsNeedNoExternalFile() {
        // No path given => no scattered external TOML file at all, yet the
        // embedded defaults are fully resolvable.
        ProductConfig pc = new ProductConfig(); // path is null
        pc.declareInlinedToml(TOML_SRC);
        assertNull(pc.path());
        assertEquals("info", pc.get("log_level"));
        assertEquals("example.com", pc.get("net.host"));
        assertEquals(List.of(80L, 443L), pc.get("net.ports"));
        // Provenance: every value comes from the inlined (b) layer.
        for (String key : pc.keys()) {
            assertSame(Layer.B, pc.resolve(key).layer());
        }
    }

    @Test
    void acceptsAlreadyDecodedMappingToo() {
        ProductConfig pc = new ProductConfig();
        pc.declareInlinedToml(Map.of("a", 1L,
                "nested", Map.of("b", 2L)));
        assertEquals(1L, pc.get("a"));
        assertEquals(2L, pc.get("nested.b"));
    }

    // -- InlineRoundTrip ------------------------------------------------

    @Test
    void restoreYieldsTomlReparsingToIdenticalData() {
        ProductConfig pc = new ProductConfig();
        pc.declareInlinedToml(TOML_SRC);

        String restored = pc.restoreInlinedToml();
        // Equivalent == re-parsing yields the same decoded data as the
        // original source decoded.
        assertEquals(TomlReader.parse(TOML_SRC),
                TomlReader.parse(restored));
    }

    @Test
    void restoreIsIdempotentUnderReparse() {
        ProductConfig pc = new ProductConfig();
        pc.declareInlinedToml(TOML_SRC);
        String once = pc.restoreInlinedToml();
        // Feed the restored text back in and restore again: stable.
        ProductConfig pc2 = new ProductConfig();
        pc2.declareInlinedToml(once);
        assertEquals(TomlReader.parse(once),
                TomlReader.parse(pc2.restoreInlinedToml()));
    }

    @Test
    void emptyInlineRestoresToEmpty() {
        ProductConfig pc = new ProductConfig();
        pc.declareInlinedToml(Map.of());
        assertEquals("", pc.restoreInlinedToml());
        assertEquals(Map.of(),
                TomlReader.parse(pc.restoreInlinedToml()));
    }

    // -- FileHiddenNotItemHidden ----------------------------------------

    @Test
    void inlinedItemsStillEnumerateUnderNormalRules() {
        ProductConfig pc = new ProductConfig();
        pc.declareInlinedToml(TOML_SRC);
        List<String> keys = pc.keys();
        for (String k : List.of("log_level", "retries", "ratio", "enabled",
                "net.host", "net.ports")) {
            assertTrue(keys.contains(k), "missing " + k);
        }
        // Tag and read-tier rules still apply to inlined items normally.
        pc.declare("retries", ItemPolicy.withTags("tuning"));
        pc.declare("net.host", ItemPolicy.withReadLevel(2));
        assertEquals(List.of("retries"), pc.query(Set.of("tuning")));
        // A read-gated inlined item hides by default but the top tier still
        // enumerates it (tiered-transparency holds for b too).
        assertFalse(pc.query().contains("net.host"));
        assertTrue(pc.query(2).contains("net.host"));
        Map<String, ResolvedValue> rv = pc.queryResolved();
        assertTrue(rv.containsKey("log_level"));
        assertEquals("info", rv.get("log_level").value());
    }

    @Test
    void aAndCStillOverrideInlinedB() {
        ProductConfig pc = new ProductConfig(
                Map.of(),                 // inlined replaced by declaration
                Map.of("retries", 99L));  // c
        pc.declareInlinedToml(TOML_SRC);
        // c overrides b.
        assertEquals(99L, pc.get("retries"));
        assertSame(Layer.C, pc.resolve("retries").layer());
        // a overrides b: write lands in the external (a) layer.
        pc.set("log_level", "debug");
        assertEquals("debug", pc.get("log_level"));
        assertSame(Layer.A, pc.resolve("log_level").layer());
        // Untouched inlined value still resolves from b.
        assertEquals(0.5, pc.get("ratio"));
        assertSame(Layer.B, pc.resolve("ratio").layer());
    }

    @Test
    void inlineLayerRejectsBuildToolchainKey() {
        ProductConfig pc = new ProductConfig();
        assertThrows(BuildConfigKeyError.class,
                () -> pc.declareInlinedToml(
                        Map.of("build", Map.of("language", "java"))));
    }

    // -- PureInternalDevPhaseOnly ---------------------------------------

    @Test
    void declaredInternalIsDevSearchableByNameAndTag() {
        ProductConfig pc = new ProductConfig();
        Object value = pc.declareInternal("MAX_BUF", 4096L,
                List.of("perf", "memory"));
        // The call returns the plain value to bind as a constant.
        assertEquals(4096L, value);
        // Dev-phase: present in the side registry, tag-searchable there.
        assertTrue(pc.devInternal().names().contains("MAX_BUF"));
        assertEquals(List.of("MAX_BUF"),
                pc.devInternal().search(List.of("perf")));
        assertEquals(List.of("MAX_BUF"),
                pc.devInternal().search(List.of("perf", "memory")));
        assertEquals(List.of(),
                pc.devInternal().search(List.of("unrelated")));
        assertEquals(4096L, pc.devInternal().get("MAX_BUF").value());
    }

    @Test
    void internalAbsentFromEveryRuntimeSurface() {
        ProductConfig pc = new ProductConfig(
                Map.of("public.k", 1L), Map.of());
        pc.declareInternal("SECRET_TUNING", 7L, List.of("internal"));
        // Not in keys / query / queryResolved / resolveAll.
        assertFalse(pc.keys().contains("SECRET_TUNING"));
        assertFalse(pc.query().contains("SECRET_TUNING"));
        assertFalse(pc.query(999).contains("SECRET_TUNING"));
        assertFalse(pc.store().resolveAll()
                .containsKey("SECRET_TUNING"));
        assertFalse(pc.queryResolved(999)
                .containsKey("SECRET_TUNING"));
        assertThrows(NoSuchElementException.class,
                () -> pc.get("SECRET_TUNING"));
    }

    @Test
    void promotedValueIsAPlainConstantNoConfigReference() throws Exception {
        ProductConfig pc = new ProductConfig();
        Object v = pc.declareInternal("RATE", 0.25);
        // Identity-equivalent to a hand-written constant: the returned object
        // IS the value passed in, a bare Double, with no wrapper carrying a
        // config-system back-reference.
        assertSame(Double.class, v.getClass());
        assertEquals(0.25, v);
        // The runtime store object holds no reference to the d registry.
        ConfigStore store = pc.store();
        for (Field f : ConfigStore.class.getDeclaredFields()) {
            f.setAccessible(true);
            Object held = f.get(store);
            assertFalse(held instanceof DevInternalRegistry,
                    "store field " + f.getName()
                            + " must not hold the d registry");
        }
    }

    @Test
    void layerDStaysOutOfRuntimeMerge() {
        // The model already excludes Layer.D from the merge order; this pins
        // that contract so a d band can never be merged at runtime.
        assertFalse(LayeredConfig.RUNTIME_MERGE_ORDER.contains(Layer.D));
        LayeredConfig cfg = new LayeredConfig(
                Map.of("k", 1L), Map.of(), Map.of());
        assertThrows(IllegalStateException.class,
                () -> cfg.layerMap(Layer.D));
    }

    @Test
    void internalValueStillHonoursStdlibContract() {
        ProductConfig pc = new ProductConfig();
        assertThrows(UnbridgedValueError.class,
                () -> pc.declareInternal("WHEN",
                        LocalDate.of(2026, 5, 16)));
    }

    @Test
    void devRegistryIsDisjointFromStoreType() {
        // The registry is a free-standing type; ConfigStore neither
        // constructs nor stores one — the structural separation that makes
        // "no runtime presence" true by construction.
        DevInternalRegistry reg = new DevInternalRegistry();
        reg.declare("X", 1L, List.of("t"));
        ConfigStore store = new ConfigStore(
                new LayeredConfig(Map.of("X", 2L), Map.of(), Map.of()));
        // Same name in both is a coincidence, not a link.
        assertEquals(2L, store.get("X"));
        assertEquals(1L, reg.get("X").value());
    }
}
