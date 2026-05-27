package app;

import java.io.FileInputStream;

public class App {
    public static String analyze(String path) {
        try {
            FileInputStream fis = new FileInputStream(path);
            byte[] data = fis.readAllBytes();
            fis.close();
            return new String(data);
        } catch (Exception e) {
            return "";
        }
    }
}
