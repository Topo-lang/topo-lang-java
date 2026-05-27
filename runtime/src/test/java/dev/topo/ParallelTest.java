package dev.topo;

import org.junit.jupiter.api.Test;
import java.util.List;
import java.util.concurrent.ExecutionException;
import java.util.concurrent.atomic.AtomicInteger;
import java.util.function.Supplier;

import static org.junit.jupiter.api.Assertions.*;

class ParallelTest {
    @Test
    void spawnAndAwait() throws Exception {
        var f1 = Parallel.spawn(() -> 1);
        var f2 = Parallel.spawn(() -> 2);
        var f3 = Parallel.spawn(() -> 3);

        @SuppressWarnings("unchecked")
        var results = Parallel.awaitAll(f1, f2, f3);
        assertEquals(List.of(1, 2, 3), results);
    }

    @Test
    void spawnVoid() throws Exception {
        var counter = new AtomicInteger(0);
        var f1 = Parallel.spawn(() -> counter.incrementAndGet());
        var f2 = Parallel.spawn(() -> counter.incrementAndGet());

        Parallel.awaitAllVoid(f1, f2);
        assertEquals(2, counter.get());
    }

    @Test
    void executeAll() throws Exception {
        List<Supplier<Integer>> tasks = List.of(() -> 10, () -> 20, () -> 30);
        var results = Parallel.executeAll(2, tasks);
        assertEquals(List.of(10, 20, 30), results);
    }

    @Test
    void costSample() {
        long ns = Parallel.costSample(() -> {
            // Simulate some work
            long sum = 0;
            for (int i = 0; i < 1000; i++) sum += i;
        });
        assertTrue(ns > 0, "Cost sample should be positive");
    }
}
