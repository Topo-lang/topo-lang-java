package app;

public class Calculator {
    public static int add(int a, int b) {
        return a + b;
    }

    public static int multiply(int a, int b) {
        return a * b;
    }

    // This method is not declared in .topo -- should cause a completeness error
    public static int subtract(int a, int b) {
        return a - b;
    }
}
