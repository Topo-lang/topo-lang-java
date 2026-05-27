package app;

// Same single-line method shape as purity_java_fail_05 but pure: neither
// `parse` nor `audit` writes static state. Confirms the scope-leak fix
// does not introduce a false positive on compact single-line bodies.
public class App {
    public static void parse() { int x = 1; if (x > 0) return; }

    public static void audit() { int y = 2; int z = y + 1; }
}
