package app;

// compute() and render() use only their parameters and locals.  Even though
// the class has private state, the parallel methods do not touch it.
public class App {
    private static int unused = 0;  // static but never written by parallel funcs

    public static int helper(int n) {
        int doubled = n * 2;
        return doubled + 3;
    }

    public static void compute() {
        int x = 10;
        int y = helper(x);
        if (y > 0) return;
    }

    public static void render() {
        int a = 1;
        int b = 2;
        int c = a + b;
        if (c > 0) return;
    }

    public static void run() {
        compute();
        render();
    }
}
