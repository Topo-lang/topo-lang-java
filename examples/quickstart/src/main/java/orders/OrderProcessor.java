package orders;

/**
 * OrderProcessor -- implements processor.topo: processOrder staged pipeline
 * and all four visibility tiers (public, protected, private, internal).
 *
 * Domain types (Order, PaymentResult, ShippingQuote, Invoice) are
 * declared as top-level classes matching the namespace-level type
 * declarations in types.topo.
 */
public class OrderProcessor {

    // --- Private helpers ---

    private static boolean checkInventory(int itemId, int quantity) {
        return true; // simulated: always in stock
    }

    private static boolean verifyAddress(int customerId) {
        return true; // simulated: address always valid
    }

    private static double applyDiscount(double total, int customerId) {
        // VIP customers (id < 100) get 10% off
        if (customerId < 100) {
            return total * 0.9;
        }
        return total;
    }

    // --- Internal (package-private) ---

    static void dumpOrderState(Order order) {
        System.out.printf("[DEBUG] Order #%d: customer=%d, total=%.2f, items=%d, valid=%s%n",
                order.id(), order.customerId(), order.total(),
                order.itemCount(), order.isValid() ? "yes" : "no");
    }

    // --- Protected ---

    protected static boolean validateOrder(Order order) {
        if (!order.isValid()) return false;
        if (!checkInventory(order.id(), order.itemCount())) return false;
        if (!verifyAddress(order.customerId())) return false;
        return true;
    }

    protected static PaymentResult chargePayment(Order order) {
        double charged = applyDiscount(order.total(), order.customerId());
        return new PaymentResult(true, order.id() * 10 + 1, charged);
    }

    protected static ShippingQuote calculateShipping(Order order) {
        double cost = 5.0 + order.itemCount() * 1.5;
        int days = order.itemCount() <= 5 ? 3 : 7;
        return new ShippingQuote(cost, days, 42);
    }

    protected static Invoice createInvoice(Order order,
                                           PaymentResult payment,
                                           ShippingQuote shipping) {
        double grandTotal = payment.chargedAmount() + shipping.cost();
        return new Invoice(order.id(), payment.transactionId(), grandTotal);
    }

    // --- Private ---

    private static void sendConfirmation(Invoice invoice) {
        System.out.printf("Confirmation: Invoice #%d, total $%.2f%n",
                invoice.number(), invoice.grandTotal());
    }

    private static void updateAnalytics(Order order, Invoice invoice) {
        System.out.printf("Analytics: order=%d, invoice=%d, total=$%.2f%n",
                order.id(), invoice.number(), invoice.grandTotal());
    }

    // --- Public ---

    public static Invoice processOrder(Order order) {
        validateOrder(order);

        PaymentResult payment = chargePayment(order);
        ShippingQuote shipping = calculateShipping(order);

        Invoice invoice = createInvoice(order, payment, shipping);

        sendConfirmation(invoice);
        updateAnalytics(order, invoice);

        return invoice;
    }

    public static void main(String[] args) {
        Order order = new Order(42, 7);
        System.out.printf("Processing order #%d for customer #%d...%n",
                order.id(), order.customerId());

        Invoice invoice = processOrder(order);

        System.out.printf("Done: Invoice #%d, grand total $%.2f%n",
                invoice.number(), invoice.grandTotal());
    }
}
