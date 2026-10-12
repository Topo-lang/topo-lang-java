public class Box<T> {
    T v;

    public <U extends Comparable<U>> U pick(U a) {
        return a;
    }
}
