package dev.topo;

import java.lang.annotation.*;

/**
 * Marks a method as belonging to a specific Topo stage.
 * This is a marker annotation — the .topo file is the source of truth.
 */
@Retention(RetentionPolicy.RUNTIME)
@Target(ElementType.METHOD)
public @interface TopoStage {
    /** Stage name as declared in .topo file. */
    String name();

    /** Stage order (0-based). */
    int order() default 0;
}
