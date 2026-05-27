package dev.topo;

import java.lang.reflect.Field;
import java.util.Arrays;
import java.util.function.Supplier;

/**
 * Generic container for Topo-managed arrays.
 *
 * <p>Serves as the primary AoS container that the DataLayoutPass can
 * transform into SoA column storage at the bytecode level.</p>
 */
public class Array<T> {
    private final Object[] data;
    private final int size;
    private ColumnarView cachedView;

    @SuppressWarnings("unchecked")
    public Array(int size, Supplier<T> factory) {
        this.size = size;
        this.data = new Object[size];
        for (int i = 0; i < size; i++) data[i] = factory.get();
    }

    private Array(Object[] data) {
        this.data = data;
        this.size = data.length;
    }

    @SuppressWarnings("unchecked")
    public T get(int index) { return (T) data[index]; }

    public void set(int index, T value) {
        data[index] = value;
        cachedView = null;
    }

    public int size() { return size; }

    /**
     * Direct access to the underlying data array.
     * Used by TypeNarrowingPass to replace virtual get() calls with direct array access.
     * Callers are responsible for type safety — the CHECKCAST is preserved as a type guard.
     */
    public Object[] rawData() { return data; }

    @SuppressWarnings("unchecked")
    public ColumnarView getColumnarView(String elementClass, String[] fieldNames,
                                        String[] fieldDescs) {
        if (cachedView != null && cachedView.matches(fieldNames)) {
            return cachedView;
        }
        cachedView = ColumnarView.materialize(data, size, elementClass, fieldNames, fieldDescs);
        return cachedView;
    }

    @SafeVarargs
    public static <T> Array<T> of(T... elements) {
        return new Array<>(elements.clone());
    }

    public static <T> Array<T> create(int size, Supplier<T> factory) {
        return new Array<>(size, factory);
    }

    /**
     * Cached columnar (SoA) view over the array's data.  Materialised once
     * by {@link #getColumnarView} and reused until the backing data changes.
     */
    public static class ColumnarView {
        public final String[] fieldNames;
        public final Object[] columns;

        private ColumnarView(String[] fieldNames, Object[] columns) {
            this.fieldNames = fieldNames;
            this.columns = columns;
        }

        public boolean matches(String[] names) {
            return Arrays.equals(this.fieldNames, names);
        }

        public float[] getFloatColumn(int index) { return (float[]) columns[index]; }
        public int[] getIntColumn(int index) { return (int[]) columns[index]; }
        public double[] getDoubleColumn(int index) { return (double[]) columns[index]; }
        public long[] getLongColumn(int index) { return (long[]) columns[index]; }

        public static ColumnarView materialize(Object[] data, int size,
                                               String elementClass,
                                               String[] fieldNames,
                                               String[] fieldDescs) {
            try {
                Class<?> clazz = Class.forName(elementClass.replace('/', '.'));
                Object[] columns = new Object[fieldNames.length];

                Field[] fields = new Field[fieldNames.length];
                for (int f = 0; f < fieldNames.length; f++) {
                    fields[f] = clazz.getField(fieldNames[f]);
                    fields[f].setAccessible(true);
                    columns[f] = allocateColumn(fieldDescs[f], size);
                }

                for (int i = 0; i < size; i++) {
                    Object element = data[i];
                    for (int f = 0; f < fieldNames.length; f++) {
                        copyFieldToColumn(fields[f], element, columns[f], i, fieldDescs[f]);
                    }
                }

                return new ColumnarView(fieldNames, columns);
            } catch (ReflectiveOperationException e) {
                throw new RuntimeException("Failed to materialize columnar view for "
                        + elementClass, e);
            }
        }

        private static Object allocateColumn(String desc, int size) {
            return switch (desc) {
                case "F" -> new float[size];
                case "D" -> new double[size];
                case "I" -> new int[size];
                case "J" -> new long[size];
                case "B" -> new byte[size];
                case "Z" -> new boolean[size];
                case "S" -> new short[size];
                case "C" -> new char[size];
                default -> throw new IllegalArgumentException("Unsupported descriptor: " + desc);
            };
        }

        private static void copyFieldToColumn(Field field, Object element,
                                              Object column, int idx,
                                              String desc) throws IllegalAccessException {
            switch (desc) {
                case "F" -> ((float[]) column)[idx] = field.getFloat(element);
                case "D" -> ((double[]) column)[idx] = field.getDouble(element);
                case "I" -> ((int[]) column)[idx] = field.getInt(element);
                case "J" -> ((long[]) column)[idx] = field.getLong(element);
                case "B" -> ((byte[]) column)[idx] = field.getByte(element);
                case "Z" -> ((boolean[]) column)[idx] = field.getBoolean(element);
                case "S" -> ((short[]) column)[idx] = field.getShort(element);
                case "C" -> ((char[]) column)[idx] = field.getChar(element);
                default -> throw new IllegalArgumentException("Unsupported descriptor: " + desc);
            }
        }
    }
}
