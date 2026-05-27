package dev.topo.app;

import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertFalse;
import static org.junit.jupiter.api.Assertions.assertNotNull;
import static org.junit.jupiter.api.Assertions.assertNull;
import static org.junit.jupiter.api.Assertions.assertTrue;

import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.nio.file.Path;
import java.util.List;
import java.util.Map;
import org.junit.jupiter.api.DisplayName;
import org.junit.jupiter.api.Nested;
import org.junit.jupiter.api.Test;

/**
 * End-to-end acceptance for the topo-app Java projection, mirroring the
 * Python vertical slice (skeleton / emit / round-trip / zero-declaration
 * check / config entry).
 *
 * <p>Requires the freshly-built toolchain ({@code topo}, {@code topo-check});
 * resolution is by {@link Toolchain} (env {@code TOPO_BIN_DIR} or the
 * {@code build/} tree of this checkout — never the stale {@code build-no-llvm}).
 *
 * <p>An ordered two-field record reused across cases — the Java analogue of
 * the Python slice's {@code Record[("id", int), ("amount", float)]}, built
 * from the same stdlib-scalar vocabulary the config port established.
 */
class VerticalSliceTest {

    private static final TypeRef ORDER_REC = Schema.record(
            Schema.field("id", long.class),
            Schema.field("amount", double.class));

    private static App buildApp() {
        return buildApp("orders");
    }

    private static App buildApp(String namespace) {
        App app = new App(namespace);
        // Registered handlers are returned unchanged and stay ordinary
        // callables (asserted in the skeleton tests below).
        app.handler("parse", TypeRef.scalar("string"), ORDER_REC,
                (String raw) -> raw.length());
        app.handler("validate", ORDER_REC, ORDER_REC, o -> o);
        app.handler("persist", ORDER_REC, TypeRef.scalar("bool"),
                o -> Boolean.TRUE);
        app.flow("order_pipeline", "parse", "validate", "persist");
        return app;
    }

    @Nested
    @DisplayName("Registration produces an enumerable graph")
    class Skeleton {

        @Test
        void graphIsEnumerable() {
            Graph g = buildApp().graph();
            assertEquals("orders", g.namespace());
            assertEquals(List.of("parse", "validate", "persist"),
                    g.handlers().stream().map(Handler::name).toList());
            assertEquals("string", g.handler("parse").in().topo());
            assertEquals("record<id: i64, amount: f64>",
                    g.handler("parse").out().topo());
            assertEquals("bool", g.handler("persist").out().topo());
            assertNotNull(g.flow());
            // parse->validate->persist->void
            assertEquals(3, g.flow().edges().size());
        }

        @Test
        void sourceHandlerHasNoInput() {
            App app = new App("src");
            app.handler("seed", TypeRef.scalar("i64"), () -> 0L);
            assertNull(app.graph().handler("seed").in());
        }

        @Test
        void handlerStaysIndependentlyCallable() {
            App app = new App("x");
            // No framework bootstrap is needed to invoke a registered unit.
            var doubler = app.handler(
                    "doubler", TypeRef.scalar("i64"), TypeRef.scalar("i64"),
                    (Long n) -> n * 2);
            assertEquals(42L, doubler.apply(21L));
            assertEquals(42L,
                    ((java.util.function.Function<Long, Long>)
                            app.callableFor("doubler")).apply(21L));
        }

        @Test
        void annotationRegistrationReflectsScalars() {
            // The @Register path recovers scalar In/Out by reflection, the
            // same discipline the Python projection applies to annotations;
            // records still take the explicit builder by design (erasure).
            App app = new App("ann");
            Handlers.scan(app, ScalarHolder.class);
            assertEquals("i64", app.graph().handler("step").in().topo());
            assertEquals("f64", app.graph().handler("step").out().topo());
            assertNull(app.graph().handler("origin").in());
        }
    }

    /** Holder for the annotation-scan skeleton case. */
    static final class ScalarHolder {
        @Handlers.Register
        static double step(long v) {
            return v + 0.5;
        }

        @Handlers.Register
        static long origin() {
            return 7L;
        }
    }

    @Nested
    @DisplayName("Emitted .topo parses under the fresh grammar")
    class Emit {

        @Test
        void emittedTopoParses() {
            App app = buildApp();
            String text = Config.of(app).emitTopo();
            assertTrue(text.contains(
                    "handler parse(string in) -> "
                            + "record<id: i64, amount: f64>;"),
                    text);
            assertTrue(text.contains("flow order_pipeline {"), text);
            // read() throws if the fresh topo rejects the source — parsing
            // it is itself the grammar-conformance proof.
            Graph g2 = Readback.read(text);
            assertEquals("orders", g2.namespace());
        }
    }

    @Nested
    @DisplayName("graph -> .topo -> graph' is semantically equivalent")
    class RoundTrip {

        @Test
        void semanticEquivalence() {
            App app = buildApp();
            Graph g1 = app.graph();
            Graph g2 = Config.of(app).roundtrip();
            assertTrue(g1.equivalentTo(g2),
                    g1.semanticKey() + " != " + g2.semanticKey());
        }

