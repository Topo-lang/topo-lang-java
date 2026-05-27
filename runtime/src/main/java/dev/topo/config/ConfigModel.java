package dev.topo.config;

import java.time.temporal.Temporal;
import java.util.Date;
import java.util.List;
import java.util.Map;
import java.util.Set;

/**
 * The language-agnostic core of the product runtime configuration: the
 * boundary name, the build-toolchain section list and its guard, and the
 * stdlib value-type contract.
 *
 * <p>This holds what the reference {@code _config_model.py} keeps as
 * module-level constants and free functions. It is the <em>core</em>: it owns
 * semantics, not wiring. It deliberately has no TOML parser, no file I/O and no
 * host-only behaviour. A host bridge decodes its ecosystem's TOML into the
 * plain {@code Map}/{@code List}/scalar values this model consumes, and
 * projects the merged result into idiomatic host accessors.
 *
 * <p>Why the product config is a separate file from the build-time
 * {@code Topo.toml}: {@code Topo.toml} configures the toolchain build (host
 * language, sources, optimisation feature-modes, check policy — owned by
 * topo-build). This model configures the built product's runtime behaviour.
 * They live at different lifecycle layers and answer different questions, so
 * they are two files with no overlapping sections. A build-toolchain key has
 * exactly one home and putting it into the product config is a category error
 * the validation hook rejects, naming the file the key actually belongs to.
 */
public final class ConfigModel {

    private ConfigModel() {
    }

    /**
     * Fixed product runtime config filename for this proof of concept. Kept
     * here (not in a bridge) so every host agrees on the boundary name.
     */
    public static final String PRODUCT_CONFIG_FILENAME = "topo-app.toml";

    /**
     * The build toolchain owns Topo.toml; these are its section names. A key
     * whose first dotted segment is one of these belongs to the build config,
     * never to the product runtime config. Naming them here keeps the
     * non-overlap boundary a single explicit set rather than scattered string
     * checks.
     */
    public static final Set<String> BUILD_TOOLCHAIN_SECTIONS = Set.of(
            "topo",
            "build",
            "builder",
            "parallel",
            "adaptive",
            "optimize",
            "observability",
            "lifetime",
            "loop_parallel",
            "types",
            "completeness",
            "check",
            "test");

    /** First dotted segment of a config key ({@code a.b.c} -> {@code a}). */
    static String rootSection(String key) {
        int dot = key.indexOf('.');
        return dot < 0 ? key : key.substring(0, dot);
    }

    /**
     * Boundary guard: refuse a key that belongs in {@code Topo.toml}.
     *
     * <p>The product runtime config and the build-time {@code Topo.toml} share
     * no sections by design; accepting a build key here would create a second,
     * silently-ignored home for it. Rejecting loudly — and naming the file the
     * key actually belongs to — keeps the boundary honest.
     */
    public static void rejectIfBuildConfigKey(String key) {
        String section = rootSection(key);
        if (BUILD_TOOLCHAIN_SECTIONS.contains(section)) {
            throw new BuildConfigKeyError(
                    "'" + key + "' configures the build toolchain (section '["
                            + section + "]') and belongs in Topo.toml, not the "
                            + "product runtime config (" + PRODUCT_CONFIG_FILENAME
                            + "). The two files share no sections; set this in "
                            + "Topo.toml instead.");
        }
    }

    // --- Value-type contract --------------------------------------------
    //
    // A config value only enters the model if it has a stdlib bridge type, so
    // every value the running product reads has a known contract — the same
    // schema vocabulary the handler In/Out boundary uses. The mapping is
    // expressed in terms of decoded plain data (the shape every host bridge
    // normalises its TOML into), so the rule reads identically in any host.

    /**
     * The stdlib bridge spelling for a decoded value, or raise.
     *
     * <p>Aggregates (list/map) are validated element-wise so a datetime
     * smuggled inside an array or table is caught, not just a top-level one.
     * Date/time/datetime have no stdlib correspondence (the time_* family is
     * not yet bridged), so they are rejected rather than given an ad-hoc
     * contract.
     */
    public static String stdlibTypeOf(Object value) {
        // java.time.* and legacy java.util.Date stand in for TOML
        // date/time/datetime: no stdlib bridge type exists for them yet.
        if (value instanceof Temporal || value instanceof Date) {
            throw new UnbridgedValueError(
                    "value of type '" + typeName(value) + "' has no stdlib "
                            + "bridge type — TOML date/time maps to the "
                            + "not-yet-implemented time_* family (the "
                            + "time_*/uuid/decimal128 stdlib-bridging gap). "
                            + "Accepting it would store a value with no "
                            + "schema contract; use a bridged scalar "
                            + "instead.");
        }
        // bool before integer so the two distinct contracts never collide.
        if (value instanceof Boolean) {
            return "bool";
        }
        if (value instanceof Byte || value instanceof Short
                || value instanceof Integer || value instanceof Long) {
            return "int"; // TOML integer -> i64
        }
        if (value instanceof Float || value instanceof Double) {
            return "float"; // TOML float -> f64
        }
        if (value instanceof String) {
            return "str";
        }
        if (value instanceof List<?> list) {
            for (Object element : list) {
                stdlibTypeOf(element);
            }
            return "slice";
        }
        if (value instanceof Map<?, ?> map) {
            for (Object element : map.values()) {
                stdlibTypeOf(element);
            }
            return "record";
        }
        throw new UnbridgedValueError(
                "value of type '" + typeName(value) + "' has no stdlib bridge "
                        + "type (stdlib-bridging-types gap). Only string / "
                        + "integer / float / bool / array / table values have "
                        + "a schema contract; refusing to store an "
                        + "uncontracted value.");
    }

    /**
     * Type-gate a value about to be written under {@code key}. Re-raises the
     * underlying {@link UnbridgedValueError} with the offending key prepended
     * so a rejection always locates the problem.
     */
    public static void validateValue(String key, Object value) {
        try {
            stdlibTypeOf(value);
        } catch (UnbridgedValueError exc) {
            throw new UnbridgedValueError(
                    "config key '" + key + "': " + exc.getMessage());
        }
    }

    private static String typeName(Object value) {
        return value == null ? "null" : value.getClass().getSimpleName();
    }
}
