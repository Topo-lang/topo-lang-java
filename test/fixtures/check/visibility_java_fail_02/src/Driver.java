package app;

// Driver is NOT declared in the .topo, so it's external from the visibility
// check's perspective.  It calls Lib.initInternal (declared internal) via a
// qualified `Lib.initInternal()` access.  The qualified callee name resolves
// to `Lib::initInternal` which matches the visMap entry `app::Lib::initInternal`
// only if the namespace prefix logic compares the suffix — actually this
// requires the extractor to emit the fully-qualified callee.
//
// Since the call is written as `Lib.initInternal()`, the extractor outputs
// callee `Lib::initInternal`.  visMap has `app::Lib::initInternal`.  These
// strings differ so no violation is triggered.  To make the test deterministic
// we use the full package-qualified form `app.Lib.initInternal()`.
public class Driver {
    public static void invoke() {
        app.Lib.initInternal();
    }
}
