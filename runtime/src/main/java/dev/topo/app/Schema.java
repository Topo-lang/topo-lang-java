package dev.topo.app;

import java.util.ArrayList;
import java.util.List;

/**
 * Captures a handler's In/Out stdlib types explicitly.
 *
 * <p>The Python projection reads In/Out from {@code __annotations__}; Java
 * type erasure removes the generic shape of a {@code record}, so the Java
 * idiom is to <em>state</em> the type at registration the same way the config
 * port states a value's bridge type rather than inferring it after the fact.
 * Scalars still come from the parameter/return {@link Class} via {@link
 * #scalarOf}; a multi-field structure is built with {@link #record} so field
 * order and naming are explicit and stable — the direct analogue of the
 * Python projection's {@code Record[("id", int), ("amount", float)]}.
 *
 * <p>The Java-class to stdlib-scalar correspondence is exactly the one the
 * config port's {@code ConfigModel.stdlibTypeOf} established (boolean→bool,
 * integral→i64, floating→f64, String→string); reusing it keeps one schema
 * vocabulary across the config boundary and the handler In/Out boundary
 * instead of inventing a parallel type system.
 */
public final class Schema {

    private Schema() {
    }

    /** Build an ordered {@code record<...>} TypeRef from named fields. */
    public static TypeRef record(TypeRef.Field... fields) {
        if (fields == null || fields.length == 0) {
            throw new IllegalArgumentException(
                    "record needs at least one ('name', type) field");
        }
        List<TypeRef.Field> list = new ArrayList<>(fields.length);
        for (TypeRef.Field f : fields) {
            list.add(f);
        }
        return TypeRef.record(list);
    }

    /** A single named record field whose type is a stdlib scalar class. */
    public static TypeRef.Field field(String name, Class<?> scalarType) {
        return new TypeRef.Field(name, scalarOf(scalarType));
    }

    /** A single named record field whose type is itself a TypeRef. */
    public static TypeRef.Field field(String name, TypeRef type) {
        return new TypeRef.Field(name, type);
    }

    /**
     * The stdlib scalar TypeRef for a Java class. Mirrors the config port's
     * value→bridge-name mapping, projected onto the bare core-stdlib spelling
     * the handler/flow {@code .topo} surface uses.
     */
    public static TypeRef scalarOf(Class<?> cls) {
        if (cls == boolean.class || cls == Boolean.class) {
            return TypeRef.scalar("bool");
        }
        if (cls == byte.class || cls == Byte.class
                || cls == short.class || cls == Short.class
                || cls == int.class || cls == Integer.class
                || cls == long.class || cls == Long.class) {
            return TypeRef.scalar("i64");
        }
        if (cls == float.class || cls == Float.class
                || cls == double.class || cls == Double.class) {
            return TypeRef.scalar("f64");
        }
        if (cls == String.class || cls == CharSequence.class) {
            return TypeRef.scalar("string");
        }
        throw new IllegalArgumentException(
                "unsupported handler type '" + cls.getName() + "'; use a "
                        + "boolean/integral/floating/String scalar, or "
                        + "Schema.record(...) for a record");
    }
}
