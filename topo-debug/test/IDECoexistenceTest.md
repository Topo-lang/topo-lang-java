# IDE Coexistence Smoke Test — topo-debug-java + IntelliJ / VSCode-Java

Acceptance criterion:

> IntelliJ / VSCode-Java and topo-debug-java attached to the same JVM
> can both hit a breakpoint, with no JDWP protocol conflict.

This is a documentation / manual smoke-level criterion (no code change
required — verification is smoke-level only). It cannot be automated in
CI (it requires a desktop IDE GUI and human interaction) and is **not
verified in this headless environment** — IDE/JDWP coexistence remains
manually-verified-only until a desktop run confirms it.

## JDWP topology — why the procedure matters

JDWP over `dt_socket` accepts **exactly one debugger connection per
listening socket**. Stock JDWP does not multiplex multiple JDI/DAP clients
onto one transport. So "two debuggers on the same JVM" only works one of
two ways:

1. **Two independent JDWP servers in one JVM is NOT possible** — a JVM
   loads a single `jdwp` agent.
2. **Coexistence works via the IDE-as-server, topo-as-not-attached model
   below, OR by alternation** (one attaches, detaches, the other attaches).

Critically, the default `topo-debug-java` flow **spawns its own target
JVM** with
`-agentlib:jdwp=transport=dt_socket,server=y,suspend=y,address=127.0.0.1:0`
and is the **sole** JDWP client of that JVM
(`topo-lang-java/topo-debug/src/main/java/dev/topo/debug/Main.java`,
`launch` + `attachJdwp`). In that mode an IDE **cannot** also connect —
the socket is already consumed by topo-debug-java. Coexistence therefore
requires the **shared-server topology** in Procedure A.

## Procedure A — shared JDWP server, IDE listens, topo-debug-java attaches (supported path)

Goal: both the IDE and topo-debug-java debug the *same* running JVM.

1. **Start the target JVM yourself** (not via topo-debug-java's spawn
   path) with a *listening* JDWP server on a fixed port:

   ```
   java -agentlib:jdwp=transport=dt_socket,server=y,suspend=y,address=127.0.0.1:5005 \
        -cp <app-classpath> com.example.MainKt
   ```

   `suspend=y` holds the JVM until the **first** debugger attaches.

2. **Attach IntelliJ** (Run -> Edit Configurations -> Remote JVM Debug ->
   host `127.0.0.1`, port `5005`, "Attach to remote JVM"). Set a
   breakpoint in a `.topo`-declared host method (e.g. the matrix kernel in
   `test/fixtures/TinyMatrix.java`). The first attach satisfies
   `suspend=y`; the JVM proceeds and hits the breakpoint. **Expected:**
   IntelliJ stops at the breakpoint, shows frames/locals.

3. While IntelliJ is paused at that breakpoint, **JDWP is single-client**:
   topo-debug-java cannot attach to port `5005` simultaneously (the IDE
   owns the connection). To exercise topo-debug-java against the *same*
   logical JVM you must use **Procedure B (alternation)** — see below.
   This is the honest limitation: stock `dt_socket` JDWP is 1:1, so true
   *simultaneous* dual-attach is not a stock-JDWP capability and must not
   be claimed.

## Procedure B — alternation on the same JVM (the realistic "coexistence")

The realistic interpretation of "both debuggers attached to the same JVM"
that stock JDWP supports is **serial coexistence without protocol
conflict**: the same JVM process is debugged by topo-debug-java and then
by the IDE (or vice versa), detach-then-attach, with no port/agent
corruption.

1. Start the target JVM with `server=y,suspend=n,address=127.0.0.1:5005`
   (`suspend=n` so it does not block waiting for a first client; use a
   long-running workload or a `readLine()` gate so it stays alive).

2. **Attach topo-debug-java** in attach mode (point its target at the
   already-running address rather than letting it spawn). Run a query that
   sets a breakpoint, hits it, reads bytes via
   `ArrayReference.getValues`, emits the binary-frame payload, then
   **detaches cleanly** (`VirtualMachine.dispose()`).
   **Expected:** topo-debug-java prints the binary frame; the JVM resumes
   on detach.

3. **Now attach IntelliJ / VSCode-Java** to the *same* port `5005` on the
   *same* JVM process. Set a breakpoint; trigger the workload again.
   **Expected:** the IDE stops at the breakpoint with full frames/locals.
   No "address already in use", no JDWP handshake error, no agent crash —
   proving the first debugger's detach left the JDWP server in a clean
   reusable state (no JDWP protocol conflict).

4. Reverse the order (IDE first, detach, then topo-debug-java) and repeat
   step 2-3. **Expected:** symmetric — both orders succeed.

## VSCode-Java specifics

VSCode-Java (`vscode-java` / `java-debug`) speaks **DAP** to the editor
but the `java-debug` adapter itself attaches to the JVM over **JDWP**
(`request: "attach"`, `hostName`, `port`). So from the JVM's perspective
it is identical to IntelliJ — one JDWP client. The same Procedure B
alternation applies; substitute the VSCode `launch.json`:

```json
{ "type": "java", "name": "Attach", "request": "attach",
  "hostName": "127.0.0.1", "port": 5005 }
```

## Pass / fail criteria

| Check | Pass condition |
|------|----------------|
| IDE breakpoint | IDE stops at a `.topo`-declared host method, shows locals |
| topo-debug-java breakpoint | Emits the topo-debug binary frame at the same logical breakpoint |
| No protocol conflict | After detach + re-attach (either order), the second debugger attaches with no JDWP handshake / port error and hits its breakpoint |
| Clean detach | First debugger's `dispose()` resumes the JVM; JDWP server reusable |

A run **passes** only if all four hold in **both** orders
(topo-first-then-IDE and IDE-first-then-topo).

## Known constraint (not a failure)

Stock `dt_socket` JDWP is **1 client per socket**. *Simultaneous*
dual-attach to one socket is outside stock JDWP and is **not** a Topo
defect — the supported coexistence story is the alternation in Procedure
B. The acceptance phrase "both debuggers attached to the same JVM" is
satisfied in the sense that the *same JVM process* is debuggable by both
tools without protocol conflict, not in the sense of two live JDWP
connections on one socket. (If simultaneous multi-client is ever required,
it needs a JDWP-proxy/multiplexer — out of scope for this adapter, left
as a follow-up.)
