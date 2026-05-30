// topo-debug-java — JDWP/JDI Extract adapter.
//
// Drives a JDWP-debuggable Java class through com.sun.jdi.*, hits a single
// line breakpoint, reads a chosen local variable from the stopped frame, and
// emits the bytes + layout descriptor on stdout using the binary-frame wire
// protocol. Mirrors topo-lang-cpp/topo-debug/adapter.cpp 1-for-1 in shape;
// only the host-debugger plumbing is different (JDI instead of LLDB).
//
// CLI (matches the C++ adapter so the topo-debug CLI's dispatcher stays
// adapter-agnostic):
//   topo-debug-java --site <file:line> --target <classpath-or-Class>
//                   [--var <name>] [-- <target-args>...]
//
// `--target` is the user-facing argument from `topo debug query ... --target X`.
// The adapter treats it as either:
//   * a path to an executable launcher (the file exists) — we exec it and
//     parse the JVM's stderr "Listening for transport ... at: <port>" line,
//     then JDI-attach. The launcher is responsible for wiring `-cp` and the
//     main-class name; it MUST start the JVM with
//     `-agentlib:jdwp=...,server=y,suspend=y,address=127.0.0.1:0`.
//   * a bare main-class name (no file exists at the path) — we synthesise
//     `java -agentlib:jdwp=... <ClassName>` ourselves and assume the user's
//     CLASSPATH env covers it. Used only for casual invocation; CMake-built
//     fixtures always go through the launcher path.
//
// Wire output (all on stdout, in order):
//   1. JSON line  {"kind":"breakpoint_hit","frame":1,"site":"..."}
//   2. For each --var entry:
//        binary frame  type=var_bytes        — raw LE bytes
//        binary frame  type=layout_descriptor — JSON {variable,dtype,shape,strides}
//
// Then waits for one JSON line {"op":"continue"} on stdin and resumes to exit.
// A 30 s wall-clock guards the breakpoint wait — JVM cold start is ~1-3 s on
// a warm cache, much slower than lldb attach.

package dev.topo.debug;

import com.sun.jdi.*;
import com.sun.jdi.connect.*;
import com.sun.jdi.event.*;
import com.sun.jdi.request.*;

import java.io.*;
import java.nio.ByteBuffer;
import java.nio.ByteOrder;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.nio.file.Path;
import java.nio.file.Paths;
import java.util.*;
import java.util.regex.*;

public final class Main {

    // ── Exit codes (match topo-lang-cpp/topo-debug/adapter.cpp 73-86) ────────
    private static final int EXIT_OK = 0;
    private static final int EXIT_USAGE = 1;
    private static final int EXIT_LAUNCH = 2;
    private static final int EXIT_RUNTIME = 3;
    private static final int EXIT_UNSUPPORTED_TYPE = 4;

    private static final long BREAKPOINT_WAIT_MS = 30_000;

    private static final String PROG_NAME = "topo-debug-java";

    // ── Argument parsing ────────────────────────────────────────────────────
    static final class Args {
        String site = "";
        String target = "";
        final List<String> vars = new ArrayList<>();
        final List<String> targetArgs = new ArrayList<>();
    }

    private static List<String> splitCsv(String s) {
        List<String> out = new ArrayList<>();
        int i = 0;
        while (i <= s.length()) {
            int j = s.indexOf(',', i);
            if (j < 0) j = s.length();
            if (j > i) out.add(s.substring(i, j));
            i = j + 1;
        }
        return out;
    }

    private static void printUsage(PrintStream out) {
        out.println("Usage: " + PROG_NAME + " --site <file:line> --target <class-or-launcher>");
        out.println("       " + " ".repeat(PROG_NAME.length()) +
                    " [--var <name>] [-- <target-args>...]");
    }

    private static boolean parseArgs(String[] argv, Args a, StringBuilder err) {
        boolean inTargetArgs = false;
        for (int i = 0; i < argv.length; ++i) {
            String s = argv[i];
            if (inTargetArgs) { a.targetArgs.add(s); continue; }
            if (s.equals("--")) { inTargetArgs = true; continue; }
            if (s.equals("-h") || s.equals("--help")) {
                printUsage(System.out);
                System.exit(EXIT_OK);
            }
            // `--flag=value` first, then `--flag value`
            String got = null;
            String flag = null;
            for (String f : new String[]{"--site", "--target", "--var"}) {
                if (s.startsWith(f + "=")) {
                    flag = f;
                    got = s.substring(f.length() + 1);
                    break;
                }
                if (s.equals(f) && i + 1 < argv.length) {
                    flag = f;
                    got = argv[++i];
                    break;
                }
            }
            if (flag == null) {
                err.append("unknown argument: ").append(s);
                return false;
            }
            switch (flag) {
                case "--site":   a.site   = got; break;
                case "--target": a.target = got; break;
                case "--var":    {
                    List<String> names = splitCsv(got);
                    if (names.isEmpty()) { err.append("--var list is empty"); return false; }
                    for (String n : names) {
                        if (n.isEmpty()) { err.append("--var: empty entry in CSV list"); return false; }
                        a.vars.add(n);
                    }
                    break;
                }
            }
        }
        if (a.site.isEmpty())   { err.append("--site is required");   return false; }
        if (a.target.isEmpty()) { err.append("--target is required"); return false; }
        if (a.vars.isEmpty()) a.vars.add("matrix");   // back-compat default
        return true;
    }

