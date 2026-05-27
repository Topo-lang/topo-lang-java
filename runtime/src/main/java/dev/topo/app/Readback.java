package dev.topo.app;

import java.io.IOException;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.nio.file.Path;
import java.util.ArrayList;
import java.util.List;
import java.util.regex.Matcher;
import java.util.regex.Pattern;

/**
 * Read {@code .topo} back into a {@link Graph} by parsing it with the real
 * toolchain.
 *
 * <p>Round-trip fidelity is the decisive constraint. To prove it honestly,
 * read-back goes through the actual {@code topo --ast-dump} parser, not a Java
 * re-implementation of the grammar (which could agree with the emitter by
 * accident). The dump only succeeds if the fresh parser accepts the source, so
 * a successful read simultaneously proves "emitted {@code .topo} parses under
 * the merged grammar" and yields graph' for the equivalence check.
 */
public final class Readback {

    private static final Pattern NS =
            Pattern.compile("NamespaceDecl '(\\w+)'");
    private static final Pattern HANDLER =
            Pattern.compile("HandlerDecl '(\\w+)\\((.*?)\\)\\s*->\\s*(.+)'");
    private static final Pattern FLOW =
            Pattern.compile("FlowBlock '(\\w+)'");
    private static final Pattern EDGE =
            Pattern.compile(
                    "Edge (\\w+) -> (\\w+)(?:\\s*\\[terminal\\])?");

    private Readback() {
    }

    /**
     * Parse {@code .topo} source text into a Graph via {@code
     * topo --ast-dump}. Throws if the toolchain rejects the source — that
     * rejection is itself the grammar-conformance signal.
     */
    public static Graph read(String text) {
        Path tmp = null;
        try {
            tmp = Files.createTempFile("roundtrip", ".topo");
            Files.writeString(tmp, text, StandardCharsets.UTF_8);
            Process proc = new ProcessBuilder(
                    Toolchain.topoBin().toString(),
                    "--ast-dump",
                    tmp.toString())
                    .redirectErrorStream(false)
                    .start();
            String stdout = new String(
                    proc.getInputStream().readAllBytes(),
                    StandardCharsets.UTF_8);
            String stderr = new String(
                    proc.getErrorStream().readAllBytes(),
                    StandardCharsets.UTF_8);
            int code = proc.waitFor();
            if (code != 0) {
                throw new IllegalStateException(
                        "topo --ast-dump rejected the emitted .topo (exit "
                                + code + "):\n" + stdout + "\n" + stderr);
            }
            return parseDump(stdout);
        } catch (IOException | InterruptedException e) {
            if (e instanceof InterruptedException) {
                Thread.currentThread().interrupt();
            }
            throw new IllegalStateException("read-back failed", e);
        } finally {
            if (tmp != null) {
                try {
                    Files.deleteIfExists(tmp);
                } catch (IOException ignored) {
                    // temp cleanup best-effort
                }
            }
        }
    }

    private static Graph parseDump(String dump) {
        String namespace = "";
        List<Handler> handlers = new ArrayList<>();
        Flow flow = null;
        for (String raw : dump.split("\n", -1)) {
            String s = raw.strip();
            Matcher m = NS.matcher(s);
            if (m.find() && m.start() == 0) {
                namespace = m.group(1);
                continue;
            }
            m = HANDLER.matcher(s);
            if (m.find() && m.start() == 0) {
                String name = m.group(1);
                String params = m.group(2).strip();
                String ret = m.group(3).strip();
                TypeRef in = null;
                if (!params.isEmpty()) {
                    // "Type in" — strip the conventional parameter name. The
                    // type itself may contain spaces (record<...>), so split
                    // off only the last whitespace-delimited token.
                    int lastWs = params.lastIndexOf(' ');
                    String typeSpec = lastWs < 0
                            ? params
                            : params.substring(0, lastWs);
                    in = parseType(typeSpec);
                }
                handlers.add(new Handler(name, in, parseType(ret)));
                continue;
            }
            m = FLOW.matcher(s);
            if (m.find() && m.start() == 0) {
                flow = new Flow(m.group(1));
                continue;
            }
            m = EDGE.matcher(s);
            if (flow != null && m.find() && m.start() == 0) {
                String src = m.group(1);
                String tgt = m.group(2);
                flow.addEdge(
                        new Edge(src, "void".equals(tgt) ? null : tgt));
            }
        }
        Graph g = new Graph(namespace);
        g.handlers().addAll(handlers);
        g.setFlow(flow);
        return g;
    }

    private static TypeRef parseType(String spec) {
        String s = spec.strip();
        if (s.startsWith("record<") && s.endsWith(">")) {
            String inner = s.substring("record<".length(), s.length() - 1);
            List<TypeRef.Field> fields = new ArrayList<>();
            // Record fields here are scalar-typed (one level of nesting,
            // matching the slice's order example), so a comma split is
            // sufficient.
            for (String part : inner.split(",")) {
                int colon = part.indexOf(':');
                String fname = part.substring(0, colon).strip();
                String ftype = part.substring(colon + 1).strip();
                fields.add(new TypeRef.Field(fname, TypeRef.scalar(ftype)));
            }
            return TypeRef.record(fields);
        }
        return TypeRef.scalar(s);
    }
}
