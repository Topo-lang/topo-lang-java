public class Safe {
    public int tryFn() {
        try {
            return 1;
        } catch (Exception e) {
            return 0;
        }
    }
}
