package app;

import java.security.AccessController;
import java.security.PrivilegedAction;

public class App {
    public static int privileged(int arg) {
        // doPrivileged escapes the current call-stack security context.
        Integer result = AccessController.doPrivileged(
            (PrivilegedAction<Integer>) () -> arg + 1
        );
        return result;
    }
}
