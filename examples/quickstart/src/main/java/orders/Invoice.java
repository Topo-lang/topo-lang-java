package orders;

/**
 * Invoice -- domain type declared in types.topo as orders::Invoice.
 */
public class Invoice {
    private final int invNumber;
    private final double total;
    private final boolean finalized;

    public Invoice(int orderId, int transactionId, double total) {
        this.invNumber = orderId * 1000 + transactionId;
        this.total = total;
        this.finalized = true;
    }

    public int number() { return invNumber; }
    public double grandTotal() { return total; }
    public boolean isFinalized() { return finalized; }
}
