// Project-simple Java fixture — host binary (full
// topo-build-jvm-java flow, JDI counterpart of cpp/rust project_simple).
//
// Companion to main.topo. The breakpoint fires on the `int sentinel = 0;`
// line after `data` is fully populated. Unlike rustc at -O2, javac at -g
// emits a complete LocalVariableTable so JDI can resolve `data` from the
// stopped frame without needing an explicit load-side breakpoint.
//
// Data values are chosen so that the halves give visibly distinct sums:
//   first_half  (data[0..4]) = 1+2+3+4       = 10
//   second_half (data[4..8]) = 10+20+30+40   = 100
//   total       (sum(data))                  = 110
//   shape(data)                              = [8]
//   dtype(data)                              = i32
//
// JDWP invokedynamic quirk: a breakpoint that
// shares a line with a string-concat `invokedynamic` can deliver while
// the thread is still inside `java.lang.invoke.StringConcatFactory`
// frames — `bpEvent.location()` is correct but `thread.frame(0)` is in
// the MethodHandle generator. The adapter already walks the stack
// (Main.java:691-705) to find the matching frame, but to keep this
// fixture's failure modes obvious the sentinel line below is a plain
// `int sentinel = 0;` — no string concat, no `println`, no `format`.
public class Main {
    public static void main(String[] args) {
        int[] data = {1, 2, 3, 4, 10, 20, 30, 40};
        int sentinel = 0;  // breakpoint here — adapter reads `data`
        // Trailing summation: forces the JIT to keep `data` live across
        // the breakpoint. Without it C2 could DCE the array after the
        // initializer + sentinel since nothing else reads it.
        int total = sentinel;
        for (int i = 0; i < data.length; ++i) {
            total += data[i];
        }
        if (total < 0) {
            throw new AssertionError("unreachable");
        }
    }
}
