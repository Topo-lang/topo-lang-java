package app;

// Three parallel methods each write a distinct static field.
public class App {
    private static int alphaCount = 0;
    private static int betaCount = 0;
    private static int gammaCount = 0;

    public static void alpha() {
        alphaCount = alphaCount + 1;
    }

    public static void beta() {
        betaCount += 2;
    }

    public static void gamma() {
        gammaCount = 99;
    }

    public static void run() {
        alpha();
        beta();
        gamma();
    }
}
