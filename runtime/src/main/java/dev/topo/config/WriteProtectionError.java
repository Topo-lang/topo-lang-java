package dev.topo.config;

/**
 * A write to an item was refused because the presented credential level is
 * below what the item's impact level requires; or a read/enumeration was
 * refused because the presented level is below the item's read tier.
 *
 * <p>The message is about the credential gap only — never about identity. The
 * gate is a mis-operation guard rail, not a secrecy boundary.
 *
 * <p>Reference parity: the Python contract uses a {@code PermissionError}
 * subclass.
 */
public class WriteProtectionError extends RuntimeException {
    public WriteProtectionError(String message) {
        super(message);
    }
}
