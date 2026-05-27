package pipeline;

/**
 * Record -- a row of data fields with a schema reference.
 * Implements types.topo Record type plus Validatable, Serializable, Indexable constraints.
 */
public class Record {
    private int schema;
    private int count;
    private int[] fields;

    public Record(int schemaId) {
        this.schema = schemaId;
        this.count = 0;
        this.fields = new int[16];
    }

    public int fieldCount() { return count; }

    public int getField(int index) {
        if (index < 0 || index >= count) return 0;
        return fields[index];
    }

    public void setField(int index, int value) {
        if (index >= 0 && index < fields.length) {
            fields[index] = value;
            if (index >= count) count = index + 1;
        }
    }

    public int schemaId() { return schema; }
    public boolean isEmpty() { return count == 0; }
}
