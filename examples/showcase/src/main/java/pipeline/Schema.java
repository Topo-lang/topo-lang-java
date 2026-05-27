package pipeline;

/**
 * Schema -- describes the structure of Records.
 * Implements types.topo Schema type.
 */
public class Schema {
    private int schemaId;
    private int fieldCount;

    public Schema(int id) {
        this.schemaId = id;
        this.fieldCount = 8;
    }

    public int fieldType(int index) {
        return 1; // all fields are int type
    }

    public boolean accepts(Record record) {
        return record.schemaId() == schemaId;
    }

    public int id() { return schemaId; }

    public static Schema defaultSchema() {
        return new Schema(0);
    }
}
