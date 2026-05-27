package app;

// Final static constants are filtered from the globals set — even if the
// parallel stage methods read them, no purity violation is reported.
public class App {
    public static final int MAX_ITERATIONS = 1000;
    public static final int DEFAULT_VALUE = 42;
    private static final String GREETING = "hello";

    public static void compute() {
        int x = MAX_ITERATIONS / 2;
        int y = DEFAULT_VALUE + 1;
        if (x + y > 0) return;
    }

    public static void render() {
        int n = DEFAULT_VALUE * 2;
        // GREETING is referenced but not written; constants are pure.
        if (GREETING.length() > 0 && n > 0) return;
    }

    public static void run() {
        compute();
        render();
    }
}
