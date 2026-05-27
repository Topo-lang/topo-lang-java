package pipeline;

/**
 * Types -- implements types.topo utility declarations: constraint adapt functions,
 * ownership functions, comptime stubs, and private helpers.
 */
public class Types {

    // -- Constraint adaptation: Validatable for Record ----------------

    static boolean recordValidate(Record record) {
        return !record.isEmpty() && record.schemaId() >= 0;
    }

    static int recordErrorCount(Record record) {
        int errors = 0;
        for (int i = 0; i < record.fieldCount(); i++) {
            if (record.getField(i) < 0) errors++;
        }
        return errors;
    }

    // -- Constraint adaptation: Serializable for Record ---------------

    static long recordSerializedSize(Record record) {
        return 8L + (long) record.fieldCount() * 4;
    }

    static void recordSerialize(Record record, int dest) {
        System.out.println("serialize Record(schema=" + record.schemaId()
                + ", fields=" + record.fieldCount() + ") -> dest " + dest);
    }

    static boolean recordDeserialize(Record record, int src) {
        record.setField(0, 1);
        return true;
    }

    // -- Constraint adaptation: Indexable for Record ------------------

    static long recordIndexKey(Record record) {
        return (long) record.schemaId() * 100000 + record.getField(0);
    }

    static int recordCompareKey(Record a, Record b) {
        long ka = recordIndexKey(a);
        long kb = recordIndexKey(b);
        return Long.compare(ka, kb);
    }

    // -- Ownership functions (public in .topo) ------------------------

    public static void submitBatch(Batch batch) {
        System.out.println("submitted batch with " + batch.size() + " records");
    }

    public static int queryCache(Cache<Record> cache, long key) {
        Object[] out = new Object[1];
        if (cache.get(key, out)) return 0;
        return -1;
    }

    public static boolean checkSource(DataSource source) {
        return source != null && source.hasMore();
    }

    // -- Comptime stubs (public in .topo) -----------------------------

    public static void useCompactFormat() {
        System.out.println("using compact record format (32-bit)");
    }

    public static void useWideFormat() {
        System.out.println("using wide record format (64-bit)");
    }

    // -- Private helpers ----------------------------------------------

    private static void compactCache(int threshold) {
        System.out.println("compacting cache entries below threshold " + threshold);
    }

    private static void rebuildIndexSegment(int segmentId) {
        System.out.println("rebuilding index segment " + segmentId);
    }
}
