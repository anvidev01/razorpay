package com.razorpay.rig;

/** One decoded WAL record. Mirrors the C++ layout in engine/include/rig/wal.hpp. */
public final class WalRecord {
    public static final int HEADER_BYTES = 36;
    public static final int HASH_BYTES   = 32;

    public final long   len, seq, wallNs, monoNs;
    public final int    crc;
    public final int    type, version, flags;
    public final byte[] payload, prevHash, thisHash;

    WalRecord(long len, int crc, long seq, long wallNs, long monoNs,
              int type, int version, int flags,
              byte[] payload, byte[] prevHash, byte[] thisHash) {
        this.len = len; this.crc = crc; this.seq = seq;
        this.wallNs = wallNs; this.monoNs = monoNs;
        this.type = type; this.version = version; this.flags = flags;
        this.payload = payload; this.prevHash = prevHash; this.thisHash = thisHash;
    }

    public String typeName() {
        return switch (type) {
            case 1 -> "MANDATE_ISSUED";    case 2 -> "CART_PROPOSED";
            case 3 -> "POLICY_DECISION";   case 4 -> "CAPABILITY_ISSUED";
            case 5 -> "CAPABILITY_DENIED"; case 6 -> "PAYMENT_ATTEMPTED";
            case 7 -> "PAYMENT_RESULT";    case 8 -> "REMEDIATION";
            case 9 -> "ANCHOR";            default -> "UNKNOWN(" + type + ")";
        };
    }
}
