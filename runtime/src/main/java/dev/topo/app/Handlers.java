package dev.topo.app;

import java.lang.annotation.ElementType;
import java.lang.annotation.Retention;
import java.lang.annotation.RetentionPolicy;
import java.lang.annotation.Target;
import java.lang.reflect.Method;
import java.lang.reflect.Modifier;

/**
 * Annotation-driven registration for the scalar-typed case.
 *
 * <p>{@link Register} marks a {@code static} method as a handler. Scalars are
 * reflected from the method's parameter / return {@link Class} exactly like
 * the Python projection reflects them from annotations, so for scalar In/Out
 * the host writes no type wiring at all. A method with one parameter is a
 * normal handler; a no-parameter method is a legal source handler.
 *
 * <p>Records are <em>not</em> derived here: Java erases the generic shape of a
 * structured type, so a {@code record<...>} In/Out must be stated explicitly
 * with {@link Schema} via {@link App#handler}. This split is honest — the
 * annotation path covers exactly what reflection can recover soundly, and the
 * builder path covers what erasure makes unrecoverable, rather than guessing.
 * The annotated method stays an ordinary callable; scanning never rewrites it.
 */
public final class Handlers {

    private Handlers() {
    }

    @Retention(RetentionPolicy.RUNTIME)
    @Target(ElementType.METHOD)
    public @interface Register {
        /** Handler name; defaults to the method name when blank. */
        String value() default "";
    }

    /**
     * Register every {@code @Register}-annotated static method declared on
     * {@code holder} into {@code app}. Each method must take 0 or 1 scalar
     * parameter and return a scalar; the bound {@link java.lang.reflect.Method}
     * is invocable as-is, keeping the handler an ordinary callable.
     */
    public static void scan(App app, Class<?> holder) {
        for (Method m : holder.getDeclaredMethods()) {
            Register reg = m.getAnnotation(Register.class);
            if (reg == null) {
                continue;
            }
            if (!Modifier.isStatic(m.getModifiers())) {
                throw new IllegalArgumentException(
                        "@Register method '" + m.getName()
                                + "' must be static");
            }
            String name = reg.value().isBlank() ? m.getName() : reg.value();
            Class<?>[] params = m.getParameterTypes();
            if (params.length > 1) {
                throw new IllegalArgumentException(
                        "handler '" + name + "' has " + params.length
                                + " inputs; a handler is a pure Functor with "
                                + "at most one input — aggregate into a record");
            }
            TypeRef out = Schema.scalarOf(m.getReturnType());
            m.setAccessible(true);
            if (params.length == 0) {
                app.handler(name, out, () -> invoke(m));
            } else {
                TypeRef in = Schema.scalarOf(params[0]);
                app.handler(name, in, out, arg -> invoke(m, arg));
            }
        }
    }

    private static Object invoke(Method m, Object... args) {
        try {
            return m.invoke(null, args);
        } catch (ReflectiveOperationException e) {
            throw new IllegalStateException(
                    "handler '" + m.getName() + "' invocation failed", e);
        }
    }
}
