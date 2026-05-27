package app;

import java.lang.invoke.MethodHandles;
import java.lang.invoke.MethodHandles.Lookup;

public class App {
    public static int load_class(int classId) {
        try {
            byte[] bytes = new byte[]{};
            Lookup lookup = MethodHandles.lookup();
            // Define a class at runtime via the trusted Lookup API — bypasses
            // standard ClassLoader constraints and access control.
            lookup.defineClass(bytes);
        } catch (Exception e) {
            // ignore
        }
        return classId;
    }
}
