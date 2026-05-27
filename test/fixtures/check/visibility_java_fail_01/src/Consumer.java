package app;

// Consumer.invoke calls Lib.helper, which is declared private in the .topo.
// Different class scopes so this is a cross-class private call — violation.
public class Consumer {
    public static void invoke() {
        app.Lib.helper();
    }
}
