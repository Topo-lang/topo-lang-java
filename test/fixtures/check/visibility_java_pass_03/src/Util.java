package app;

// Util.publicHelper is a public sibling method.  Bridge calls it freely.
public class Util {
    public static void publicHelper() {
        int n = 1;
        if (n > 0) return;
    }
}
