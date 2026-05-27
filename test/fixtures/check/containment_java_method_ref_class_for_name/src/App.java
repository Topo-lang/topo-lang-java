package app;

import java.util.function.Function;

public class App {
    public static int apply_loader(int nameId) {
        // Method reference to Class.forName — bound to a Function variable and
        // invoked indirectly. Without JDT binding resolution, only the surface
        // pattern Class::forName is visible.
        Function<String, Class<?>> loader = Class::forName;
        try {
            Class<?> cls = loader.apply("app.Generated");
            return cls.getName().length();
        } catch (Exception e) {
            return nameId;
        }
    }
}