    // Split "file:line" into components. Accepts paths with embedded colons by
    // splitting on the *last* ':'.
    static final class Site { String file; int line; }
    private static boolean splitSite(String site, Site out, StringBuilder err) {
        int pos = site.lastIndexOf(':');
        if (pos < 0) {
            err.append("site '").append(site).append("' missing ':' (expected file:line)");
            return false;
        }
        out.file = site.substring(0, pos);
        String lineStr = site.substring(pos + 1);
        if (lineStr.isEmpty()) { err.append("site '").append(site).append("' missing line"); return false; }
        try {
            out.line = Integer.parseInt(lineStr);
        } catch (NumberFormatException e) {
            err.append("site '").append(site).append("' line is not a number"); return false;
        }
        if (out.line <= 0) { err.append("site '").append(site).append("' line must be >= 1"); return false; }
        return true;
    }

    // ── Dtype mapping (JDI primitive Type → wire string) ────────────────────
    private static String jdiPrimitiveToDtype(PrimitiveType t) {
        if (t instanceof ByteType)    return "i8";
        if (t instanceof ShortType)   return "i16";
        if (t instanceof IntegerType) return "i32";
        if (t instanceof LongType)    return "i64";
        if (t instanceof FloatType)   return "f32";
        if (t instanceof DoubleType)  return "f64";
        if (t instanceof CharType)    return "u16";    // Java char is unsigned 16-bit
        if (t instanceof BooleanType) return "u8";     // serialise as one byte 0/1
        return "";
    }

    private static int dtypeElemSize(String dtype) {
        switch (dtype) {
            case "i8":  case "u8":  return 1;
            case "i16": case "u16": return 2;
            case "i32": case "u32": case "f32": return 4;
            case "i64": case "u64": case "f64": return 8;
            default: return 0;
        }
    }

    // ── Recursive array walk: peels dimensions, captures shape ──────────────
    // For an `int[16][16]` ArrayReference: returns ("i32", [16, 16], 4).
    // For an `int[16]` ArrayReference: returns ("i32", [16], 4).
    // For a non-array value: returns the primitive dtype and empty shape.
    static final class LeafInfo {
        String dtype = "";
        List<Long> shape = new ArrayList<>();
        int elemBytes = 0;
    }

    /**
     * Read the leaf type + shape from a JDI value. For `int[][]` we walk both
     * dimensions and require every row to have the same length (rectangular).
     * Ragged Java arrays are unsupported (exit 4).
     */
    private static LeafInfo introspectShape(Value v, StringBuilder err) {
        LeafInfo info = new LeafInfo();
        if (v == null) { err.append("variable value is null"); return null; }

        // Walk array dimensions. JDI arrays are *references*, so we descend
        // into the first non-null element to find the inner type.
        Value current = v;
        while (current instanceof ArrayReference) {
            ArrayReference arr = (ArrayReference) current;
            int len = arr.length();
            info.shape.add((long) len);
            if (len == 0) {
                // 0-length array: try the static component type via signature.
                String sig = arr.referenceType().signature();
                // "[I" → primitive int; "[[I" → int[][]; we already consumed one '['.
                String inner = sig.substring(1);
                return finishFromInnerSig(inner, info, err);
            }
            // Read first row; ensure all rows are the same length (rectangular).
            List<Value> values = arr.getValues();
            Value first = values.get(0);
            if (first instanceof ArrayReference) {
                long firstLen = ((ArrayReference) first).length();
                for (int i = 1; i < values.size(); ++i) {
                    Value row = values.get(i);
                    if (!(row instanceof ArrayReference) ||
                        ((ArrayReference) row).length() != firstLen) {
                        err.append("ragged or non-rectangular array not supported");
                        return null;
                    }
                }
            }
            current = first;
        }

        if (current instanceof PrimitiveValue) {
            PrimitiveType pt = (PrimitiveType) current.type();
            String dtype = jdiPrimitiveToDtype(pt);
            if (dtype.isEmpty()) {
                err.append("primitive type ").append(pt.name()).append(" is unsupported");
                return null;
            }
            info.dtype = dtype;
            info.elemBytes = dtypeElemSize(dtype);
            return info;
        }

        err.append("variable has non-primitive leaf type: ")
           .append(current == null ? "<null>" : current.type().name());
        return null;
    }

