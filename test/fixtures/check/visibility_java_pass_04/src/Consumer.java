package app;

// Cross-class private call would normally violate visibility, but mode=off
// in Topo.toml suppresses the check entirely.
public class Consumer {
    public static void invoke() {
        Lib.hidden();
    }
}
