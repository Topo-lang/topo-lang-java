package pipeline;

import java.util.ArrayList;
import java.util.List;

/**
 * Batch -- a collection of Records with bounded capacity.
 * Implements types.topo Batch type.
 */
public class Batch {
    private int count;
    private int cap;
    private final List<Record> records;

    public Batch(int capacity) {
        this.count = 0;
        this.cap = capacity;
        this.records = new ArrayList<>(capacity);
    }

    public void add(Record record) {
        if (count < cap) {
            records.add(record);
            count++;
        }
    }

    public Record get(int index) {
        return records.get(index);
    }

    public int size() { return count; }
    public boolean isFull() { return count >= cap; }

    public void clear() {
        records.clear();
        count = 0;
    }
}
