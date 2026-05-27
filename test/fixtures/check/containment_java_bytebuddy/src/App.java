package app;

import net.bytebuddy.ByteBuddy;
import net.bytebuddy.dynamic.DynamicType;

public class App {
    public static int build_subclass(int parentId) {
        // ByteBuddy fluent API generates new bytecode at runtime — escape from
        // the static class hierarchy.
        DynamicType.Unloaded<?> unloaded = new ByteBuddy()
            .subclass(Object.class)
            .name("app.Generated")
            .make();
        return parentId;
    }
}
