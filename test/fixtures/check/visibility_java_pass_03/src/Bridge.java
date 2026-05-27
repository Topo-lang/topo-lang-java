package app;

// Bridge: public coordinate calls private worker (same class — OK), and
// also calls Util.publicHelper from a sibling class (public — no constraint).
public class Bridge {
    private static void worker() {
        // private impl
        int x = 5;
        if (x > 0) return;
    }

    public static void coordinate() {
        worker();
        Util.publicHelper();
    }
}