    // If we hit a 0-length array we don't have an element to inspect; recover
    // the primitive from the type signature instead.
    private static LeafInfo finishFromInnerSig(String sig, LeafInfo info, StringBuilder err) {
        // Strip further '[' for nested arrays.
        while (sig.startsWith("[")) {
            // Without an example row we can't observe inner length; default to 0.
            info.shape.add(0L);
            sig = sig.substring(1);
        }
        String dtype = "";
        switch (sig) {
            case "B": dtype = "i8";  break;
            case "S": dtype = "i16"; break;
            case "I": dtype = "i32"; break;
            case "J": dtype = "i64"; break;
            case "F": dtype = "f32"; break;
            case "D": dtype = "f64"; break;
            case "C": dtype = "u16"; break;
            case "Z": dtype = "u8";  break;
            default:
                err.append("unsupported array element signature: ").append(sig);
                return null;
        }
        info.dtype = dtype;
        info.elemBytes = dtypeElemSize(dtype);
        return info;
    }

    // Pack a JDI array (possibly N-dim) into a flat row-major LE byte buffer.
    // Caller must have already validated via introspectShape().
    private static byte[] packArrayBytesLE(ArrayReference arr, LeafInfo info) {
        long totalElems = 1;
        for (Long d : info.shape) totalElems *= d;
        int total = Math.toIntExact(totalElems * info.elemBytes);
        ByteBuffer buf = ByteBuffer.allocate(total).order(ByteOrder.LITTLE_ENDIAN);
        flattenArray(arr, info, buf);
        return buf.array();
    }

    private static void flattenArray(ArrayReference arr, LeafInfo info, ByteBuffer buf) {
        List<Value> values = arr.getValues();
        for (Value v : values) {
            if (v instanceof ArrayReference) {
                flattenArray((ArrayReference) v, info, buf);
            } else {
                writePrimitiveLE(v, info.dtype, buf);
            }
        }
    }

    private static void writePrimitiveLE(Value v, String dtype, ByteBuffer buf) {
        // Scalars too — caller may pass a non-array value.
        switch (dtype) {
            case "i8":
                buf.put(((ByteValue) v).value());
                break;
            case "i16":
                buf.putShort(((ShortValue) v).value());
                break;
            case "i32":
                buf.putInt(((IntegerValue) v).value());
                break;
            case "i64":
                buf.putLong(((LongValue) v).value());
                break;
            case "f32":
                buf.putFloat(((FloatValue) v).value());
                break;
            case "f64":
                buf.putDouble(((DoubleValue) v).value());
                break;
            case "u16":
                buf.putShort((short) ((CharValue) v).value());
                break;
            case "u8":
                buf.put(((BooleanValue) v).value() ? (byte) 1 : (byte) 0);
                break;
            default:
                throw new IllegalStateException("unhandled dtype: " + dtype);
        }
    }

    // Pack a scalar primitive into its 1-/2-/4-/8-byte LE form.
    private static byte[] packScalarBytesLE(Value v, LeafInfo info) {
        ByteBuffer buf = ByteBuffer.allocate(info.elemBytes).order(ByteOrder.LITTLE_ENDIAN);
        writePrimitiveLE(v, info.dtype, buf);
        return buf.array();
    }

    // Row-major strides in bytes from a shape and elemBytes.
    private static long[] rowMajorStrides(List<Long> shape, int elemBytes) {
        long[] strides = new long[shape.size()];
        if (shape.isEmpty()) return strides;
        long acc = elemBytes;
        for (int i = shape.size() - 1; i >= 0; --i) {
            strides[i] = acc;
            acc *= shape.get(i);
        }
        return strides;
    }

    // ── Binary frame writer (see topo-core/include/topo/Debug/Ipc/BinaryFrame.h) ─
    private static final byte FRAME_VAR_BYTES = 0x01;
    private static final byte FRAME_LAYOUT_DESCRIPTOR = 0x02;

