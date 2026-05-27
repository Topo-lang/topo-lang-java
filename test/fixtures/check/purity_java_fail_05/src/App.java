package app;

// Both methods have SINGLE-LINE bodies. `parse` is the scope-leak trigger
// (compact `{ ... }` whose `}` is consumed before method detection);
// `audit` (parallel stage<1>) writes the static field `sideLog` on one
// line. Pre-fix the extractor leaked `inFunction` past `parse` and dropped
// `audit`'s write (false PASS). Post-fix `sideLog` is flagged.
public class App {
    private static int sideLog = 0;

    public static void parse() { int x = 1; if (x > 0) return; }

    public static void audit() { sideLog = sideLog + 1; }
}
