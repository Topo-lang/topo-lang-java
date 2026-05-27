package dev.topo.config;

import java.util.EnumMap;
import java.util.Map;

/**
 * Write protection: impact level + credential gate.
 *
 * <p>This gate exists to stop <em>mistaken</em> writes to items where a wrong
 * value has outsized blast radius — it is a guard rail, not a secrecy
 * boundary. It is identity-independent by construction: the check takes a
 * credential <em>level</em>, never a principal. A human and an agent
 * presenting the same level are treated identically; there is no "who"
 * argument anywhere.
 *
 * <p>The required-credential-level table is held as an explicit, mutable
 * ordered map (not an {@code impact == HIGH} branch) so inserting a mid level
 * later is a table edit, not a logic rewrite — exactly the multi-level
 * generalisation the reference contract pins.
 *
 * <p><strong>Thread safety.</strong> All table mutations and reads go
 * through {@link #TABLE_LOCK}. The exposed {@code REQUIRED_CREDENTIAL_LEVEL}
 * map is therefore <em>not</em> safe to iterate directly from outside this
 * class; use {@link #snapshotRequiredCredentialLevels()} for a defensive
 * copy you may walk freely. This protects the {@link #snapshot}/
 * {@link #restore} pair from interleaving with a concurrent
 * {@link #authorizeWrite} that reads the LOW/HIGH threshold.
 */
public final class WriteProtection {

    private WriteProtection() {
    }

    /**
     * Monitor that serialises every read and write of
     * {@link #REQUIRED_CREDENTIAL_LEVEL}.
     */
    private static final Object TABLE_LOCK = new Object();

    /**
     * Credential level a writer must present to pass the gate for an item of a
     * given impact. Mutable on purpose: the reference contract demonstrates
     * inserting a mid threshold by editing this table, not by rewriting logic.
     * {@link #snapshotRequiredCredentialLevels()} /
     * {@link #restoreRequiredCredentialLevels(Map)} let a caller edit and then
     * restore it cleanly.
     *
     * <p>Direct iteration from outside this class is unsafe — go through
     * {@link #snapshotRequiredCredentialLevels()} for a defensive copy. The
     * field stays {@code public} for the documented edit-then-restore
     * extension point but every mutation here is wrapped by
     * {@link #TABLE_LOCK}.
     */
    public static final Map<ImpactLevel, Integer> REQUIRED_CREDENTIAL_LEVEL =
            new EnumMap<>(ImpactLevel.class);

    static {
        REQUIRED_CREDENTIAL_LEVEL.put(ImpactLevel.LOW, 0);
        REQUIRED_CREDENTIAL_LEVEL.put(ImpactLevel.HIGH, 1);
    }

    /**
     * A writer with no credential is level 0 — enough for LOW items, short of
     * anything that requires deliberate intent.
     */
    public static final int NO_CREDENTIAL_LEVEL = 0;

    /** The minimum credential level a writer must present for this item. */
    public static int requiredCredentialLevel(ItemPolicy policy) {
        synchronized (TABLE_LOCK) {
            return REQUIRED_CREDENTIAL_LEVEL.get(policy.impact());
        }
    }

    /**
     * The minimum permission level a caller must present to have this item
     * enumerated/read. {@code 0} means unrestricted (a non-permission item
     * that any caller, credentialled or not, can see).
     *
     * <p>This is the read-visibility tiering role — the orthogonal twin of
     * {@link #requiredCredentialLevel(ItemPolicy)} (the write gate). Both
     * consult the same integer scale; they answer different questions (may I
     * <em>see</em> it vs. may I <em>change</em> it) and never collapse into
     * one another.
     */
    public static int requiredReadLevel(ItemPolicy policy) {
        return policy.readLevel();
    }

    /**
     * Pass iff {@code credentialLevel} meets the item's required level.
     *
     * <p>Note the signature: there is no principal/identity parameter. The
     * gate cannot and does not distinguish a human from an agent — it only
     * compares levels, which is exactly the "guard against accidental
     * writes, not a secrecy boundary" intent.
     */
    public static void authorizeWrite(String key, ItemPolicy policy,
            int credentialLevel) {
        // Single locked read so the threshold doesn't change between the
        // comparison and the error message.
        int needed;
        synchronized (TABLE_LOCK) {
            needed = REQUIRED_CREDENTIAL_LEVEL.get(policy.impact());
        }
        if (credentialLevel < needed) {
            throw new WriteProtectionError(
                    "config key '" + key + "' is impact=" + policy.impact().name()
                            + "; writing it requires credential level >= "
                            + needed + ", but the write presented level "
                            + credentialLevel + ". This guard prevents "
                            + "accidental high-impact changes; re-issue the "
                            + "write with a sufficient credential level if the "
                            + "change is intended.");
        }
    }

    /** A defensive copy of the current required-credential-level table. */
    public static Map<ImpactLevel, Integer> snapshotRequiredCredentialLevels() {
        synchronized (TABLE_LOCK) {
            return new EnumMap<>(REQUIRED_CREDENTIAL_LEVEL);
        }
    }

    /** Replace the table contents with a previously taken snapshot. */
    public static void restoreRequiredCredentialLevels(
            Map<ImpactLevel, Integer> snapshot) {
        synchronized (TABLE_LOCK) {
            REQUIRED_CREDENTIAL_LEVEL.clear();
            REQUIRED_CREDENTIAL_LEVEL.putAll(snapshot);
        }
    }
}
