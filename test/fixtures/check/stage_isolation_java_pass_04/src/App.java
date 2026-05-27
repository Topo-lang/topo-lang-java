package app;

// init() (stage<1>) calls process() (stage<2>) via the qualified form —
// would normally violate stage isolation, but mode=off suppresses the check.
public class App {
    public static void init() {
        app.App.process();
    }

    public static void process() {
        int x = 1;
        if (x > 0) return;
    }

    public static void run() {
        init();
        process();
    }
}
