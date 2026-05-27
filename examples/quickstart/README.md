# Topo Quickstart: Order Processing (Java)

## What This Example Does

An order processing system with clear API boundaries and execution stages:

- **public**: `processOrder()` -- the single entry point
- **protected**: `validateOrder()`, `chargePayment()`, `calculateShipping()`, `createInvoice()` -- reusable processing components
- **private**: `sendConfirmation()`, `updateAnalytics()`, `checkInventory()`, `verifyAddress()`, `applyDiscount()` -- implementation details
- **internal** (package-private): `dumpOrderState()` -- debug/test helper

### What the Declarations Mean

The `.topo` file declares a 4-stage processing pipeline:

```
                        +-------------------+
          Stage 1:      | validateOrder()   |
                        +---------+---------+
                                  |
                    +-------------+-------------+
                    |                           |
          Stage 2:  | chargePayment()     calculateShipping()  |
                    |  (parallel-safe)      (parallel-safe)    |
                    +-------------+-------------+
                                  |
          Stage 3:      +---------+---------+
                        | createInvoice()   |
                        +---------+---------+
                                  |
                    +-------------+-------------+
                    |                           |
          Stage 4:  | sendConfirmation()  updateAnalytics()    |
                    |  (parallel-safe)      (parallel-safe)    |
                    +-------------+-------------+
```

- **Stage 1**: `validateOrder` -- must complete before anything else
- **Stage 2**: `chargePayment` + `calculateShipping` -- independent operations that can safely run in parallel
- **Stage 3**: `createInvoice` -- depends on both stage-2 results
- **Stage 4**: `sendConfirmation` + `updateAnalytics` -- independent post-processing

Topo enforces these constraints:
- Stage 2 cannot start before stage 1 completes
- `chargePayment` and `calculateShipping` must not share mutable state (they are declared parallel-safe)
- Code outside the `orders` package cannot call `private` methods like `sendConfirmation`

## Try It

### Step 1: Validate declarations

```sh
topo --check topo-lang-java/examples/quickstart/topo/processor.topo
```

### Step 2: Verify completeness

```sh
topo-test --project topo-lang-java/examples/quickstart --check-completeness
```

Expected output:

```
[PASS] Completeness: 11 host symbol(s) checked, 11 .topo function(s) -- all OK
```

### Step 3: Add an undeclared function --> ERROR

Add a new method to `OrderProcessor.java` without updating the `.topo` file:

```java
public static boolean cancelOrder(int orderId) { return true; }
```

Run the check again:

```sh
topo-test --project topo-lang-java/examples/quickstart --check-completeness
```

```
ERROR: symbol 'orders::cancelOrder' exists in host code
       but is not declared in .topo
```

**Why it matters**: Someone added a new method but forgot to update the contract.
Topo catches this before compilation.

### Step 4: Remove a declared function --> WARNING

Delete the `calculateShipping()` method from `OrderProcessor.java`
(keep the `.topo` declaration).

```sh
topo-test --project topo-lang-java/examples/quickstart --check-completeness
```

```
WARNING: function 'orders::calculateShipping' is declared in .topo
         but not found in host code
```

### Step 5: Visibility mismatch

Change `sendConfirmation()` from `private` to `public`:

```java
public static void sendConfirmation(Invoice invoice) { ... }
```

```sh
topo-test --project topo-lang-java/examples/quickstart --check-completeness
```

```
WARNING: function 'orders::sendConfirmation' is declared private in .topo
         but has public visibility in host code
```

Now change `processOrder()` from `public` to `private`:

```java
private static Invoice processOrder(Order order) { ... }
```

```sh
topo-test --project topo-lang-java/examples/quickstart --check-completeness
```

```
ERROR: function 'orders::processOrder' is declared public in .topo
       but has private visibility in host code
```

### Step 6: Restore and pass

Undo all changes. Run the check again:

```sh
topo-test --project topo-lang-java/examples/quickstart --check-completeness
```

```
[PASS] Completeness: all OK
```

### Step 7: Verify stage isolation

Stage isolation verifies that stage N does not depend on stage N+1 outputs.
This requires a test command that exercises the pipeline:

```sh
topo-test --project topo-lang-java/examples/quickstart \
          --check-isolation \
          --test-cmd "echo 'pipeline-test-placeholder'"
```

Expected: each stage passes independently — stage 2 methods (`chargePayment`,
`calculateShipping`) do not call stage 3 or 4 methods.

### Step 8: Verify parallel safety

Purity check verifies that functions in the same stage (declared parallel-safe)
do not access shared mutable state:

```sh
topo-test --project topo-lang-java/examples/quickstart \
          --check-purity \
          --test-cmd "echo 'pipeline-test-placeholder'"
```

Expected: stage 2 (`chargePayment` + `calculateShipping`) and stage 4
(`sendConfirmation` + `updateAnalytics`) pass — no shared global state.

## What You Learned

| Violation | Severity | Topo Report |
|-----------|----------|-------------|
| Code has function, `.topo` does not declare it | ERROR | "exists in host code but not declared" |
| `.topo` declares function, code does not implement it | WARNING | "declared but not found in host code" |
| `.topo` says public, code is private | ERROR | "declared public but private" |
| `.topo` says private, code is public | WARNING | "declared private but public" |
| Stage N calls stage N+1 method | ERROR | "stage isolation violated" |
| Parallel-stage method writes global state | ERROR | "purity violation" |

## What's Next

- **Showcase example**: demonstrates all Topo features -- constraints, templates, pipeline DAG, lifetime management, priority, ownership
