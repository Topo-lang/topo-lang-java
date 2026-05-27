package app;

// load() and prefetch() are both stage<1>; load() calls prefetch().  Same
// stage so no violation.
public class App {
    public static void load() {
        prefetch();
    }

    public static void prefetch() {
        int x = 1;
        if (x > 0) return;
    }

    public static void run() {
        load();
        prefetch();
    }
}