    private static void writeBinaryFrame(OutputStream out, byte type, byte[] payload)
            throws IOException {
        // Magic 'T','O','P','O' (byte-for-byte, *not* endian-swapped).
        out.write(new byte[]{'T', 'O', 'P', 'O'});
        out.write(type);
        out.write(0);            // flags
        out.write(0); out.write(0);  // reserved
        // frame_id u64 LE = 1
        ByteBuffer hdr = ByteBuffer.allocate(16).order(ByteOrder.LITTLE_ENDIAN);
        hdr.putLong(1L);
        hdr.putLong(payload.length);
        out.write(hdr.array());
        out.write(payload);
        out.flush();
    }

    private static void writeJsonLine(OutputStream out, String json) throws IOException {
        byte[] bytes = (json + "\n").getBytes(StandardCharsets.UTF_8);
        out.write(bytes);
        out.flush();
    }

    private static String layoutJson(String varName, String dtype, List<Long> shape, long[] strides) {
        StringBuilder sb = new StringBuilder();
        sb.append("{\"variable\":\"").append(escape(varName)).append("\"")
          .append(",\"dtype\":\"").append(escape(dtype)).append("\"")
          .append(",\"shape\":[");
        for (int i = 0; i < shape.size(); ++i) {
            if (i > 0) sb.append(',');
            sb.append(shape.get(i));
        }
        sb.append("],\"strides\":[");
        for (int i = 0; i < strides.length; ++i) {
            if (i > 0) sb.append(',');
            sb.append(strides[i]);
        }
        sb.append("]}");
        return sb.toString();
    }

    private static String escape(String s) {
        StringBuilder b = new StringBuilder(s.length());
        for (int i = 0; i < s.length(); ++i) {
            char c = s.charAt(i);
            switch (c) {
                case '"':  b.append("\\\""); break;
                case '\\': b.append("\\\\"); break;
                case '\n': b.append("\\n"); break;
                case '\r': b.append("\\r"); break;
                case '\t': b.append("\\t"); break;
                default:
                    if (c < 0x20) b.append(String.format("\\u%04x", (int) c));
                    else b.append(c);
            }
        }
        return b.toString();
    }

    // ── JVM launch + JDI attach ─────────────────────────────────────────────

    private static int die(int code, String msg) {
        System.err.println(PROG_NAME + ": " + msg);
        return code;
    }
    private static int dieUsage(String msg) {
        System.err.println(PROG_NAME + ": " + msg);
        printUsage(System.err);
        return EXIT_USAGE;
    }

    /**
     * Validate the `--target` argument before it reaches a subprocess.
     *
     * Two acceptable shapes:
     *   1. An existing executable file. When the optional
     *      {@code --launcher-allowlist <root>} (or
     *      {@code TOPO_DEBUG_JAVA_ALLOWED_ROOTS} env, ``File.pathSeparator``-
     *      delimited) is set, the canonical path MUST live under one of
     *      the allowlist roots — closes the "poisoned .topo-dbg.json
     *      picks an attacker launcher" vector.
     *   2. A bare Java main-class identifier matching the JLS grammar
     *      ``(Letter)(Letter|Digit)*`` joined by ``.``. Rejects leading
     *      ``-`` (so the value cannot be mistaken for a `java` flag) and
     *      any embedded control character.
     *
     * On reject, writes the diagnostic to {@code err} and returns
     * {@code null}, so the subprocess spawn never trusts an unvalidated
     * {@code --target} argument.
     */
    static Path validateTarget(String target, List<Path> allowedRoots, StringBuilder err) {
        if (target == null || target.isEmpty()) {
            err.append("--target is empty");
            return null;
        }
        for (int i = 0; i < target.length(); ++i) {
            char c = target.charAt(i);
            if (c == 0 || (c < 0x20 && c != '\t')) {
                err.append("--target contains an ASCII control character");
                return null;
            }
        }

        Path candidate;
        try {
            candidate = Paths.get(target);
        } catch (java.nio.file.InvalidPathException e) {
            err.append("--target is not a valid path: ").append(e.getMessage());
            return null;
        }
        if (Files.exists(candidate) && Files.isExecutable(candidate)) {
            Path real;
            try {
                real = candidate.toRealPath();
            } catch (IOException e) {
                err.append("cannot resolve --target: ").append(e.getMessage());
                return null;
            }
            if (allowedRoots != null && !allowedRoots.isEmpty()) {
                boolean ok = false;
                for (Path root : allowedRoots) {
                    Path r;
                    try {
                        r = root.toRealPath();
                    } catch (IOException e) {
                        continue;
                    }
                    if (real.startsWith(r)) { ok = true; break; }
                }
                if (!ok) {
                    err.append("--target ").append(real)
                       .append(" is outside the launcher allowlist (")
                       .append(allowedRoots).append(")");
                    return null;
                }
            }
            return real;
        }

        // Bare-classname branch — must be a JLS-valid fully-qualified
        // identifier. Rejects leading `-` (no flag injection), leading
        // digits, consecutive dots, etc.
        if (target.startsWith("-")) {
            err.append("--target ").append(target)
               .append(" cannot start with '-' (would be parsed as a flag)");
            return null;
        }
        int start = 0;
        while (true) {
            int dot = target.indexOf('.', start);
            String seg = (dot < 0) ? target.substring(start) : target.substring(start, dot);
            if (seg.isEmpty()) {
                err.append("--target ").append(target)
                   .append(" has empty segment (leading/consecutive/trailing '.')");
                return null;
            }
            char first = seg.charAt(0);
            if (!(Character.isLetter(first) || first == '_' || first == '$')) {
                err.append("--target segment '").append(seg)
                   .append("' must start with a letter, '_', or '$'");
                return null;
            }
            for (int i = 1; i < seg.length(); ++i) {
                char c = seg.charAt(i);
                if (!(Character.isLetterOrDigit(c) || c == '_' || c == '$')) {
                    err.append("--target segment '").append(seg)
                       .append("' contains invalid character '").append(c).append("'");
                    return null;
                }
            }
            if (dot < 0) break;
            start = dot + 1;
        }
        // Bare-classname branch returns null path; caller's launchTarget
        // handles the "no resolved launcher file" case as the
        // synthesise-java path. Diagnostic empty == accepted.
        return null;
    }

