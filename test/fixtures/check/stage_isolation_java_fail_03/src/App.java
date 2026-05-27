package app;

// Both loadA and loadB (stage<1>) call merge (stage<2>) — two forward calls.
public class App {
    public static void loadA() {
        app.App.merge();
    }

    public static void loadB() {
        app.App.merge();
    }

    public static void merge() {
        int x = 0;
        if (x >= 0) return;
    }

    public static void run() {
        loadA();
        loadB();
        merge();
    }
}
