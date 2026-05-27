package app;

import java.io.FileReader;

public class App {
    public static int compute(int x) {
        try {
            FileReader fr = new FileReader("data.txt");
            fr.close();
        } catch (Exception e) {}
        return x * 2;
    }
}
