package app;

// `compute` (parallel stage<1>) writes to the static field `counter` —
// purity violation.
public class App {
    private static int counter = 0;

    public static void compute() {
        counter = counter + 1;
    }

    public static void render() {
        // pure: no static writes
        int x = 10;
        if (x > 0) return;
    }

    public static void run() {
        compute();
        render();
    }
}
