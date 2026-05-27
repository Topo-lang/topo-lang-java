package orders;

/**
 * Order -- domain type declared in types.topo as orders::Order.
 */
public class Order {
    private final int orderId;
    private final int custId;
    private final double amount;
    private final int items;

    public Order(int id, int customerId) {
        this.orderId = id;
        this.custId = customerId;
        this.amount = 99.95;
        this.items = 3;
    }

    public int id() { return orderId; }
    public int customerId() { return custId; }
    public double total() { return amount; }
    public int itemCount() { return items; }
    public boolean isValid() { return items > 0 && amount > 0.0; }
}
