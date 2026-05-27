package app;

// `coordinate` (public) calls `detail` (private) from the same class.
// Same namespace `app::App` so visibility check allows it.
public class App {
    private static void detail() {
        // private implementation
        int n = 0;
        if (n >= 0) return;
    }

    public static void coordinate() {
        detail();
    }

    public static void run() {
        coordinate();
    }
}
