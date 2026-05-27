// JfrDemo records a short `.jfr` for end-to-end testing of
// the topo-profile-jvm-jfr bridge. The bridge translates `.jfr` events into
// the same NDJSON wire shape used by jfr_sample.ndjson (the checked-in
// fixture for the NDJSON-input path).
//
// CLI: java JfrDemo <output.jfr>
//
// Records `jdk.ExecutionSample` events at ~10 ms cadence for ~150 ms of busy
// compute. We keep this self-contained so the e2e test can produce its input
// at build time without depending on any external JFR sample (which would
// need to be platform-portable).

import jdk.jfr.Recording;
import jdk.jfr.consumer.RecordingFile;

import java.nio.file.Path;
import java.nio.file.Paths;
import java.time.Duration;

public final class JfrDemo {

    public static void main(String[] argv) throws Exception {
        if (argv.length != 1) {
            System.err.println("Usage: java JfrDemo <output.jfr>");
            System.exit(1);
        }
        Path out = Paths.get(argv[0]);

        Recording r = new Recording();
        // High-frequency sample period so a short workload reliably produces
        // multiple jdk.ExecutionSample events.
        r.enable("jdk.ExecutionSample").withPeriod(Duration.ofMillis(10));
        r.setDestination(out);
        r.start();

        // ~150 ms of compute spread across a few methods so the recording
        // captures a variety of stacks.
        long deadline = System.nanoTime() + Duration.ofMillis(150).toNanos();
        long acc = 0;
        while (System.nanoTime() < deadline) {
            acc += compute(acc);
        }
        // Force the JVM not to optimise the loop out entirely.
        if (acc == Long.MIN_VALUE) System.out.println("never");

        r.stop();
        r.close();
    }

    private static long compute(long seed) {
        long x = seed;
        for (int i = 0; i < 10_000; ++i) {
            x = mix(x, i);
        }
        return x;
    }

    private static long mix(long x, int i) {
        return (x ^ (x << 13)) + i * 0x9E3779B97F4A7C15L;
    }

    private JfrDemo() {}
}
