// topo-profile-jvm-jfr — bridge from `.jfr` binary to the
// NDJSON wire format consumed by topo-core's JfrNdjsonConverter.
//
// CLI: java -jar topo-profile-jvm-jfr.jar <input.jfr>
//
// stdout receives one NDJSON object per event of interest. Event shape
// (mirrors topo-lang-java/topo-profile/test/fixtures/jfr_sample.ndjson):
//
//   {"event_type":"<class>","ts_ns":<int>,
//    "thread":{"id":<int>,"name":"<str>"},
//    "stack":[{"class":"<str>","method":"<str>","line":<int>}],
//    "duration_ns":<int>}
//
// Frame order is root → leaf (matches the JFR API default; JfrNdjsonConverter
// preserves this order). Events without a stack trace are skipped (the C++
// converter treats an empty stack as a parse error, so emitting one would
// poison the stream). Topo-prefixed event classes (`topo.Stage`,
// `topo.Pipeline`, `topo.Parallel`, `topo.Arena`) are passed through if
// present — they appear in the recording iff ObservabilityPass injected them.
//
// Exit codes:
//   0   success (zero or more events emitted)
//   1   usage error (missing arg, etc.)
//   2   read error (file missing, invalid `.jfr`, IO failure)

package dev.topo.profile.jfr;

import jdk.jfr.ValueDescriptor;
import jdk.jfr.consumer.RecordedEvent;
import jdk.jfr.consumer.RecordedFrame;
import jdk.jfr.consumer.RecordedMethod;
import jdk.jfr.consumer.RecordedStackTrace;
import jdk.jfr.consumer.RecordedThread;
import jdk.jfr.consumer.RecordingFile;

import java.io.PrintStream;
import java.nio.file.Files;
import java.nio.file.Path;
import java.nio.file.Paths;
import java.time.Duration;
import java.time.Instant;
import java.util.Set;

public final class Main {

    private static final int EXIT_OK = 0;
    private static final int EXIT_USAGE = 1;
    private static final int EXIT_READ = 2;

    // Initial event set: the three JFR built-ins that map onto sampling
    // semantics plus any `topo.*` event injected by ObservabilityPass. The
    // built-ins use the canonical JFR class names; the prefix check is a
    // cheap pass-through for forward compatibility.
    private static final Set<String> BUILTIN_EVENTS = Set.of(
        "jdk.ExecutionSample",
        "jdk.NativeMethodSample",
        "jdk.JavaMonitorEnter"
    );
    private static final String TOPO_PREFIX = "topo.";
    // Per-Pass profile events injected by AdaptivePass /
    // ArenaPass / ParallelPass / PipelinePass via dev.topo.PassEvents.
    // They are @StackTrace(false), so the normal stack-required render
    // path drops them; this prefix selects the stackless render path that
    // forwards their custom fields under a "fields" object for
    // JfrNdjsonConverter to route into pass_events.<PassName>[].
    private static final String TOPO_PASS_PREFIX = "topo.pass.";

    public static void main(String[] argv) {
        if (argv.length != 1) {
            System.err.println("Usage: java -jar topo-profile-jvm-jfr.jar <input.jfr>");
            System.exit(EXIT_USAGE);
        }
        Path input = Paths.get(argv[0]);
        if (!Files.isReadable(input)) {
            System.err.println("topo-profile-jvm-jfr: cannot read '" + input + "'");
            System.exit(EXIT_READ);
        }

        // Force ASCII-only output via a deliberately simple JSON serialiser so
        // the bridge can't sneak non-UTF-8 bytes past the consumer. PrintStream
        // is line-buffered on stdout in JVMs running with a terminal; the
        // explicit autoflush keeps the contract intact when stdout is a pipe
        // (which is exactly how topo-profile spawns us).
        PrintStream out = new PrintStream(System.out, true);

        try (RecordingFile rf = new RecordingFile(input)) {
            while (rf.hasMoreEvents()) {
                RecordedEvent evt = rf.readEvent();
                String name = evt.getEventType().getName();
                if (!isInteresting(name)) continue;
                String line = name.startsWith(TOPO_PASS_PREFIX)
                        ? renderPassEvent(evt, name)
                        : renderEvent(evt, name);
                if (line == null) continue;
                out.println(line);
            }
        } catch (Exception e) {
            System.err.println("topo-profile-jvm-jfr: read error: " + e.getMessage());
            System.exit(EXIT_READ);
        }
        System.exit(EXIT_OK);
    }

    private static boolean isInteresting(String eventName) {
        return BUILTIN_EVENTS.contains(eventName) || eventName.startsWith(TOPO_PREFIX);
    }

    // Build a single NDJSON object for the event, or return null if the event
    // has no stack trace (the downstream C++ converter rejects empty stacks).
    private static String renderEvent(RecordedEvent evt, String eventType) {
        RecordedStackTrace st = evt.getStackTrace();
        if (st == null) return null;
        var frames = st.getFrames();
        if (frames == null || frames.isEmpty()) return null;

        long tsNs = instantToEpochNanos(evt.getStartTime());
        long durationNs = 0;
        Duration d = evt.getDuration();
        if (d != null) durationNs = d.toNanos();

        StringBuilder sb = new StringBuilder(256);
        sb.append('{');
        appendKV(sb, "event_type", eventType);
        sb.append(',');
        sb.append("\"ts_ns\":").append(tsNs);
        sb.append(',');
        appendThread(sb, evt.getThread());
        sb.append(',');
        appendStack(sb, frames);
        sb.append(',');
        sb.append("\"duration_ns\":").append(durationNs);
        sb.append('}');
        return sb.toString();
    }

