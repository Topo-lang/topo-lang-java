package app;

import java.io.FileReader;

public class App {
    public static int read_file(int id) {
        try {
            FileReader fr = new FileReader("data.txt");
            fr.close();
        } catch (Exception e) {}
        return 0;
    }

    public static int process(int x) {
        return x * 2;
    }
}
