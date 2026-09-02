package com.razorpay.rig;

import java.nio.ByteBuffer;
import java.nio.ByteOrder;

/**
 * Decoder for the POLICY_DECISION payload written by the C++ engine.
 * Offsets are asserted against the C++ layout -- see engine/src (offsetof dump):
 *   sizeof(IntentSchema)=384  sizeof(DecisionPayload)=1536  MAXC=16  MAX_CART=64
 */
public final class DecisionPayload {
    public static final int SIZE = 1536;
    private static final int SCHEMA = 0;
    // IntentSchema
    private static final int MANDATE_ID = SCHEMA + 0,   NOT_BEFORE = SCHEMA + 8,
                             NOT_AFTER  = SCHEMA + 16,  BUDGET     = SCHEMA + 24,
                             ALLOW_MASK = SCHEMA + 32,  N_CONSTR   = SCHEMA + 40,
                             SCHEMA_VER = SCHEMA + 44,  C_SKU      = SCHEMA + 48,
                             C_UNIT     = SCHEMA + 112, C_QTY      = SCHEMA + 240;
    // DecisionPayload
    private static final int NOW_NS = 384, MERCHANT = 392, N_LINES = 396,
                             SKU = 400, UNIT = 656, QTY = 1168,
                             REC_BITS = 1424, REC_TOTAL = 1432, EVAL_NS = 1440;

    public long mandateId, notBeforeNs, notAfterNs, totalBudgetPaise, merchantAllowMask;
    public int  nConstraints, schemaVersion;
    public int[]  constraintSku = new int[PolicyKernel.MAXC];
    public long[] constraintUnit = new long[PolicyKernel.MAXC];
    public int[]  constraintQty = new int[PolicyKernel.MAXC];

    public long nowNs;
    public int  merchantId, nLines;
    public int[]  sku       = new int[PolicyKernel.MAX_CART];
    public long[] unitPaise = new long[PolicyKernel.MAX_CART];
    public int[]  qty       = new int[PolicyKernel.MAX_CART];
    public int  recordedBits;
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
        }
        d.nowNs      = b.getLong(NOW_NS);
        d.merchantId = b.getInt(MERCHANT);
        d.nLines     = b.getInt(N_LINES);
        for (int i = 0; i < PolicyKernel.MAX_CART; i++) {
            d.sku[i]       = b.getInt(SKU + 4 * i);
            d.unitPaise[i] = b.getLong(UNIT + 8 * i);
            d.qty[i]       = b.getInt(QTY + 4 * i);
        }
        d.recordedBits  = b.getInt(REC_BITS);
        d.recordedTotal = b.getLong(REC_TOTAL);
        d.evalNs        = b.getLong(EVAL_NS);
        return d;
    }
}
