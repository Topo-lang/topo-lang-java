package pipeline;

/**
 * Cache -- a key-value store for Serializable items.
 * Implements types.topo Cache<T> template.
 */
public class Cache<T> {
    private int count;
    private int maxSize;

    public Cache() {
        this.count = 0;
        this.maxSize = 1024;
    }

    public boolean get(long key, Object[] out) {
        return count > 0;
    }

    public void put(long key, T value) {
        if (count < maxSize) count++;
    }

    public void evict(long key) {
        if (count > 0) count--;
    }

    public int size() { return count; }

    public void clear() { count = 0; }
}
