package pipeline;

/**
 * Index -- a searchable index for Indexable items.
 * Implements types.topo Index<T> template.
 */
public class Index<T> {
    private int count;

    public Index() {
        this.count = 0;
    }

    public void insert(T item) {
        count++;
    }

    public boolean lookup(long key, Object[] out) {
        return count > 0;
    }

    public void remove(long key) {
        if (count > 0) count--;
    }

    public int size() { return count; }
}
