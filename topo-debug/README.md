# topo-debug-java

JDWP/JDI Extract adapter for Java host binaries.

Packaged as `topo-debug-java-0.1.0.jar` and launched via the generated
`topo-debug-java.sh` (POSIX) / `topo-debug-java.cmd` (Windows) wrappers,
this adapter drives a Java main class on behalf of `topo-debug query`.
Given `--site <file:line>` and `--target <launcher-or-class>` it sets a
JDI breakpoint, attaches to the inferior JVM, reads the named variable
from the stopped frame, and emits the bytes + layout descriptor over the
topo-debug binary-frame wire protocol on stdout.

Wire-compatible with `topo-debug-cpp` / `topo-debug-rust`; pick between
adapters via the `topo-debug --adapter` flag. The CLI side (the
multivar dispatcher) is host-language agnostic.

## Launch model

`--target` is either:

* **An executable launcher script** (file exists + is executable) — the
  adapter execs it and parses the JVM's "Listening for transport
  dt_socket at address: \<port\>" line from stdout. The launcher MUST
  start the JVM with `-agentlib:jdwp=transport=dt_socket,server=y,
  suspend=y,address=127.0.0.1:0`. The CMake build generates one such
  launcher per fixture (e.g., `run_tiny_matrix`).
* **A bare Java main-class name** — the adapter synthesises
  `java -agentlib:jdwp=... <ClassName>` from its own `java.home` and
  assumes the user's CLASSPATH covers it. Used only for ad-hoc invocation.

### `--target` trust model

The adapter validates `--target` before spawning anything (see
`Main.validateTarget`):

* The launcher-script branch canonicalises with `Files.toRealPath`,
  then — when the env var `TOPO_DEBUG_JAVA_ALLOWED_ROOTS` is set
  (path-separator-delimited list of directories) — requires the
  resolved path to live under one of the allowlist roots. With no
  allowlist set the local-dev shape stays back-compat: any executable
  path is accepted.
* The bare-classname branch enforces the JLS identifier grammar
  (`(Letter)(Letter|Digit)*` joined by `.`), rejects a leading `-`
  (so the value cannot be mistaken for a `java` flag), and rejects any
  embedded ASCII control character.

A poisoned `*.topo-dbg.json` shipped with a third-party project
therefore cannot point `--target` at `/usr/bin/curl` or inject a `java`
command-line flag through the bare-classname branch. The
`topo-dbg.json` registry remains part of the project trust boundary —
it is treated as project source, not external untrusted input.

## Frame selection

When a breakpoint fires on a line that crosses an `invokedynamic` (e.g.,
string-concat's `makeConcatWithConstants`), HotSpot can deliver the event
while the thread is still mid-resolution inside `java.lang.invoke.*` /
`jdk.internal.*`. The adapter therefore walks the stack from `frame(0)`
upwards and picks the first frame whose `Method` matches
`BreakpointEvent.location().method()` — that gives a stable user frame
across `-cp` cache states and JIT warm-up.

## Supported variable types

* Primitive integer / float scalars (`byte`, `short`, `int`, `long`,
  `float`, `double`, `char`, `boolean`).
* Rectangular N-dimensional arrays of the above (`int[][]`, `double[]`,
  ...). Ragged Java arrays exit 4.
* Anything else exits 4 with a diagnostic.

## Exit codes

| Code | Meaning |
|------|---------|
| 0    | ok |
| 1    | CLI / usage / IO error |
| 2    | target launch failed |
| 3    | breakpoint never hit / runtime error |
| 4    | variable type unsupported |
