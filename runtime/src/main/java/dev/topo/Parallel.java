package dev.topo;

import java.util.ArrayList;
import java.util.List;
import java.util.concurrent.*;
import java.util.function.Supplier;

/**
 * Parallel execution primitives for Topo stage parallelism.
 * Uses Java 21 Virtual Threads for lightweight task spawning.
 */
public final class Parallel {
    private static final ExecutorService VIRTUAL_EXECUTOR =
        Executors.newVirtualThreadPerTaskExecutor();

    private Parallel() {}

    /**
     * Spawn a task for parallel execution. Returns a Future that can be awaited.
     */
    public static <T> Future<T> spawn(Supplier<T> task) {
        return VIRTUAL_EXECUTOR.submit(task::get);
    }

    /**
     * Spawn a void task for parallel execution.
     */
    public static Future<Void> spawn(Runnable task) {
        return VIRTUAL_EXECUTOR.submit(() -> { task.run(); return null; });
    }

    /**
     * Await all futures and return their results.
     * Throws ExecutionException if any task fails.
     */
    @SafeVarargs
    public static <T> List<T> awaitAll(Future<T>... futures) throws InterruptedException, ExecutionException {
        var results = new ArrayList<T>(futures.length);
        for (var future : futures) {
            results.add(future.get());
        }
        return results;
    }

    /**
     * Await all futures (void tasks).
     */
    public static void awaitAllVoid(Future<?>... futures) throws InterruptedException, ExecutionException {
        for (var future : futures) {
            future.get();
        }
    }

    /**
     * Execute tasks in parallel using a ForkJoinPool with the specified parallelism.
     * Useful when a bounded thread pool is preferred over virtual threads.
     */
    public static <T> List<T> executeAll(int parallelism, List<Supplier<T>> tasks)
            throws InterruptedException, ExecutionException {
        var pool = new ForkJoinPool(parallelism);
        try {
            var futures = new ArrayList<ForkJoinTask<T>>(tasks.size());
            for (var task : tasks) {
                futures.add(pool.submit(task::get));
            }
            var results = new ArrayList<T>(tasks.size());
            for (var f : futures) {
                results.add(f.get());
            }
            return results;
        } finally {
            pool.shutdown();
        }
    }

    /**
     * Sample the cost of a computation in nanoseconds.
     * Used by the adaptive dispatch system to decide whether to parallelize.
     */
    public static long costSample(Runnable task) {
        long start = System.nanoTime();
        task.run();
        return System.nanoTime() - start;
    }
}
