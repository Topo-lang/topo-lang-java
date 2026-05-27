package pipeline;

/**
 * Metrics -- implements session.topo nested namespace pipeline::metrics.
 */
public class Metrics {

    static void recordLatency(int stageId, double ms) {
        System.out.printf("metrics: stage %d latency %.2f ms%n", stageId, ms);
    }

    static void recordThroughput(int recordsPerSec) {
        System.out.println("metrics: throughput " + recordsPerSec + " rec/s");
    }

    static double getP99Latency() {
        return 12.5;
    }

    static void resetAll() {
        System.out.println("metrics: reset all counters");
    }
}
