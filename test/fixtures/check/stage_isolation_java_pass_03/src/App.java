package app;

// process() is at stage<2> and calls init() at stage<1> via the qualified
// `app.App.init()` form so the extractor produces the matching qualified
// callee.  Backward call: process(stage 2) -> init(stage 1) is allowed.
public class App {
    public static void init() {
        int x = 1;
        if (x > 0) return;
    }

    public static void process() {
        app.App.init();
    }

    public static void run() {
        init();
        process();
    }
}
