package app;

// Two forward stage calls: readInput (stage<1>) and normalize (stage<2>)
// both reach into writeOutput (stage<3>).
public class App {
    public static void readInput() {
        app.App.writeOutput();
    }

    public static void normalize() {
        app.App.writeOutput();
    }

    public static void writeOutput() {
        int x = 0;
        if (x >= 0) return;
    }

    public static void run() {
        readInput();
        normalize();
        writeOutput();
    }
}
