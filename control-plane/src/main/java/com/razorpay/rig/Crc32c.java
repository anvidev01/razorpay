package com.razorpay.rig;

/** CRC32C (Castagnoli, reflected). Independent implementation -- if it disagrees with
 *  the C++ writer, the format is under-specified and the audit trail is not portable. */
final class Crc32c {
    private static final int[] T = new int[256];
    static {
        for (int i = 0; i < 256; i++) {
            int c = i;
            for (int k = 0; k < 8; k++) c = ((c & 1) != 0) ? (0x82F63B78 ^ (c >>> 1)) : (c >>> 1);
            T[i] = c;
        }
    }
    static int compute(byte[] b, int off, int len) {
        int c = 0xFFFFFFFF;
        for (int i = off; i < off + len; i++) c = T[(c ^ b[i]) & 0xFF] ^ (c >>> 8);
        return c ^ 0xFFFFFFFF;
    }
    private Crc32c() {}
}
