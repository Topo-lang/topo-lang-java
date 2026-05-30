package dev.topo.config;

/**
 * A config value whose type has no stdlib bridge was offered.
 *
 * <p>The message names the offending key and the stdlib-bridging-types
 * gap so the rejection is actionable (for
 * example a TOML datetime: the time_* family is not yet a stdlib type, so
 * accepting it would mean a value with no schema contract). Silently keeping
 * such a value would leave the product reading something nothing in the schema
 * describes — louder is safer than a phantom contract.
 *
 * <p>Reference parity: the Python contract uses a {@code TypeError} subclass;
 * this is an unchecked exception so a contract-violating write fails loudly.
 */
public class UnbridgedValueError extends RuntimeException {
    public UnbridgedValueError(String message) {
        super(message);
    }
}
