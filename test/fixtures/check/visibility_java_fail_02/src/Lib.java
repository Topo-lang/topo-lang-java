package app;

public class Lib {
    static void initInternal() {
        // declared internal in .topo
        int n = 0;
        if (n >= 0) return;
    }

    public static void publicEntry() {
        initInternal();
    }
}
