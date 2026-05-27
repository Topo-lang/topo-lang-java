package app;

public class Lib {
    static void alpha() {
        int n = 0;
        if (n >= 0) return;
    }

    static void beta() {
        int n = 0;
        if (n >= 0) return;
    }

    public static void publicEntry() {
        alpha();
        beta();
    }
}
