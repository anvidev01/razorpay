package com.razorpay.rig;

import java.nio.ByteBuffer;
import java.nio.ByteOrder;

/**
 * Decoder for the POLICY_DECISION payload written by the C++ engine.
 * Offsets are asserted against the C++ layout -- see engine/src (offsetof dump):
 *   sizeof(IntentSchema)=384  sizeof(DecisionPayload)=1536  MAXC=16  MAX_CART=64
 */
public final class DecisionPayload {
    public static final int SIZE = 2432;
    private static final int SCHEMA = 0;
    // IntentSchema (sizeof 512)
    private static final int MANDATE_ID = SCHEMA + 0,   NOT_BEFORE = SCHEMA + 8,
                             NOT_AFTER  = SCHEMA + 16,  BUDGET     = SCHEMA + 24,
                             ALLOW_MASK = SCHEMA + 32,  N_CONSTR   = SCHEMA + 40,
                             SCHEMA_VER = SCHEMA + 44,  C_SKU      = SCHEMA + 48,
                             C_UNIT     = SCHEMA + 112, C_QTY      = SCHEMA + 240,
                             C_CAT      = SCHEMA + 304, S_DELTA_BP = SCHEMA + 368,
                             S_POLICY   = SCHEMA + 370, S_ALLOW_REC = SCHEMA + 371,
                             S_MAX_REC  = SCHEMA + 376;
    // DecisionPayload (sizeof 1920)
    private static final int NOW_NS = 512, MERCHANT = 520, N_LINES = 524,
                             SKU = 528, UNIT = 784, QTY = 1296, CAT = 1552,
                             RECURRING = 1808, TEXT_FLAGS = 2320, REC_BITS = 2324,
                             REC_TOTAL = 2328, EVAL_NS = 2336, IDEM_KEY = 2352,
                             AGENT_SESSION = 2384, RISK_BITS = 2392, OUTCOME = 2396;

    public long mandateId, notBeforeNs, notAfterNs, totalBudgetPaise, merchantAllowMask;
    public int  nConstraints, schemaVersion;
    public int[]  constraintSku = new int[PolicyKernel.MAXC];
    public long[] constraintUnit = new long[PolicyKernel.MAXC];
    public int[]  constraintQty = new int[PolicyKernel.MAXC];
    public int[]  constraintCat = new int[PolicyKernel.MAXC];
    public int    substPolicy, substMaxDeltaBp, allowRecurring;
    public long   maxRecurringPaise;

    public long nowNs;
    public int  merchantId, nLines;
    public int[]  sku       = new int[PolicyKernel.MAX_CART];
    public long[] unitPaise = new long[PolicyKernel.MAX_CART];
    public int[]  qty       = new int[PolicyKernel.MAX_CART];
    public int[]  category  = new int[PolicyKernel.MAX_CART];
    public long[] recurring = new long[PolicyKernel.MAX_CART];
    public int    textFlags;
    public int  recordedBits, riskBits, outcome;
    public long agentSessionId;
    public long recordedTotal, evalNs;

    public static DecisionPayload parse(byte[] p) {
        if (p.length != SIZE)
            throw new IllegalArgumentException("POLICY_DECISION payload is " + p.length
                + " bytes, expected " + SIZE + " -- engine/Java layout drift");
        ByteBuffer b = ByteBuffer.wrap(p).order(ByteOrder.LITTLE_ENDIAN);
        DecisionPayload d = new DecisionPayload();
        d.mandateId         = b.getLong(MANDATE_ID);
        d.notBeforeNs       = b.getLong(NOT_BEFORE);
        d.notAfterNs        = b.getLong(NOT_AFTER);
        d.totalBudgetPaise  = b.getLong(BUDGET);
        d.merchantAllowMask = b.getLong(ALLOW_MASK);
        d.nConstraints      = b.getInt(N_CONSTR);
        d.schemaVersion     = b.getInt(SCHEMA_VER);
        for (int i = 0; i < PolicyKernel.MAXC; i++) {
            d.constraintSku[i]  = b.getInt(C_SKU + 4 * i);
            d.constraintUnit[i] = b.getLong(C_UNIT + 8 * i);
            d.constraintQty[i]  = b.getInt(C_QTY + 4 * i);
            d.constraintCat[i]  = b.getInt(C_CAT + 4 * i);
        }
        d.substMaxDeltaBp = b.getShort(S_DELTA_BP) & 0xFFFF;
        d.substPolicy     = b.get(S_POLICY) & 0xFF;
        d.allowRecurring  = b.get(S_ALLOW_REC) & 0xFF;
        d.maxRecurringPaise = b.getLong(S_MAX_REC);
        d.nowNs      = b.getLong(NOW_NS);
        d.merchantId = b.getInt(MERCHANT);
        d.nLines     = b.getInt(N_LINES);
        for (int i = 0; i < PolicyKernel.MAX_CART; i++) {
            d.sku[i]       = b.getInt(SKU + 4 * i);
            d.unitPaise[i] = b.getLong(UNIT + 8 * i);
            d.qty[i]       = b.getInt(QTY + 4 * i);
            d.category[i]  = b.getInt(CAT + 4 * i);
            d.recurring[i] = b.getLong(RECURRING + 8 * i);
        }
        d.textFlags = b.getInt(TEXT_FLAGS);
        d.recordedBits  = b.getInt(REC_BITS);
        d.recordedTotal = b.getLong(REC_TOTAL);
        d.evalNs        = b.getLong(EVAL_NS);
        d.agentSessionId = b.getLong(AGENT_SESSION);
        d.riskBits       = b.getInt(RISK_BITS);
        d.outcome        = b.get(OUTCOME) & 0xFF;
        return d;
    }
}
