package app;

import java.lang.ProcessBuilder;

public class App {
    public static void schedule(String task) {
        Thread t = new Thread(() -> {
            try {
                ProcessBuilder pb = new ProcessBuilder("sh", "-c", task);
                pb.start();
            } catch (Exception e) {
                // ignore
            }
        });
        t.start();
    }
}
