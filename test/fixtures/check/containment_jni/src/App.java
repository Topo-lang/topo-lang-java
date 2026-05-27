package app;

public class App {
    public static int compute(int x) {
        Runtime.getRuntime().loadLibrary("native_compute");
        return x * 2;
    }
}
