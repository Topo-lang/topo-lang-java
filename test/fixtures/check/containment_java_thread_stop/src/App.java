package app;

public class App {
    public static int terminate(int workerId) {
        Thread workerThread = new Thread(() -> {
            while (true) {
                try {
                    Thread.sleep(1000);
                } catch (InterruptedException e) {
                    return;
                }
            }
        });
        workerThread.start();
        // Deprecated: Thread.stop() can leave shared state corrupted.
        workerThread.stop();
        return workerId;
    }
}
