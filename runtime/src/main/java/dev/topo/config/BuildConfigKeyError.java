package dev.topo.config;

/**
 * Raised when a key that belongs to the build toolchain is offered to the
 * product runtime config. The message names {@code Topo.toml} so the user is
 * told exactly where the key actually belongs instead of the product config
 * silently accepting a key nothing reads.
 *
 * <p>Mirrors the reference contract's {@code ValueError} subclass: an
 * unchecked exception so a misplaced key fails the write loudly.
 */
public class BuildConfigKeyError extends IllegalArgumentException {
    public BuildConfigKeyError(String message) {
        super(message);
    }
}
