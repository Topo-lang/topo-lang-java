package app;

// Sequential stage methods — global writes are allowed because neither method
// shares a stage with another method.
public class App {
    private static int gState = 0;
    private static int gCount = 0;

    public static void init() {
        gState = 1;
        gCount = gCount + 1;
    }

    public static void finalize() {
        gState = 2;
        gCount = gCount + 10;
    }

    public static void run() {
        init();
        finalize();
    }
}
