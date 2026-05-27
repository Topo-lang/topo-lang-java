package dev.topo;

import java.lang.annotation.*;

/**
 * Marks a method as a Topo pipeline entry point.
 * This is a marker annotation — the .topo file is the source of truth.
 */
@Retention(RetentionPolicy.RUNTIME)
@Target(ElementType.METHOD)
public @interface TopoPipeline {
}
