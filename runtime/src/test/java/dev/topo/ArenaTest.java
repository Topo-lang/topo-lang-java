package dev.topo;

import org.junit.jupiter.api.Test;
import java.lang.foreign.ValueLayout;
import static org.junit.jupiter.api.Assertions.*;

class ArenaTest {
    @Test
    void allocateAndClose() {
        try (var arena = new Arena()) {
            var segment = arena.allocate(1024);
            assertNotNull(segment);
            assertEquals(1024, segment.byteSize());
        }
    }

    @Test
    void allocateIntArray() {
        try (var arena = new Arena()) {
            var segment = arena.allocateIntArray(10);
            assertNotNull(segment);
            // Write and read back
            segment.setAtIndex(ValueLayout.JAVA_INT, 0, 42);
            assertEquals(42, segment.getAtIndex(ValueLayout.JAVA_INT, 0));
        }
    }

    @Test
    void arenaWithSizeHint() {
        try (var arena = new Arena(4096)) {
            var segment = arena.allocate(256);
            assertNotNull(segment);
        }
    }
}
