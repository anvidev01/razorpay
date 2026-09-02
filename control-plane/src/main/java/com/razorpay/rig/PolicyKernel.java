package com.razorpay.rig;

/**
 * An INDEPENDENT reimplementation of the C++ policy kernel
 * (engine/src/kernel.cpp), written from the same specification.
 *
 * This is not a wrapper and not a binding. Re-running the C++ engine over its own log
 * only proves the code is self-consistent. A second implementation, in another language,
 * reaching the identical verdict on every historical decision proves the POLICY is
 * well-specified -- that the audit trail means something independent of the binary that
 * produced it. If these two ever disagree, the log tells you immediately.
 */
public final class PolicyKernel {

    public static final int R_NONE                 = 0;
    public static final int R_SKU_NOT_IN_INTENT    = 1;
    public static final int R_QTY_EXCEEDED         = 1 << 1;
    public static final int R_UNIT_PRICE_EXCEEDED  = 1 << 2;
    public static final int R_CART_TOTAL_EXCEEDED  = 1 << 3;
    public static final int R_MERCHANT_NOT_ALLOWED = 1 << 4;
    public static final int R_MANDATE_EXPIRED      = 1 << 5;
    public static final int R_ARITH_OVERFLOW       = 1 << 6;
    public static final int R_REPLAY_NONCE         = 1 << 7;
    public static final int R_SCHEMA_VERSION       = 1 << 8;
    public static final int R_ENGINE_RESOURCE      = 1 << 9;
    public static final int SCHEMA_VER             = 1;
    public static final int MAXC                   = 16;
    public static final int MAX_CART               = 64;

    public static String[] names(int bits) {
        String[] all = {"R_SKU_NOT_IN_INTENT","R_QTY_EXCEEDED","R_UNIT_PRICE_EXCEEDED",
                        "R_CART_TOTAL_EXCEEDED","R_MERCHANT_NOT_ALLOWED","R_MANDATE_EXPIRED",
                        "R_ARITH_OVERFLOW","R_REPLAY_NONCE","R_SCHEMA_VERSION","R_ENGINE_RESOURCE"};
        int n = Integer.bitCount(bits);
        String[] out = new String[n];
        int k = 0;
        for (int i = 0; i < all.length; i++) if ((bits & (1 << i)) != 0) out[k++] = all[i];
        return out;
    }

    /** Result of one evaluation. */
    public static final class Result {
        public final int bits; public final long total;
        Result(int bits, long total) { this.bits = bits; this.total = total; }
    }

    private static int findConstraint(DecisionPayload d, int sku) {
        int found = -1;
        for (int i = 0; i < MAXC; i++)
            if (i < d.nConstraints && d.constraintSku[i] == sku) found = i;
        return found;
    }

    public static Result evaluate(DecisionPayload d) {
        int bits = 0;

        if (d.schemaVersion != SCHEMA_VER) bits |= R_SCHEMA_VERSION;
        // unsigned comparison: the C++ side uses uint64 nanoseconds
        if (Long.compareUnsigned(d.nowNs, d.notBeforeNs) < 0
         || Long.compareUnsigned(d.nowNs, d.notAfterNs)  > 0) bits |= R_MANDATE_EXPIRED;
        if (d.merchantId == 0 || d.merchantId > 64
         || ((d.merchantAllowMask >>> (d.merchantId - 1)) & 1L) == 0) bits |= R_MERCHANT_NOT_ALLOWED;
        if (d.nLines > MAX_CART) bits |= R_ENGINE_RESOURCE;

        long total = 0;
        int n = Math.min(d.nLines, MAX_CART);
        for (int i = 0; i < n; i++) {
            int  sku = d.sku[i];
            long up  = d.unitPaise[i];
            long q   = Integer.toUnsignedLong(d.qty[i]);

            int ci = findConstraint(d, sku);
            if (ci < 0) bits |= R_SKU_NOT_IN_INTENT;
            if (ci >= 0 && q > Integer.toUnsignedLong(d.constraintQty[ci])) bits |= R_QTY_EXCEEDED;
            // two independent unit-price bounds; the second applies even to an unknown SKU
            if ((ci >= 0 && up > d.constraintUnit[ci]) || up > d.totalBudgetPaise)
                bits |= R_UNIT_PRICE_EXCEEDED;
            if (up < 0 || q == 0) bits |= R_SCHEMA_VERSION;

            try {
                long line = Math.multiplyExact(up, q);
                total = Math.addExact(total, line);
            } catch (ArithmeticException e) {
                bits |= R_ARITH_OVERFLOW;
            }
        }
        if (total > d.totalBudgetPaise) bits |= R_CART_TOTAL_EXCEEDED;
        return new Result(bits, total);
    }

    private PolicyKernel() {}
}
