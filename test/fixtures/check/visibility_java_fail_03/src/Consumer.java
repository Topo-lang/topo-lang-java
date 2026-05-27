package app;

// Consumer.drive crosses the class boundary to call BOTH private methods
// `alpha` and `beta`.  Two visibility violations expected.
public class Consumer {
    public static void drive() {
        app.Lib.alpha();
        app.Lib.beta();
    }
}