    /**
     * Read the launcher allowlist from the
     * {@code TOPO_DEBUG_JAVA_ALLOWED_ROOTS} env var (``File.pathSeparator``-
     * delimited). Empty / unset means "no allowlist" (back-compat) and
     * absolute-path launchers are accepted regardless of where they live.
     * Documented in topo-debug/README.md.
     */
    static List<Path> readAllowedLauncherRoots() {
        String raw = System.getenv("TOPO_DEBUG_JAVA_ALLOWED_ROOTS");
        if (raw == null || raw.isEmpty()) return java.util.Collections.emptyList();
        List<Path> roots = new ArrayList<>();
        for (String s : raw.split(java.util.regex.Pattern.quote(java.io.File.pathSeparator))) {
            if (s.isEmpty()) continue;
            roots.add(Paths.get(s));
        }
        return roots;
    }

    /**
     * Launch the target and wait for it to announce its JDWP listen port on
     * stderr. Two paths:
     *   * `--target` resolves to an existing file → exec it as a launcher; the
     *     launcher MUST spawn the target JVM with
     *     `-agentlib:jdwp=transport=dt_socket,server=y,suspend=y,address=127.0.0.1:0`
     *     and forward its stderr to ours so we can capture the port line.
     *   * Otherwise → treat `--target` as a Java main class and synthesise
     *     `java -agentlib:jdwp=... <ClassName>` from our own JAVA_HOME.
     *
     * Both branches now route through {@link #validateTarget}: the
     * launcher path is canonicalised + allowlist-checked, and the bare
     * classname is JLS-grammar-validated so a poisoned target value
     * cannot select an arbitrary launcher or inject a `java` flag.
     */
    private static Process launchTarget(String target, List<String> extraArgs,
                                        int[] portOut, StringBuilder err) throws IOException {
        List<String> cmd = new ArrayList<>();
        StringBuilder validateErr = new StringBuilder();
        Path validated = validateTarget(target, readAllowedLauncherRoots(), validateErr);
        if (validateErr.length() > 0) {
            err.append(validateErr);
            return null;
        }
        Path targetPath = (validated != null) ? validated : Paths.get(target);
        if (validated != null) {
            cmd.add(targetPath.toAbsolutePath().toString());
            cmd.addAll(extraArgs);
        } else {
            // Bare main-class fall-back. validateTarget already enforced
            // the JLS grammar so the value here cannot be a `java` flag.
            String javaHome = System.getProperty("java.home");
            String javaBin = javaHome + (isWindows() ? "/bin/java.exe" : "/bin/java");
            if (!Files.exists(Paths.get(javaBin))) javaBin = "java";
            cmd.add(javaBin);
            cmd.add("-agentlib:jdwp=transport=dt_socket,server=y,suspend=y,address=127.0.0.1:0");
            cmd.addAll(extraArgs);
            cmd.add(target);
        }

        ProcessBuilder pb = new ProcessBuilder(cmd);
        pb.redirectErrorStream(false);
        Process p = pb.start();

        // The JDWP agent prints `Listening for transport dt_socket at address: N`
        // on the *target JVM's stdout* (not stderr). We must read both streams:
        // stdout to capture the port + drain the "done" println after resume,
        // stderr to forward target chatter to our own stderr. Reading the port
        // synchronously on stdout is fine because the agent prints it before
        // any user code runs (suspend=y).
        BufferedReader childStdout = new BufferedReader(new InputStreamReader(p.getInputStream(), StandardCharsets.UTF_8));
        BufferedReader childStderr = new BufferedReader(new InputStreamReader(p.getErrorStream(), StandardCharsets.UTF_8));

        // Drain stderr unconditionally (it's not load-bearing) onto our stderr.
        Thread tErr = new Thread(() -> {
            try {
                String l;
                while ((l = childStderr.readLine()) != null) {
                    System.err.println("[target] " + l);
                }
            } catch (IOException ignore) {}
        }, "target-stderr-drain");
        tErr.setDaemon(true);
        tErr.start();

        Pattern pat = Pattern.compile(".*Listening for transport.*address:?\\s*(\\d+).*");
        long deadline = System.currentTimeMillis() + 15_000;
        String line;
        while ((line = childStdout.readLine()) != null) {
            if (System.currentTimeMillis() > deadline) {
                err.append("timeout waiting for JVM to announce JDWP port");
                p.destroyForcibly();
                return null;
            }
            Matcher m = pat.matcher(line);
            if (m.matches()) {
                portOut[0] = Integer.parseInt(m.group(1));
                // Spawn a drainer for the rest of stdout (the fixture's
                // println + any post-resume output). Discard so it doesn't
                // pollute our binary IPC stream.
                final BufferedReader rem = childStdout;
                Thread tOut = new Thread(() -> {
                    try {
                        String l;
                        while ((l = rem.readLine()) != null) {
                            // discard
                        }
                    } catch (IOException ignore) {}
                }, "target-stdout-drain");
                tOut.setDaemon(true);
                tOut.start();
                return p;
            }
            // Echo unrelated stdout lines onto our stderr (for debugging).
            System.err.println("[target] " + line);
        }
        err.append("JVM stdout closed before announcing JDWP port");
        return null;
    }

