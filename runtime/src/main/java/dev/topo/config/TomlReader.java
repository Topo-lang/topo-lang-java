package dev.topo.config;

import java.util.ArrayList;
import java.util.LinkedHashMap;
import java.util.List;
import java.util.Map;

/**
 * A minimal, deterministic TOML reader for the product-config vocabulary.
 *
 * <p>The layered model is language-agnostic and never touches files. This is
 * part of the host (Java) bridge. The JDK ships no TOML parser and the repo
 * vendors none, so — mirroring the Python bridge's "no hard third-party
 * dependency, ship a minimal reader/writer" stance — this hand-rolled reader
 * covers exactly the scalar / array / table / dotted-key / inline-table
 * vocabulary the model accepts, which is also exactly what {@link TomlWriter}
 * emits. It is intentionally <em>not</em> a full TOML 1.0 implementation: it
 * is the symmetric inverse of the writer over the supported value set, which
 * is what the round-trip contract requires.
 *
 * <p>Decoded scalars normalise to {@code String}, {@code Long}, {@code Double}
 * and {@code Boolean}; arrays to {@code List}; tables and inline tables to
 * {@code LinkedHashMap} — the plain shape the model consumes. Dates/times are
 * intentionally not parsed: the model rejects unbridged temporal values, and
 * the writer never emits them.
 */
public final class TomlReader {

    private TomlReader() {
    }

    /** Parse TOML text into a nested {@code Map} document. */
    public static Map<String, Object> parse(String text) {
        Map<String, Object> root = new LinkedHashMap<>();
        Map<String, Object> current = root;
        for (String rawLine : text.split("\n", -1)) {
            String line = stripComment(rawLine).trim();
            if (line.isEmpty()) {
                continue;
            }
            if (line.startsWith("[")) {
                if (!line.endsWith("]")) {
                    throw new IllegalArgumentException(
                            "malformed table header: " + line);
                }
                String path = line.substring(1, line.length() - 1).trim();
                current = descendToTable(root, path);
                continue;
            }
            int eq = topLevelEquals(line);
            if (eq < 0) {
                throw new IllegalArgumentException(
                        "expected key = value, got: " + line);
            }
            String key = unquoteKey(line.substring(0, eq).trim());
            String valueText = line.substring(eq + 1).trim();
            current.put(key, parseValue(valueText));
        }
        return root;
    }

    private static String stripComment(String line) {
        boolean inString = false;
        for (int i = 0; i < line.length(); i++) {
            char c = line.charAt(i);
            if (c == '"') {
                // A quote not escaped by a preceding backslash toggles state.
                int back = 0;
                for (int j = i - 1; j >= 0 && line.charAt(j) == '\\'; j--) {
                    back++;
                }
                if (back % 2 == 0) {
                    inString = !inString;
                }
            } else if (c == '#' && !inString) {
                return line.substring(0, i);
            }
        }
        return line;
    }

    @SuppressWarnings("unchecked")
    private static Map<String, Object> descendToTable(
            Map<String, Object> root, String dottedPath) {
        Map<String, Object> cursor = root;
        for (String part : splitDotted(dottedPath)) {
            Object next = cursor.get(part);
            if (next == null) {
                Map<String, Object> table = new LinkedHashMap<>();
                cursor.put(part, table);
                cursor = table;
            } else if (next instanceof Map) {
                cursor = (Map<String, Object>) next;
            } else {
                throw new IllegalArgumentException(
                        "table path '" + dottedPath
                                + "' collides with a scalar value");
            }
        }
        return cursor;
    }

    private static List<String> splitDotted(String path) {
        List<String> parts = new ArrayList<>();
        StringBuilder buf = new StringBuilder();
        boolean inQuote = false;
        for (int i = 0; i < path.length(); i++) {
            char c = path.charAt(i);
            if (c == '"') {
                inQuote = !inQuote;
            } else if (c == '.' && !inQuote) {
                parts.add(unquoteKey(buf.toString().trim()));
                buf.setLength(0);
            } else {
                buf.append(c);
            }
        }
        parts.add(unquoteKey(buf.toString().trim()));
        return parts;
    }

    private static String unquoteKey(String key) {
        if (key.length() >= 2 && key.startsWith("\"") && key.endsWith("\"")) {
            return unescape(key.substring(1, key.length() - 1));
        }
        return key;
    }