        @Test
        void handEditSurvivesReadback() {
            // The .topo is a view, not an opaque IR: reorder the two
            // interior edges by hand, read back, still equivalent.
            App app = buildApp();
            String text = Config.of(app).emitTopo();
            String edited = text.replace(
                    "      parse -> validate;\n"
                            + "      validate -> persist;",
                    "      validate -> persist;\n"
                            + "      parse -> validate;");
            assertTrue(app.graph().equivalentTo(Readback.read(edited)));
        }
    }

    @Nested
    @DisplayName("Zero hand-written .topo, the existing topo-check runs")
    class ZeroDeclarationCheck {

        // A handler must physically live in a class in Java; the .topo
        // names only the handler symbols, so the host class is hidden from
        // CompletenessCheck by this glob.
        private static final String HOLDER_GLOB = "*::App";

        private App scalarFlowApp(String namespace) {
            App app = new App(namespace);
            app.handler("parse", TypeRef.scalar("i64"),
                    TypeRef.scalar("i64"), (Long v) -> v + 1);
            app.handler("enrich", TypeRef.scalar("i64"),
                    TypeRef.scalar("i64"), (Long v) -> v * 2);
            app.handler("audit", TypeRef.scalar("i64"),
                    TypeRef.scalar("i64"), (Long v) -> v);
            app.handler("total", TypeRef.scalar("i64"),
                    TypeRef.scalar("f64"), (Long v) -> v + 0.5);
            app.flow("pipeline", "parse",
                    App.parallel("enrich", "audit"), "total");
            return app;
        }

        private Path writeSource(String body) throws Exception {
            Path dir = Files.createTempDirectory("topo-app-src");
            Path f = dir.resolve("App.java");
            Files.writeString(f, body, StandardCharsets.UTF_8);
            return f;
        }

        private static final String COMPLIANT_SRC = """
                package app;
                public final class App {
                    public static long parse(long raw) { return raw + 1; }
                    public static long enrich(long v) { return v * 2; }
                    public static long audit(long v) { return v; }
                    public static double total(long v) {
                        return (double) v + 0.5;
                    }
                }
                """;

        // `audit` is a same-stage parallel candidate (sibling of `enrich`
        // in parse -> {enrich || audit} -> total) and hides a static-field
        // write — the impurity core PurityCheck must flag, with zero
        // hand-written .topo.
        private static final String VIOLATING_SRC = """
                package app;
                public final class App {
                    private static long sideLog = 0;
                    public static long parse(long raw) { return raw + 1; }
                    public static long enrich(long v) { return v * 2; }
                    public static long audit(long v) { sideLog += v; return v; }
                    public static double total(long v) {
                        return (double) v + 0.5;
                    }
                }
                """;

        @Test
        void compliantFlowPasses() throws Exception {
            App app = scalarFlowApp("orders");
            Path src = writeSource(COMPLIANT_SRC);
            Check.Result r = Check.run(app, List.of(src), HOLDER_GLOB);
            assertTrue(r.passed(),
                    "compliant flow should pass topo-check\n"
                            + r.stdout() + r.stderr());
        }

        @Test
        void violatingFlowIsFlagged() throws Exception {
            // Parity with the Python / TypeScript / Rust slices: an impure
            // same-stage parallel handler in a flow is flagged by core
            // PurityCheck even though no .topo was hand-written.
            App app = scalarFlowApp("orders");
            Path src = writeSource(VIOLATING_SRC);
            Check.Result r = Check.run(app, List.of(src), HOLDER_GLOB);
            assertFalse(r.passed(),
                    "impure parallel handler should fail topo-check\n"
                            + r.stdout() + r.stderr());
            assertTrue(
                    r.stdout().contains("in parallel stage writes to global symbol"),
                    "expected the parallel-stage purity diagnostic\n"
                            + r.stdout());
        }

        @Test
        void unbridgedRecordFieldTypeIsRejectedEarly() {
            // Failing early on an unrepresentable type mirrors the Python
            // projection refusing an unsupported annotation before emission.
            assertFalse(canRegisterRecordWith(Object.class));
        }

        private boolean canRegisterRecordWith(Class<?> bad) {
            try {
                Schema.record(Schema.field("x", bad));
                return true;
            } catch (IllegalArgumentException expected) {
                return false;
            }
        }
    }

    @Nested
    @DisplayName("config(app): snapshot lists full graph; emit == emitter")
    class ConfigEntry {

        @Test
        void snapshotListsFullGraph() {
            Map<String, Object> snap = Config.of(buildApp()).snapshot();
            assertEquals("orders", snap.get("namespace"));
            assertEquals(3, ((List<?>) snap.get("handlers")).size());
            @SuppressWarnings("unchecked")
            Map<String, Object> flow =
                    (Map<String, Object>) snap.get("flow");
            assertEquals("order_pipeline", flow.get("name"));
            assertEquals(3, ((List<?>) flow.get("edges")).size());
        }

        @Test
        void configEmitEqualsEmitterOutput() {
            App app = buildApp();
            assertEquals(Emitter.emit(app.graph()),
                    Config.of(app).emitTopo());
        }
    }
}
