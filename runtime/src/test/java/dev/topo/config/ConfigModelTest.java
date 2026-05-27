package dev.topo.config;

import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertThrows;
import static org.junit.jupiter.api.Assertions.assertTrue;

import java.util.LinkedHashMap;
import java.util.List;
import java.util.Map;
import java.util.NoSuchElementException;

import org.junit.jupiter.api.Test;

/**
 * Acceptance for the layered product-config model: frozen a/b/c merge
 * precedence, per-value provenance, and the Topo.toml boundary guard. Mirrors
 * the reference {@code test_config_model.py} parity cases one for one.
 */
class ConfigModelTest {

    // A sample where each layer is the sole winner of at least one key, plus a
    // key all three set so precedence is unambiguous.
    private LayeredConfig sample() {
        Map<String, Object> inlined = new LinkedHashMap<>();
        inlined.put("log.level", "warn");   // only b -> b wins
        inlined.put("cache.size", 64L);     // b, overridden by a
        inlined.put("retry.count", 1L);     // b, overridden by a and c
        Map<String, Object> external = new LinkedHashMap<>();
        external.put("cache.size", 256L);   // a beats b
        external.put("retry.count", 3L);    // a beats b, lost to c
        external.put("feature.flag", true); // only a -> a wins
        Map<String, Object> injected = new LinkedHashMap<>();
        injected.put("retry.count", 9L);    // c beats a and b
        injected.put("tracing.enabled", false); // only c -> c wins
        return new LayeredConfig(inlined, external, injected);
    }

    @Test
    void eachKeyHasUniqueEffectiveValueAndProvenance() {
        Map<String, ResolvedValue> resolved = sample().resolveAll();

        assertEquals("warn", resolved.get("log.level").value());
        assertEquals(Layer.B, resolved.get("log.level").layer());

        assertEquals(256L, resolved.get("cache.size").value());
        assertEquals(Layer.A, resolved.get("cache.size").layer());

        assertEquals(true, resolved.get("feature.flag").value());
        assertEquals(Layer.A, resolved.get("feature.flag").layer());

        // Set by all three layers: c (most explicit) must win.
        assertEquals(9L, resolved.get("retry.count").value());
        assertEquals(Layer.C, resolved.get("retry.count").layer());

        assertEquals(false, resolved.get("tracing.enabled").value());
        assertEquals(Layer.C, resolved.get("tracing.enabled").layer());
    }

    @Test
    void keysEnumeratesEveryLayerOnceSorted() {
        assertEquals(
                List.of("cache.size", "feature.flag", "log.level",
                        "retry.count", "tracing.enabled"),
                sample().keys());
    }

    @Test
    void iterProvenanceTriples() {
        List<LayeredConfig.Provenance> triples =
                LayeredConfig.iterProvenance(sample().resolveAll());
        assertEquals(List.of(
                new LayeredConfig.Provenance("cache.size", 256L, Layer.A),
                new LayeredConfig.Provenance("feature.flag", true, Layer.A),
                new LayeredConfig.Provenance("log.level", "warn", Layer.B),
                new LayeredConfig.Provenance("retry.count", 9L, Layer.C),
                new LayeredConfig.Provenance("tracing.enabled", false,
                        Layer.C)),
                triples);
    }

    @Test
    void mergeLayersHelperMatches() {
        Map<String, ResolvedValue> resolved = LayeredConfig.mergeLayers(
                Map.of("x", 1L), Map.of("x", 2L), Map.of("x", 3L));
        assertEquals(3L, resolved.get("x").value());
        assertEquals(Layer.C, resolved.get("x").layer());
    }

    @Test
    void unknownKeyRaises() {
        assertThrows(NoSuchElementException.class,
                () -> sample().resolve("does.not.exist"));
    }

    @Test
    void dLayerIsNotARuntimeMergeLayer() {
        // d exists in the vocabulary but is promoted to code, never merged at
        // runtime — asking the model to read it as a layer is an explicit
        // construction error, not a silent empty result.
        assertThrows(IllegalStateException.class,
                () -> new LayeredConfig().layerMap(Layer.D));
    }

    @Test
    void buildSectionKeyRejectedAndPointsToTopoToml() {
        BuildConfigKeyError ex = assertThrows(BuildConfigKeyError.class,
                () -> ConfigModel.rejectIfBuildConfigKey("build.language"));
        assertTrue(ex.getMessage().contains("Topo.toml"));
        assertTrue(ex.getMessage()
                .contains(ConfigModel.PRODUCT_CONFIG_FILENAME));
    }

    @Test
    void featureModeSectionKeyRejected() {
        for (String key : List.of("parallel.mode",
                "adaptive.min_trigger_ns", "optimize.indirection",
                "check.jobs", "topo.root")) {
            assertThrows(BuildConfigKeyError.class,
                    () -> ConfigModel.rejectIfBuildConfigKey(key));
        }
    }

    @Test
    void buildKeyInALayerRejectedOnResolveAll() {
        LayeredConfig cfg = new LayeredConfig(Map.of(),
                Map.of("build.standard", "c++20"), Map.of());
        BuildConfigKeyError ex = assertThrows(BuildConfigKeyError.class,
                cfg::resolveAll);
        assertTrue(ex.getMessage().contains("Topo.toml"));
    }

    @Test
    void productKeyWithSimilarNameIsNotRejected() {
        // Only the exact build sections are off-limits; product keys that
        // merely look related are fine.
        ConfigModel.rejectIfBuildConfigKey("checkout.timeout_ms"); // not [check]
        ConfigModel.rejectIfBuildConfigKey("testing_endpoint.url"); // not [test]
        LayeredConfig cfg = new LayeredConfig(
                Map.of("checkout.timeout_ms", 5000L), Map.of(), Map.of());
        assertEquals(5000L, cfg.resolve("checkout.timeout_ms").value());
    }
}
