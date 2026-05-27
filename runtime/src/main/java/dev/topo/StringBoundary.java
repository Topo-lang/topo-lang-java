package dev.topo;

import java.nio.charset.StandardCharsets;

/**
 * UTF-8 ↔ UTF-16 transcoding at the Topo Functor boundary.
 *
 * <p>The cross-language {@code string} ABI is fixed as UTF-8
 * length-prefixed bytes ({@code {u32 len_bytes, u8* utf8_ptr}}, 16 B on a
 * 64-bit platform). Java's {@code String} is UTF-16 internally, so any
 * Functor boundary that exchanges {@code string} values must transcode
 * once on the way in and once on the way out. This class provides the
 * minimum transcoding helpers; emitter-generated boundary glue lands in
 * a later batch — for now the helpers are callable directly by user code
 * that constructs the boundary explicitly.</p>
 *
 * <p>Cost is one allocation per call (the transcoded byte[] or String).
 * For high-frequency boundary calls, callers should reuse buffers or
 * batch transcoding outside this helper.</p>
 */
public final class StringBoundary {
    private StringBoundary() {}

    /**
     * Decode UTF-8 bytes from the Topo boundary into a Java UTF-16 String.
     *
     * @param utf8 UTF-8 encoded bytes ({@code u8*} side of the boundary layout);
     *             must not be {@code null}
     * @return the decoded String
     */
    public static String fromTopo(byte[] utf8) {
        return new String(utf8, StandardCharsets.UTF_8);
    }

    /**
     * Encode a Java UTF-16 String as UTF-8 bytes for the Topo boundary.
     *
     * @param s the String to encode; must not be {@code null}
     * @return UTF-8 encoded bytes whose {@code length} populates the
     *         {@code u32 len_bytes} field of the boundary layout
     */
    public static byte[] toTopo(String s) {
        return s.getBytes(StandardCharsets.UTF_8);
    }
}
