package pipeline;

/**
 * Pipeline -- implements pipeline.topo: runPipeline DAG and
 * priority-differentiated operations.
 */
public class Pipeline {

    // -- Pipeline entry (public, critical priority) -------------------

    public static int runPipeline(int sourceId) {
        int raw = ingest(sourceId);
        int validated = validate(raw);
        int transformed = transform(validated);
        int indexed = buildIndex(validated);
        int stored = store(transformed, indexed);
        return notify(stored);
    }

    // -- Pipeline stage functions (protected in .topo) ----------------

    static int ingest(int sourceId) {
        System.out.println("ingest: reading from source " + sourceId);
        return sourceId * 100 + 42;
    }

    static int validate(int rawData) {
        System.out.println("validate: checking data " + rawData);
        return rawData <= 0 ? 0 : rawData;
    }

    static int transform(int validated) {
        System.out.println("transform: processing " + validated);
        return validated * 2 + 1;
    }

    static int buildIndex(int validated) {
        System.out.println("buildIndex: indexing " + validated);
        return validated % 1000;
    }

    static int store(int transformed, int indexed) {
        System.out.println("store: saving transformed=" + transformed
                + " indexed=" + indexed);
        return transformed + indexed;
    }

    static int notify(int storeResult) {
        System.out.println("notify: pipeline complete, result=" + storeResult);
        return storeResult;
    }

    // -- Priority-differentiated operations (public) ------------------

    public static void flushBuffer(int bufferId) {
        System.out.println("flushBuffer: flushing buffer " + bufferId
                + " [critical]");
    }

    public static void compactStorage() {
        System.out.println("compactStorage: reclaiming space [low priority]");
    }

    public static void mergeIndices() {
        System.out.println("mergeIndices: consolidating index segments"
                + " [low priority]");
    }

    public static void collectMetrics() {
        System.out.println("collectMetrics: gathering stats [background]");
    }

    public static void archiveProcessed(int batchId) {
        System.out.println("archiveProcessed: archiving batch " + batchId
                + " [background]");
    }

    // -- Private helpers ----------------------------------------------

    private static int parseRaw(int data) {
        return data & 0x7FFFFFFF;
    }

    private static int applySchema(int parsed, Schema schema) {
        return parsed + schema.id();
    }

    private static int buildSegment(int data, int segmentSize) {
        return data / segmentSize;
    }
}
