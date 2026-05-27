package app;

public class App {
    public static int panic(int code) {
        // Bypasses shutdown hooks and finalizers — strict escape from JVM lifecycle.
        Runtime.getRuntime().halt(0);
        return code;
    }
}
