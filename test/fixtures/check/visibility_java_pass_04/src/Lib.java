package app;

public class Lib {
    static void hidden() {
        // private impl
        int n = 0;
        if (n >= 0) return;
    }

    public static void publicEntry() {
        hidden();
    }
}
