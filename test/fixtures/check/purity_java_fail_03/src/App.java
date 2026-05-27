package app;

// `compute` uses ++/--/compound-assignment on a static field.  All three
// forms are writes — at least two distinct violations expected.
public class App {
    private static int ticks = 0;

    public static void compute() {
        ++ticks;
        ticks++;
    }

    public static void render() {
        // pure
        int n = 1;
        if (n > 0) return;
    }

    public static void run() {
        compute();
        render();
    }
}
