package dev.topo;

import org.junit.jupiter.api.Test;
import static org.junit.jupiter.api.Assertions.*;

class ObserveTest {
    @Test
    void stageEventLifecycle() {
        var event = Observe.beginStage("test::init", 0);
        assertNotNull(event);
        // Simulate work
        Observe.endStage(event);
        // No exception means success — JFR events are fire-and-forget
    }

    @Test
    void stageEventFields() {
        var event = new Observe.StageEvent();
        event.stageName = "MyClass::process";
        event.stageOrder = 1;
        assertEquals("MyClass::process", event.stageName);
        assertEquals(1, event.stageOrder);
    }
}