    private static int topLevelEquals(String line) {
        boolean inString = false;
        int depth = 0;
        for (int i = 0; i < line.length(); i++) {
            char c = line.charAt(i);
            if (c == '"') {
                int back = 0;
                for (int j = i - 1; j >= 0 && line.charAt(j) == '\\'; j--) {
                    back++;
                }
                if (back % 2 == 0) {
                    inString = !inString;
                }
            } else if (!inString) {
                if (c == '[' || c == '{') {
                    depth++;
                } else if (c == ']' || c == '}') {
                    depth--;
                } else if (c == '=' && depth == 0) {
                    return i;
                }
            }
        }
        return -1;
    }

    static Object parseValue(String text) {
        String t = text.trim();
        if (t.isEmpty()) {
            throw new IllegalArgumentException("empty value");
        }
        if (t.equals("true")) {
            return Boolean.TRUE;
        }
        if (t.equals("false")) {
            return Boolean.FALSE;
        }
        if (t.startsWith("\"")) {
            return parseString(t);
        }
        if (t.startsWith("[")) {
            return parseArray(t);
        }
        if (t.startsWith("{")) {
            return parseInlineTable(t);
        }
        return parseNumber(t);
    }

    private static String parseString(String t) {
        if (t.length() < 2 || !t.endsWith("\"")) {
            throw new IllegalArgumentException("unterminated string: " + t);
        }
        return unescape(t.substring(1, t.length() - 1));
    }

    private static String unescape(String s) {
        StringBuilder out = new StringBuilder();
        for (int i = 0; i < s.length(); i++) {
            char c = s.charAt(i);
            if (c == '\\' && i + 1 < s.length()) {
                char n = s.charAt(++i);
                switch (n) {
                    case 'n' -> out.append('\n');
                    case 't' -> out.append('\t');
                    case 'r' -> out.append('\r');
                    case '"' -> out.append('"');
                    case '\\' -> out.append('\\');
                    default -> out.append(n);
                }
            } else {
                out.append(c);
            }
        }
        return out.toString();
    }

    private static Object parseNumber(String t) {
        // A float has a '.' or an exponent; otherwise it is an integer. The
        // model normalises TOML integer -> i64 (Long), float -> f64 (Double).
        if (t.indexOf('.') >= 0 || t.indexOf('e') >= 0
                || t.indexOf('E') >= 0) {
            return Double.parseDouble(t);
        }
        return Long.parseLong(t);
    }

    private static List<Object> parseArray(String t) {
        if (!t.endsWith("]")) {
            throw new IllegalArgumentException("unterminated array: " + t);
        }
        List<Object> out = new ArrayList<>();
        for (String element : splitTopLevel(t.substring(1, t.length() - 1))) {
            String e = element.trim();
            if (!e.isEmpty()) {
                out.add(parseValue(e));
            }
        }
        return out;
    }

    private static Map<String, Object> parseInlineTable(String t) {
        if (!t.endsWith("}")) {
            throw new IllegalArgumentException(
                    "unterminated inline table: " + t);
        }
        Map<String, Object> out = new LinkedHashMap<>();
        for (String pair : splitTopLevel(t.substring(1, t.length() - 1))) {
            String p = pair.trim();
            if (p.isEmpty()) {
                continue;
            }
            int eq = topLevelEquals(p);
            if (eq < 0) {
                throw new IllegalArgumentException(
                        "malformed inline table entry: " + p);
            }
            out.put(unquoteKey(p.substring(0, eq).trim()),
                    parseValue(p.substring(eq + 1).trim()));
        }
        return out;
    }

    private static List<String> splitTopLevel(String body) {
        List<String> parts = new ArrayList<>();
        StringBuilder buf = new StringBuilder();
        boolean inString = false;
        int depth = 0;
        for (int i = 0; i < body.length(); i++) {
            char c = body.charAt(i);
            if (c == '"') {
                int back = 0;
                for (int j = i - 1; j >= 0 && body.charAt(j) == '\\'; j--) {
                    back++;
                }
                if (back % 2 == 0) {
                    inString = !inString;
                }
                buf.append(c);
            } else if (!inString && (c == '[' || c == '{')) {
                depth++;
                buf.append(c);
            } else if (!inString && (c == ']' || c == '}')) {
                depth--;
                buf.append(c);
            } else if (!inString && c == ',' && depth == 0) {
                parts.add(buf.toString());
                buf.setLength(0);
            } else {
                buf.append(c);
            }
        }
        if (!buf.isEmpty()) {
            parts.add(buf.toString());
        }
        return parts;
    }
}
