package orders;

/**
 * PaymentResult -- domain type declared in types.topo as orders::PaymentResult.
 */
public class PaymentResult {
    private final boolean isApproved;
    private final int txnId;
    private final double charged;

    PaymentResult(boolean approved, int txnId, double charged) {
        this.isApproved = approved;
        this.txnId = txnId;
        this.charged = charged;
    }

    public boolean approved() { return isApproved; }
    public int transactionId() { return txnId; }
    public double chargedAmount() { return charged; }
}
