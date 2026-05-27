// Project-multi Java fixture — host binary.
// Counterpart of cpp/rust project_multi: two `int[4]` locals in the same
// frame, summary template references both via `{sum(a)}`/`{sum(b)}` so a
// single adapter spawn resolves all placeholders.
//
// Initialiser literals stay simple (constant-fold-friendly) — javac at
// `-g` still emits the assignment line table so the locals are present
// in the frame at the breakpoint regardless. The trailing summation keeps
// both arrays live so the JIT can't DCE them after the sentinel.
//
// Expected values at the breakpoint (`int sentinel = 0;`):
//   sum(a)              = 1+2+3+4         = 10
//   sum(b)              = 10+20+30+40     = 100
//   sum(a) + sum(b)                       = 110
//   max(b) - max(a)     = 40 - 4          = 36
public class Main {
    public static void main(String[] args) {
        int[] a = {1, 2, 3, 4};
        int[] b = {10, 20, 30, 40};
        int sentinel = 0;  // breakpoint here — adapter reads `a` and `b`
        int t = sentinel;
        for (int i = 0; i < a.length; ++i) {
            t += a[i] + b[i];
        }
        if (t < 0) {
            throw new AssertionError("unreachable");
        }
    }
}