    private static boolean isWindows() {
        return System.getProperty("os.name", "").toLowerCase().startsWith("windows");
    }

    private static VirtualMachine attachJdwp(int port, StringBuilder err) {
        VirtualMachineManager vmm = Bootstrap.virtualMachineManager();
        AttachingConnector connector = null;
        for (AttachingConnector c : vmm.attachingConnectors()) {
            if ("com.sun.jdi.SocketAttach".equals(c.name())) {
                connector = c;
                break;
            }
        }
        if (connector == null) { err.append("no SocketAttach connector available"); return null; }
        Map<String, Connector.Argument> args = connector.defaultArguments();
        args.get("hostname").setValue("127.0.0.1");
        args.get("port").setValue(Integer.toString(port));
        try {
            return connector.attach(args);
        } catch (Exception e) {
            err.append("JDWP attach failed: ").append(e.toString());
            return null;
        }
    }

    public static void main(String[] argv) throws Exception {
        Args a = new Args();
        StringBuilder err = new StringBuilder();
        if (!parseArgs(argv, a, err)) { System.exit(dieUsage(err.toString())); return; }

        Site site = new Site();
        if (!splitSite(a.site, site, err)) { System.exit(dieUsage(err.toString())); return; }

        // ── Launch target JVM with JDWP ─────────────────────────────────────
        int[] portBox = new int[]{-1};
        StringBuilder lerr = new StringBuilder();
        Process targetProc;
        try {
            targetProc = launchTarget(a.target, a.targetArgs, portBox, lerr);
        } catch (IOException e) {
            System.exit(die(EXIT_LAUNCH, "failed to spawn target JVM: " + e.getMessage()));
            return;
        }
        if (targetProc == null) {
            System.exit(die(EXIT_LAUNCH, "target JVM launch failed: " + lerr));
            return;
        }

        // ── Attach JDI ──────────────────────────────────────────────────────
        VirtualMachine vm = attachJdwp(portBox[0], lerr);
        if (vm == null) {
            targetProc.destroyForcibly();
            System.exit(die(EXIT_LAUNCH, "JDI attach failed: " + lerr));
            return;
        }

        try {
            int rc = runAdapter(vm, a, site);
            // Clean up: target JVM exits naturally after we resume it.
            try { targetProc.waitFor(5, java.util.concurrent.TimeUnit.SECONDS); }
            catch (InterruptedException ignore) {}
            if (targetProc.isAlive()) targetProc.destroyForcibly();
            System.exit(rc);
        } catch (Throwable t) {
            t.printStackTrace(System.err);
            targetProc.destroyForcibly();
            System.exit(EXIT_RUNTIME);
        }
    }

