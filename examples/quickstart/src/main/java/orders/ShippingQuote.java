package orders;

/**
 * ShippingQuote -- domain type declared in types.topo as orders::ShippingQuote.
 */
public class ShippingQuote {
    private final double shipCost;
    private final int days;
    private final int carrier;

    ShippingQuote(double cost, int days, int carrier) {
        this.shipCost = cost;
        this.days = days;
        this.carrier = carrier;
    }

    public double cost() { return shipCost; }
    public int estimatedDays() { return days; }
    public int carrierId() { return carrier; }
}
