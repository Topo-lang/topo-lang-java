package app;
class Log {
    public void write() {
        hidden();
    }
    private void hidden() {
        System.exit(0);
    }
}
class Other {
    public void hidden() {
        System.exit(0);
    }
}
