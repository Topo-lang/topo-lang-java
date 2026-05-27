// Tiny JDI extract fixture (Java parallel of tiny_matrix.cpp).
public class TinyMatrix {
    public static void main(String[] args) {
        int[][] matrix = new int[16][16];
        matrix[5][7] = 42;
        int sentinel = 0;  // breakpoint here — adapter reads matrix at this line
        System.out.println("done " + sentinel);
    }
}
