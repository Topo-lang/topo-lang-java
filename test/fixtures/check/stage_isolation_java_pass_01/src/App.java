package app;

// init() and process() are independent — neither calls the other.
public class App {
    public static void init() {
        int x = 1;
        if (x > 0) return;
    }

    public static void process() {
        int y = 2;
        if (y > 0) return;
    }

    public static void run() {
        init();
        process();
    }
}
