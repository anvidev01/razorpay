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
    public static final int R_SUBSTITUTION_DENIED  = 1 << 10;
    public static final int R_SUBSTITUTION_DELTA   = 1 << 11;
    public static final int R_INJECTION_SUSPECTED  = 1 << 12;
    public static final int R_DUPLICATE_CHARGE     = 1 << 13;
    public static final int R_VELOCITY_ANOMALY     = 1 << 14;
    public static final int R_NEW_MERCHANT         = 1 << 15;
    public static final int R_ODD_HOUR             = 1 << 16;
    public static final int R_MANDATE_UNKNOWN      = 1 << 17;
    public static final int R_REVERSAL_UNAUTHORISED = 1 << 18;
    public static final int R_REVERSAL_EXCEEDS      = 1 << 19;
    public static final int R_REVERSAL_NO_PAYMENT   = 1 << 20;
    public static final int R_REVERSAL_DUPLICATE    = 1 << 21;
    public static final int R_SUBSCRIPTION_UNDISCLOSED = 1 << 22;
    public static final int R_RECURRING_EXCEEDS        = 1 << 23;
    public static final int SUBST_DENY = 0, SUBST_SAME_CATEGORY = 1, SUBST_ANY_IN_BUDGET = 2;

    /**
     * Bits derived from cross-transaction state rather than from one record's inputs.
     * The kernel is a pure function of a single decision, so it cannot re-derive these;
     * the auditor excludes them or it would report false divergences. Injection is NOT
     * here -- it is computed from this cart's own text and travels in the payload.
     */
    public static final int STATEFUL_RISK_MASK =
            R_VELOCITY_ANOMALY | R_NEW_MERCHANT | R_ODD_HOUR;
    public static final int SCHEMA_VER             = 1;
    public static final int MAXC                   = 16;
    public static final int MAX_CART               = 64;

    public static String[] names(int bits) {
        String[] all = {"R_SKU_NOT_IN_INTENT","R_QTY_EXCEEDED","R_UNIT_PRICE_EXCEEDED",
                        "R_CART_TOTAL_EXCEEDED","R_MERCHANT_NOT_ALLOWED","R_MANDATE_EXPIRED",
                        "R_ARITH_OVERFLOW","R_REPLAY_NONCE","R_SCHEMA_VERSION","R_ENGINE_RESOURCE",
                        "R_SUBSTITUTION_DENIED","R_SUBSTITUTION_DELTA","R_INJECTION_SUSPECTED",
                        "R_DUPLICATE_CHARGE","R_VELOCITY_ANOMALY","R_NEW_MERCHANT",
                        "R_ODD_HOUR","R_MANDATE_UNKNOWN","R_REVERSAL_UNAUTHORISED",
                        "R_REVERSAL_EXCEEDS","R_REVERSAL_NO_PAYMENT",
                        "R_REVERSAL_DUPLICATE","R_SUBSCRIPTION_UNDISCLOSED",
                        "R_RECURRING_EXCEEDS"};
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

    private static int findByCategory(DecisionPayload d, int cat) {
        if (cat == 0) return -1;
        int found = -1;
        for (int i = 0; i < MAXC; i++)
            if (i < d.nConstraints && d.constraintCat[i] == cat) found = i;
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
        bits |= (d.textFlags & R_INJECTION_SUSPECTED);

        long total = 0;
        long[] aggQty = new long[MAXC];
        int n = Math.min(d.nLines, MAX_CART);
        for (int i = 0; i < n; i++) {
            int  sku = d.sku[i];
            long up  = d.unitPaise[i];
            long q   = Integer.toUnsignedLong(d.qty[i]);

            // A recurring commitment sits outside the cart total, which is exactly how
            // a 1-rupee trial that becomes 999 a month passes every price cap.
            long rec = d.recurring[i];
            if (rec > 0 && d.allowRecurring == 0) bits |= R_SUBSCRIPTION_UNDISCLOSED;
            if (rec > 0 && d.allowRecurring != 0 && rec > d.maxRecurringPaise)
                bits |= R_RECURRING_EXCEEDS;

            int cat = d.category[i];
            int ci  = findConstraint(d, sku);
            int sub = (ci < 0 && d.substPolicy == SUBST_SAME_CATEGORY)
                        ? findByCategory(d, cat) : -1;
            boolean anyOk = (ci < 0 && d.substPolicy == SUBST_ANY_IN_BUDGET);
            int eff  = ci >= 0 ? ci : sub;
            int safe = eff < 0 ? 0 : eff;

            if (ci < 0 && sub < 0 && !anyOk) bits |= R_SKU_NOT_IN_INTENT;
            if (ci < 0 && sub < 0 && d.substPolicy == SUBST_SAME_CATEGORY)
                bits |= R_SUBSTITUTION_DENIED;
            long cap     = d.constraintUnit[safe];
            long ceiling = cap + (cap * d.substMaxDeltaBp) / 10000;
            if (ci < 0 && sub >= 0 && up > ceiling) bits |= R_SUBSTITUTION_DELTA;

            // AGGREGATE, not per line. The agent chooses how many lines it sends, so a
            // per-line check lets ten lines of one item buy ten of a max-one item. The
            // C++ kernel was fixed for this; this auditor was not, and the two silently
            // disagreed on exactly the attack the fix exists to stop.
            if (eff >= 0) aggQty[safe] += q;
            // two independent unit-price bounds; the second applies even to an unknown SKU
            if ((ci >= 0 && up > d.constraintUnit[safe]) || up > d.totalBudgetPaise)
                bits |= R_UNIT_PRICE_EXCEEDED;
            if (up < 0 || q == 0) bits |= R_SCHEMA_VERSION;

            try {
                long line = Math.multiplyExact(up, q);
                total = Math.addExact(total, line);
            } catch (ArithmeticException e) {
                bits |= R_ARITH_OVERFLOW;
            }
        }
        for (int ci = 0; ci < MAXC; ci++)
            if (ci < d.nConstraints
                && aggQty[ci] > Integer.toUnsignedLong(d.constraintQty[ci])) bits |= R_QTY_EXCEEDED;

        if (total > d.totalBudgetPaise) bits |= R_CART_TOTAL_EXCEEDED;
        return new Result(bits, total);
    }

    private PolicyKernel() {}
}
