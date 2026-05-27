package app;

// Pure stage methods: no static fields anywhere.  compute() and render()
// only touch parameters and locals.
public class App {
    public static void compute() {
        int local = 42;
        local = local + 1;
        int result = local * 2;
        // do nothing with result — pure local computation
        if (result > 0) return;
    }

    public static void render() {
        int x = 5;
        int y = 10;
        int sum = x + y;
        // pure: only locals
        if (sum > 0) return;
    }

    public static void run() {
        compute();
        render();
    }
}
