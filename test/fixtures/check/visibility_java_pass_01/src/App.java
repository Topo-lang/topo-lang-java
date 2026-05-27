package app;

// Both compute() and render() are public — no visibility violation.
public class App {
    public static void compute() {
        // simple body
        int x = 1;
        if (x > 0) return;
    }

    public static void render() {
        int y = 2;
        if (y > 0) return;
    }

    public static void run() {
        compute();
        render();
    }
}
