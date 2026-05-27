package pipeline;

/**
 * DataSource -- base type for reading data into batches.
 * Implements types.topo DataSource type.
 */
public class DataSource {
    private long totalBytes;

    public DataSource() {
        this.totalBytes = 0;
    }

    public int read(Batch batch) {
        Record r = new Record(0);
        r.setField(0, 42);
        batch.add(r);
        totalBytes += 64;
        return 1;
    }

    public boolean hasMore() {
        return true;
    }

    public long bytesRead() {
        return totalBytes;
    }
}