    // Render a per-Pass profile event. These are
    // @StackTrace(false), so unlike renderEvent we never require a stack;
    // instead we forward the event's declared scalar fields verbatim under
    // a "fields" object. JfrNdjsonConverter keys off `event_type` starting
    // with `topo.pass.` and routes {ts_ns, thread, fields} into
    // pass_events.<PassName>[], leaving the existing jdk.ExecutionSample
    // sampling path untouched.
    private static String renderPassEvent(RecordedEvent evt, String eventType) {
        long tsNs = instantToEpochNanos(evt.getStartTime());

        StringBuilder sb = new StringBuilder(256);
        sb.append('{');
        appendKV(sb, "event_type", eventType);
        sb.append(',');
        sb.append("\"ts_ns\":").append(tsNs);
        sb.append(',');
        appendThread(sb, evt.getThread());
        sb.append(',');
        sb.append("\"fields\":{");
        int written = 0;
        for (ValueDescriptor vd : evt.getEventType().getFields()) {
            String fname = vd.getName();
            // Skip JFR's synthetic/internal fields — only forward the
            // fields the user declared on the dev.topo.PassEvents class.
            if (fname.startsWith("jdk.") || "startTime".equals(fname)
                    || "duration".equals(fname) || "eventThread".equals(fname)
                    || "stackTrace".equals(fname)) {
                continue;
            }
            if (!evt.hasField(fname)) continue;
            Object v = evt.getValue(fname);
            if (written > 0) sb.append(',');
            ++written;
            sb.append('"').append(fname).append("\":");
            if (v == null) {
                sb.append("null");
            } else if (v instanceof Number || v instanceof Boolean) {
                sb.append(v.toString());
            } else {
                appendJsonString(sb, v.toString());
            }
        }
        sb.append('}');
        sb.append('}');
        return sb.toString();
    }

    private static long instantToEpochNanos(Instant ts) {
        if (ts == null) return 0L;
        // toEpochMilli would lose sub-millisecond resolution; multiply seconds
        // and add nano-of-second directly. Within the long range for ~292
        // years either side of the epoch — well past any realistic recording.
        return Math.addExact(
            Math.multiplyExact(ts.getEpochSecond(), 1_000_000_000L),
            (long) ts.getNano());
    }

    private static void appendThread(StringBuilder sb, RecordedThread th) {
        sb.append("\"thread\":{");
        long id = 0L;
        String name = "";
        if (th != null) {
            id = th.getJavaThreadId();
            name = th.getJavaName();
            if (name == null) name = th.getOSName();
            if (name == null) name = "";
        }
        sb.append("\"id\":").append(id);
        sb.append(',');
        appendKV(sb, "name", name);
        sb.append('}');
    }

    private static void appendStack(StringBuilder sb, java.util.List<RecordedFrame> frames) {
        sb.append("\"stack\":[");
        // RecordingFile returns frames leaf → root. Reverse here so the wire
        // shape is root → leaf, matching the existing NDJSON fixture and the
        // JfrNdjsonConverter contract documented in JfrNdjsonConverter.h.
        for (int i = frames.size() - 1, written = 0; i >= 0; --i, ++written) {
            RecordedFrame f = frames.get(i);
            RecordedMethod m = f.getMethod();
            if (m == null) continue;
            String cls = (m.getType() != null) ? m.getType().getName() : "";
            String mth = m.getName();
            if (cls == null) cls = "";
            if (mth == null) mth = "";
            int line = f.getLineNumber();
            if (written > 0) sb.append(',');
            sb.append('{');
            appendKV(sb, "class", cls);
            sb.append(',');
            appendKV(sb, "method", mth);
            sb.append(',');
            sb.append("\"line\":").append(line);
            sb.append('}');
        }
        sb.append(']');
    }

    private static void appendKV(StringBuilder sb, String key, String value) {
        sb.append('"').append(key).append("\":");
        appendJsonString(sb, value);
    }

    // Minimal JSON-string encoder. Handles the escapes RFC 8259 requires
    // (`"`, `\`, control chars). JFR class / method / thread names cannot
    // legally contain non-BMP characters or unpaired surrogates in any
    // workload we ship; we still emit a 4-hex-digit escape for ASCII < 0x20
    // so the output stays line-safe even if a thread name carries a newline.
    private static void appendJsonString(StringBuilder sb, String s) {
        sb.append('"');
        for (int i = 0; i < s.length(); ++i) {
            char c = s.charAt(i);
            switch (c) {
                case '"':  sb.append("\\\""); break;
                case '\\': sb.append("\\\\"); break;
                case '\n': sb.append("\\n"); break;
                case '\r': sb.append("\\r"); break;
                case '\t': sb.append("\\t"); break;
                case '\b': sb.append("\\b"); break;
                case '\f': sb.append("\\f"); break;
                default:
                    if (c < 0x20) {
                        sb.append(String.format("\\u%04x", (int) c));
                    } else {
                        sb.append(c);
                    }
            }
        }
        sb.append('"');
    }

    private Main() {}
}
