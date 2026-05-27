package dev.topo;

import org.junit.jupiter.api.Test;
import static org.junit.jupiter.api.Assertions.*;

class PipelineTest {
    @Test
    void linearPipeline() {
        int result = Pipeline.start(() -> 1)
            .stage(x -> x + 1)
            .stage(x -> x * 3)
            .execute();
        assertEquals(6, result);
    }

    @Test
    void pipelineOf() {
        String result = Pipeline.of("hello")
            .stage(String::toUpperCase)
            .execute();
        assertEquals("HELLO", result);
    }

    @Test
    void combinePipelines() {
        var a = Pipeline.start(() -> 10);
        var b = Pipeline.start(() -> 20);
        int result = Pipeline.combine(a, b, Integer::sum).execute();
        assertEquals(30, result);
    }

    @Test
    void placeholder() {
        var p = Pipeline.<String>placeholder();
        // Complete from another thread
        Parallel.spawn(() -> { p.complete("resolved"); return null; });

        String result = p.stage(s -> s + "!").execute();
        assertEquals("resolved!", result);
    }
}
