package dev.topo;

import jdk.jfr.*;

/**
 * Observability integration using JDK Flight Recorder (JFR).
 * Provides custom events for Topo stage execution tracing.
 */
public final class Observe {
    private Observe() {}

    /**
     * JFR event for tracking stage execution.
     */
    @Name("topo.Stage")
    @Label("Topo Stage")
    @Description("Tracks the execution of a Topo stage")
    @Category({"Topo", "Stage"})
    @StackTrace(false)
    public static class StageEvent extends Event {
        @Label("Stage Name")
        public String stageName;

        @Label("Stage Order")
        public int stageOrder;
    }

    /**
     * JFR event for pipeline execution.
     */
    @Name("topo.Pipeline")
    @Label("Topo Pipeline")
    @Description("Tracks pipeline stage execution")
    @Category({"Topo", "Pipeline"})
    @StackTrace(false)
    public static class PipelineEvent extends Event {
        @Label("Pipeline Name")
        public String pipelineName;

        @Label("Stage Index")
        public int stageIndex;

        @Label("Duration (ns)")
        public long durationNs;
    }

    /**
     * JFR event for parallel task execution.
     */
    @Name("topo.Parallel")
    @Label("Topo Parallel Task")
    @Description("Tracks parallel task spawning and completion")
    @Category({"Topo", "Parallel"})
    @StackTrace(false)
    public static class ParallelEvent extends Event {
        @Label("Task Name")
        public String taskName;

        @Label("Thread Name")
        public String threadName;

        @Label("Duration (ns)")
        public long durationNs;
    }

    /**
     * Begin a stage observation. Returns an event that should be committed when the stage ends.
     */
    public static StageEvent beginStage(String name, int order) {
        var event = new StageEvent();
        event.stageName = name;
        event.stageOrder = order;
        event.begin();
        return event;
    }

    /**
     * End and commit a stage observation.
     */
    public static void endStage(StageEvent event) {
        event.end();
        if (event.shouldCommit()) {
            event.commit();
        }
    }
}
