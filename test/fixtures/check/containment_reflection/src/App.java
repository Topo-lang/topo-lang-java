package app;

import java.lang.reflect.Method;

public class App {
    public static String transform(String data) {
        try {
            Class<?> cls = Class.forName("app.Transform");
            Method m = cls.getMethod("run", String.class);
            return (String) m.invoke(null, data);
        } catch (Exception e) {
            return data;
        }
    }
}
