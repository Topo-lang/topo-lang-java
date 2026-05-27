package app;

public class Lib {
    static void helper() {
        // private (relative to .topo) implementation
        int n = 0;
        if (n >= 0) return;
    }

    public static void publicEntry() {
        helper();
    }
}
