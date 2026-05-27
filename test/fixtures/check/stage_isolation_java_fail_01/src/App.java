package app;

// init() (stage<1>) calls process() (stage<2>) via the qualified
// `app.App.process()` form so the L1 extractor produces the matching
// callee `app::App::process`.  Forward stage call — violation.
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
