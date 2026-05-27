// Minimal Java span-emitter for the topo-profile demo.
//
// Mirrors topo-lang-cpp/topo-profile/test/fixtures/spans_demo.cpp: emits
// three NDJSON span records on stdout following libtopo-observe's wire
// shape, so topo-profile (which is host-agnostic — it only consumes the
// NDJSON contract) can re-emit them under the profile-trace schema with
// `backend: "jvm"`.
//
// Wire shape (one line per span, JSON pretty-print stripped):
//   {"name":"pipeline::demo::stageN","duration_ns":<ns>,
//    "thread_id":<u64>,"ts_ns":<ns since epoch>}
//
// Names follow the `pipeline::<name>::stage<N>` convention so the CLI's
// parseStagePipeline() recovers stage / pipeline fields. The fixture
// does not depend on any topo runtime — pure stdlib so it stays
// trivially compilable by javac without classpath setup.
public class SpansDemo {
    private static void busy(long us) {
        long deadlineNs = System.nanoTime() + us * 1000L;
        long acc = 0;
        while (System.nanoTime() < deadlineNs) {
            for (int i = 0; i < 1000; i++) {
                acc ^= i;
            }
        }
        // Prevent dead-code elimination.
        if (acc == -1L) System.err.println("unreachable");
    }

    private static void emitSpan(String name, long durationNs) {
        long tid = Thread.currentThread().getId();
        long tsNs = System.currentTimeMillis() * 1_000_000L;
        // Hand-rolled JSON to avoid pulling in a JSON library; key order
        // matches libtopo-observe so any future contract tests stay aligned.
        System.out.printf("{\"name\":\"%s\",\"duration_ns\":%d,"
                          + "\"thread_id\":%d,\"ts_ns\":%d}%n",
                          name, durationNs, tid, tsNs);
        System.out.flush();
    }

    private static long timedSpan(String name, long busyUs) {
        long startNs = System.nanoTime();
        busy(busyUs);
        long durationNs = System.nanoTime() - startNs;
        emitSpan(name, durationNs);
        return durationNs;
    }

    public static void main(String[] args) {
        timedSpan("pipeline::demo::stage0", 5000);
        timedSpan("pipeline::demo::stage1", 5000);
        timedSpan("pipeline::demo::stage2", 5000);
    }
}
