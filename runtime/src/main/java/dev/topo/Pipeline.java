package dev.topo;

import java.util.concurrent.CompletableFuture;
import java.util.concurrent.Executor;
import java.util.concurrent.Executors;
import java.util.function.Function;
import java.util.function.Supplier;

/**
 * Pipeline execution using CompletableFuture chains.
 * Each stage is a transformation step that can execute asynchronously.
 */
public final class Pipeline<T> {
    private static final Executor DEFAULT_EXECUTOR =
        Executors.newVirtualThreadPerTaskExecutor();

    private final CompletableFuture<T> future;

    private Pipeline(CompletableFuture<T> future) {
        this.future = future;
    }

    /**
     * Create a pipeline starting with a supplier.
     */
    public static <T> Pipeline<T> start(Supplier<T> supplier) {
        return new Pipeline<>(CompletableFuture.supplyAsync(supplier, DEFAULT_EXECUTOR));
    }

    /**
     * Create a pipeline from an already-available value.
     */
    public static <T> Pipeline<T> of(T value) {
        return new Pipeline<>(CompletableFuture.completedFuture(value));
    }

    /**
     * Create a placeholder pipeline that will be filled later.
     * This maps to Topo's pipeline placeholder concept.
     */
    public static <T> Pipeline<T> placeholder() {
        return new Pipeline<>(new CompletableFuture<>());
    }

    /**
     * Add a stage to the pipeline that transforms the value.
     */
    public <U> Pipeline<U> stage(Function<T, U> transform) {
        return new Pipeline<>(future.thenApplyAsync(transform, DEFAULT_EXECUTOR));
    }

    /**
     * Add a stage with a custom executor.
     */
    public <U> Pipeline<U> stage(Function<T, U> transform, Executor executor) {
        return new Pipeline<>(future.thenApplyAsync(transform, executor));
    }

    /**
     * Execute the pipeline and return the result, blocking until complete.
     */
    public T execute() {
        return future.join();
    }

    /**
     * Get the underlying CompletableFuture for advanced composition.
     */
    public CompletableFuture<T> toFuture() {
        return future;
    }

    /**
     * Complete a placeholder pipeline with a value.
     */
    public boolean complete(T value) {
        return future.complete(value);
    }

    /**
     * Combine two pipelines, waiting for both to complete.
     */
    public static <A, B, R> Pipeline<R> combine(Pipeline<A> a, Pipeline<B> b,
                                                  java.util.function.BiFunction<A, B, R> combiner) {
        return new Pipeline<>(a.future.thenCombineAsync(b.future, combiner, DEFAULT_EXECUTOR));
    }
}
