package app;

import java.net.URLClassLoader;
import java.net.URL;

public class App {
    public static String load(String name) {
        try {
            URL[] urls = { new URL("file:///tmp/plugins/") };
            URLClassLoader loader = new URLClassLoader(urls);
            Class<?> cls = loader.loadClass(name);
            return cls.getName();
        } catch (Exception e) {
            return "";
        }
    }
}