    private static int runAdapter(VirtualMachine vm, Args a, Site site) throws Exception {
        EventRequestManager erm = vm.eventRequestManager();
        EventQueue queue = vm.eventQueue();

        // ── Set a "class prepared" request for files matching the site basename,
        // so we can install the breakpoint as soon as the class loads.
        ClassPrepareRequest cpr = erm.createClassPrepareRequest();
        // The fixture is in the default package — match all classes; we'll
        // filter by source file at delivery time. (Filtering by source name
        // is more reliable than by class name because the .java file basename
        // is what `--site file:line` gives us.)
        cpr.setSuspendPolicy(EventRequest.SUSPEND_ALL);
        cpr.enable();

        // The target was launched with suspend=y, so it's currently waiting on
        // the JDWP handshake completion. Resume so class loading begins.
        vm.resume();

        long deadline = System.currentTimeMillis() + BREAKPOINT_WAIT_MS;
        BreakpointRequest bp = null;
        BreakpointEvent bpEvent = null;

        outer:
        while (System.currentTimeMillis() < deadline) {
            long remaining = deadline - System.currentTimeMillis();
            EventSet set;
            try {
                set = queue.remove(Math.max(remaining, 50));
            } catch (InterruptedException ie) {
                continue;
            }
            if (set == null) continue;

            for (Event ev : set) {
                if (ev instanceof VMDeathEvent || ev instanceof VMDisconnectEvent) {
                    return die(EXIT_RUNTIME,
                               "target VM exited before breakpoint hit at " + a.site);
                }
                if (ev instanceof ClassPrepareEvent && bp == null) {
                    ClassPrepareEvent cpe = (ClassPrepareEvent) ev;
                    ReferenceType rt = cpe.referenceType();
                    // Match by source file name (basename only; --site may be a path).
                    String wantBasename = new File(site.file).getName();
                    try {
                        String src = rt.sourceName();
                        if (!src.equals(wantBasename)) {
                            continue;
                        }
                    } catch (AbsentInformationException ignore) {
                        continue;
                    }
                    // Skip nested / anonymous / hidden classes (e.g.,
                    // StringConcatFactory's invokedynamic-generated classes,
                    // `TinyMatrix$1`, etc.) — they often share the source file
                    // name and may even claim the same line numbers, but their
                    // frame has no LocalVariableTable for the variables the
                    // user actually declared. Stick to the top-level class.
                    String className = rt.name();
                    if (className.contains("$") || className.contains("/")) {
                        continue;
                    }
                    List<Location> locs = rt.locationsOfLine(site.line);
                    if (locs.isEmpty()) {
                        continue;
                    }
                    // Prefer a location inside a concrete (non-synthetic, non-native,
                    // non-abstract) method.
                    Location chosen = null;
                    for (Location l : locs) {
                        Method m = l.method();
                        if (!m.isAbstract() && !m.isNative() && !m.isSynthetic() &&
                            !m.isBridge()) {
                            chosen = l;
                            break;
                        }
                    }
                    if (chosen == null) chosen = locs.get(0);
                    bp = erm.createBreakpointRequest(chosen);
                    bp.setSuspendPolicy(EventRequest.SUSPEND_ALL);
                    bp.enable();
                    cpr.disable();
                }
                if (ev instanceof BreakpointEvent) {
                    bpEvent = (BreakpointEvent) ev;
                    break outer;
                }
            }
            set.resume();
        }

        if (bpEvent == null) {
            return die(EXIT_RUNTIME,
                       "timeout waiting for breakpoint hit (" + BREAKPOINT_WAIT_MS + " ms)");
        }

        ThreadReference thread = bpEvent.thread();
        thread.suspend();
        // Find the frame matching the breakpoint's location. Don't assume it
        // is frame(0): when HotSpot delivers a breakpoint that crosses an
        // invokedynamic (e.g., string concat's `makeConcatWithConstants` on
        // the line after our `--break` line), the breakpoint can be reported
        // while the thread is still inside the JIT/MethodHandle generation
        // helpers, so frame(0) is in `java.lang.invoke.*` or `jdk.internal.*`.
        // The user's frame is somewhere deeper in the stack; pick the first
        // one whose location matches bpEvent.location().method().
        Location bpLoc = bpEvent.location();
        StackFrame frame = null;
        int frameCount = thread.frameCount();
        for (int i = 0; i < frameCount; ++i) {
            StackFrame f = thread.frame(i);
            Location l = f.location();
            if (l.method().equals(bpLoc.method())) {
                frame = f;
                break;
            }
        }
        if (frame == null) {
            // Last-resort fallback — original frame(0) behaviour. This path
            // only triggers if the breakpoint fired in a method that has no
            // corresponding frame on the stack (shouldn't happen).
            frame = thread.frame(0);
        }
        OutputStream out = System.out;

        // breakpoint_hit JSON line
        String hit = "{\"kind\":\"breakpoint_hit\",\"frame\":1,\"site\":\"" +
                     escape(a.site) + "\"}";
        writeJsonLine(out, hit);

        // One (VarBytes, LayoutDescriptor) pair per --var entry.
        for (String varName : a.vars) {
            LocalVariable local = null;
            try {
                // First try the strict visibility check (scope-aware).
                local = frame.visibleVariableByName(varName);
            } catch (AbsentInformationException ignore) {
                // Method has no LocalVariableTable at all — fall through.
            }
            if (local == null) {
                // Fall back to method-wide variable list. JDI's visibility
                // semantics use start_pc/length intervals; on some JVM builds
                // the breakpoint PC sits right at start_pc where the variable
                // is technically "not yet visible" by the strict half-open
                // [start_pc, start_pc + length) check, even though the slot
                // is fully initialised. Picking the variable up directly from
                // method.variables() bypasses that off-by-one.
                try {
                    for (LocalVariable lv : frame.location().method().variables()) {
                        if (lv.name().equals(varName)) {
                            local = lv;
                            break;
                        }
                    }
                } catch (AbsentInformationException ignore) {
                    // No debug info at all.
                }
            }
            if (local == null) {
                StringBuilder names = new StringBuilder();
                Location frameLoc = frame.location();
                try {
                    for (LocalVariable lv : frameLoc.method().variables()) {
                        if (names.length() > 0) names.append(", ");
                        names.append(lv.name());
                    }
                } catch (AbsentInformationException ignore) {
                    names.append("<no debug info>");
                }
                return die(EXIT_RUNTIME,
                           "variable '" + varName + "' not in scope at " + a.site +
                           " (frame method: " + frameLoc.declaringType().name() +
                           "." + frameLoc.method().name() + " bytecode=" + frameLoc.codeIndex() +
                           " line=" + frameLoc.lineNumber() +
                           ", method locals: " + names + ")");
            }
            Value v = frame.getValue(local);
            StringBuilder ierr = new StringBuilder();
            LeafInfo info = introspectShape(v, ierr);
            if (info == null) {
                return die(EXIT_UNSUPPORTED_TYPE,
                           "variable '" + varName + "': " + ierr);
            }
            byte[] payload;
            if (v instanceof ArrayReference) {
                payload = packArrayBytesLE((ArrayReference) v, info);
            } else if (v instanceof PrimitiveValue) {
                payload = packScalarBytesLE(v, info);
            } else {
                return die(EXIT_UNSUPPORTED_TYPE,
                           "variable '" + varName + "' has unsupported value class: " +
                           v.getClass().getSimpleName());
            }
            writeBinaryFrame(out, FRAME_VAR_BYTES, payload);
            long[] strides = rowMajorStrides(info.shape, info.elemBytes);
            String layout = layoutJson(varName, info.dtype, info.shape, strides);
            writeBinaryFrame(out, FRAME_LAYOUT_DESCRIPTOR,
                             layout.getBytes(StandardCharsets.UTF_8));
        }

        // Wait for control-plane continue.
        BufferedReader stdin = new BufferedReader(new InputStreamReader(System.in, StandardCharsets.UTF_8));
        String line;
        while ((line = stdin.readLine()) != null) {
            if (line.contains("\"continue\"")) break;
        }

        // Resume; wait for VM exit.
        vm.resume();
        try {
            // Drain the event queue briefly so VMDeath/VMDisconnect actually
            // arrives without polluting subsequent runs.
            long endBy = System.currentTimeMillis() + 5_000;
            while (System.currentTimeMillis() < endBy) {
                EventSet s = queue.remove(200);
                if (s == null) continue;
                boolean exited = false;
                for (Event e : s) {
                    if (e instanceof VMDeathEvent || e instanceof VMDisconnectEvent) {
                        exited = true;
                    }
                }
                s.resume();
                if (exited) break;
            }
        } catch (VMDisconnectedException ignore) {
            // Already gone; that's the happy path.
        }
        return EXIT_OK;
    }
}
