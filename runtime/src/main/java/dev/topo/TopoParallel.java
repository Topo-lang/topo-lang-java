package dev.topo;

import java.lang.annotation.*;

/**
 * Marks a method as a candidate for parallel execution.
 * This is a marker annotation — the .topo file is the source of truth.
 */
@Retention(RetentionPolicy.RUNTIME)
@Target(ElementType.METHOD)
public @interface TopoParallel {
    /** Minimum number of tasks to trigger parallelization. */
    int minTasks() default 2;
}
