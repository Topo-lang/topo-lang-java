package app;

public class App {
    public static int abort_via_var(int code) {
        // var inference hides the Runtime type from simple-name extraction.
        // JDT binding resolution should still classify rt.halt(0) as Runtime.halt.
        var rt = Runtime.getRuntime();
        rt.halt(0);
        return code;
    }
}
