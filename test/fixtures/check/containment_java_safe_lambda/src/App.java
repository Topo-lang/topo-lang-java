package app;

import java.util.function.Function;

public class App {
    public static int measure(int s) {
        // Pure lambda over a primitive — no escape, no system call, no
        // restricted API. Should pass containment.
        Function<String, Integer> length = (str) -> str.length();
        return length.apply("topo-" + s);
    }
}
