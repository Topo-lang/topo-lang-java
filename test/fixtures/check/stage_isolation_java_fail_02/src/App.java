package app;

// load() (stage<1>) jumps directly to emit() (stage<3>) — forward call,
// skipping stage<2>.
public class App {
    public static void load() {
        app.App.emit();
    }

    public static void transform() {
        int x = 2;
        if (x > 0) return;
    }

    public static void emit() {
        int y = 3;
        if (y > 0) return;
    }

    public static void run() {
        load();
        transform();
        emit();
    }
}
