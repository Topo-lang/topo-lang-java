package pipeline;

/**
 * FileSource -- reads data from a file descriptor.
 * Extends DataSource (types.topo inheritance).
 */
public class FileSource extends DataSource {
    private int fd;
    private long offset;
    private long totalSize;

    public FileSource(int path) {
        super();
        this.fd = path;
        this.offset = 0;
        this.totalSize = 1024 * 1024;
    }

    public long fileSize() {
        return totalSize;
    }

    public double progress() {
        if (totalSize == 0) return 1.0;
        return (double) offset / (double) totalSize;
    }
}
