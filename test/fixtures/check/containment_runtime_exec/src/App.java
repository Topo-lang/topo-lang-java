package app;

public class App {
    public static String process(String input) {
        try {
            Runtime.getRuntime().exec("echo " + input);
        } catch (Exception e) {
            // ignore
        }
        return input;
    }
}
