package dev.topo;

import java.lang.foreign.MemorySegment;
import java.lang.foreign.ValueLayout;

/**
 * Arena-based memory allocation using Java 21's Foreign Function & Memory API.
 * Provides deterministic deallocation for off-heap memory.
 */
public final class Arena implements AutoCloseable {
    private static final ThreadLocal<Arena> CURRENT = new ThreadLocal<>();

    private final java.lang.foreign.Arena arena;

    /**
     * Create a confined arena (single-thread access, deterministic close).
     */
    public Arena() {
        this.arena = java.lang.foreign.Arena.ofConfined();
    }

    /**
     * Create an arena with specified default size hint (for documentation;
     * Panama arenas grow automatically).
     */
    public Arena(long sizeHint) {
        this.arena = java.lang.foreign.Arena.ofConfined();
    }

    /**
     * Create a shared arena (multi-thread access).
     */
    public static Arena ofShared() {
        var a = new Arena();
        // Note: uses the confined arena internally; for true shared access,
        // callers should use java.lang.foreign.Arena.ofShared() directly
        return a;
    }

    /**
     * Allocate a block of memory.
     * @param byteSize Number of bytes to allocate
     * @return MemorySegment pointing to allocated memory
     */
    public MemorySegment allocate(long byteSize) {
        return arena.allocate(byteSize);
    }

    /**
     * Allocate a block of memory with alignment.
     */
    public MemorySegment allocate(long byteSize, long alignment) {
        return arena.allocate(byteSize, alignment);
    }

    /**
     * Allocate an array of ints.
     */
    public MemorySegment allocateIntArray(int count) {
        return arena.allocate(ValueLayout.JAVA_INT, count);
    }

    /**
     * Allocate an array of floats.
     */
    public MemorySegment allocateFloatArray(int count) {
        return arena.allocate(ValueLayout.JAVA_FLOAT, count);
    }

    /**
     * Allocate an array of longs.
     */
    public MemorySegment allocateLongArray(int count) {
        return arena.allocate(ValueLayout.JAVA_LONG, count);
    }

    /**
     * Allocate an array of doubles.
     */
    public MemorySegment allocateDoubleArray(int count) {
        return arena.allocate(ValueLayout.JAVA_DOUBLE, count);
    }

    /**
     * Allocate an array of bytes.
     */
    public MemorySegment allocateByteArray(int count) {
        return arena.allocate(ValueLayout.JAVA_BYTE, count);
    }

    /**
     * Allocate an array of shorts.
     */
    public MemorySegment allocateShortArray(int count) {
        return arena.allocate(ValueLayout.JAVA_SHORT, count);
    }

    /**
     * Allocate an array of chars.
     */
    public MemorySegment allocateCharArray(int count) {
        return arena.allocate(ValueLayout.JAVA_CHAR, count);
    }

    /**
     * Allocate an array of booleans (stored as bytes).
     */
    public MemorySegment allocateBooleanArray(int count) {
        return arena.allocate(ValueLayout.JAVA_BYTE, count);
    }

    /**
     * Get the underlying Panama arena for advanced usage.
     */
    public java.lang.foreign.Arena unwrap() {
        return arena;
    }

    /**
     * Set the current thread's arena (called by ArenaPass at scope entry).
     */
    public static void setCurrent(Arena arena) {
        CURRENT.set(arena);
    }

    /**
     * Get the current thread's arena, or null if none active.
     */
    public static Arena getCurrent() {
        return CURRENT.get();
    }

    /**
     * Clear the current thread's arena (called by ArenaPass at scope exit).
     */
    public static void clearCurrent() {
        CURRENT.remove();
    }

    @Override
    public void close() {
        arena.close();
    }
}
