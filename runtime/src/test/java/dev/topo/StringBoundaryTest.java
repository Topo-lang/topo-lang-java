package dev.topo;

import org.junit.jupiter.api.Test;
import static org.junit.jupiter.api.Assertions.*;

import java.nio.charset.StandardCharsets;

class StringBoundaryTest {
    @Test
    void roundTripAscii() {
        String original = "hello topo";
        byte[] utf8 = StringBoundary.toTopo(original);
        // ASCII bytes == UTF-8 bytes == Java string length
        assertArrayEquals(original.getBytes(StandardCharsets.US_ASCII), utf8);
        assertEquals(original, StringBoundary.fromTopo(utf8));
    }

    @Test
    void roundTripMultibyte() {
        // BMP + supplementary (emoji) code points force multi-byte UTF-8
        // sequences and a Java surrogate pair on the UTF-16 side. A clean
        // round-trip proves the transcoder is not silently truncating or
        // mis-encoding. Non-ASCII literals are written as Java unicode
        // escapes so the source file stays ASCII-only while the runtime
        // test data still exercises BMP + supplementary code points
        // (U+00E9, U+4E16, U+754C, and the U+1F680 surrogate pair).
        String original = "h\u00e9llo \u4e16\u754c \ud83d\ude80";
        byte[] utf8 = StringBoundary.toTopo(original);
        assertEquals(original, StringBoundary.fromTopo(utf8));
    }

    @Test
    void emptyString() {
        byte[] utf8 = StringBoundary.toTopo("");
        assertEquals(0, utf8.length);
        assertEquals("", StringBoundary.fromTopo(utf8));
    }
}
