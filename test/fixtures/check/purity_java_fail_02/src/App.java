package app;

// `compute` writes to the public static field `gValue` via a qualified
// `App.gValue =` access — still a purity violation.
public class App {
    public static int gValue = 0;

    public static void compute() {
        App.gValue = App.gValue + 5;
    }

    public static void render() {
        // pure
        int x = 7;
        if (x > 0) return;
    }

    public static void run() {
        compute();
        render();
    }
}
