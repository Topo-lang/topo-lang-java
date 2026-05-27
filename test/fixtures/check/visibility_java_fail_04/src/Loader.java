package app;

// Loader is a separate class.  It calls Storage.flushBuffers via the fully
// qualified `app.Storage.flushBuffers()` form, which the L1 extractor turns
// into the callee `app::Storage::flushBuffers`.  That matches the .topo's
// private declaration and triggers a visibility violation because the
// caller `app::Loader::load` lives in a different class scope.
public class Loader {
    public static void load() {
        app.Storage.flushBuffers();
    }
}
