package com.razorpay.rig;

import java.nio.file.Path;
import java.util.ArrayList;
import java.util.List;

/**
 * The auditor. Verifies the hash chain, then re-executes every recorded decision with
 * an independent Java implementation of the policy and asserts the verdict matches.
 *
 *   usage: java -cp out com.razorpay.rig.ReplayAuditor [wal/rig.wal] [--json]
 */
public final class ReplayAuditor {

    private static final String G = "[32m", R = "[31m",
                                D = "[2m",  Z = "[0m";

    public static void main(String[] args) throws Exception {
        String path = "wal/rig.wal";
        boolean json = false;
        for (String a : args) {
            if (a.equals("--json")) json = true; else path = a;
        }

        var rep = ChainVerifier.verify(Path.of(path), r -> r.type == 3);

        long decisions = 0, divergent = 0, allowed = 0, denied = 0;
        List<String> details = new ArrayList<>();
        for (WalRecord r : rep.decisions) {
            DecisionPayload d;
            try {
                d = DecisionPayload.parse(r.payload);
            } catch (IllegalArgumentException e) {
                divergent++;
                details.add("seq " + r.seq + ": " + e.getMessage());
                continue;
            }
            decisions++;
            var v = PolicyKernel.evaluate(d);
            if (v.bits != d.recordedBits || v.total != d.recordedTotal) {
                divergent++;
                details.add(String.format(
                    "seq %d: recorded 0x%04X/%d, java replay 0x%04X/%d",
                    r.seq, d.recordedBits, d.recordedTotal, v.bits, v.total));
            }
            if (d.recordedBits == 0) allowed++; else denied++;
        }

        if (json) {
            System.out.printf(
                "{\"records\":%d,\"chain_intact\":%b,\"decisions\":%d,\"allowed\":%d,"
              + "\"denied\":%d,\"divergent\":%d,\"detail\":\"%s\"}%n",
                rep.records, rep.intact, decisions, allowed, denied, divergent, rep.detail);
            System.exit(rep.intact && divergent == 0 ? 0 : 1);
        }

        System.out.printf("%n  %sindependent audit (Java)%s  %s%n", D, Z, path);
        System.out.printf("  chain     : %d records, SHA-256 chain %s%s%s%n",
            rep.records, rep.intact ? G : R, rep.intact ? "INTACT" : "BROKEN", Z);
        if (!rep.intact) {
            System.out.printf("  %s          -> %s at seq %d%s%n", R, rep.detail, rep.breakSeq, Z);
        }
        System.out.printf("  replay    : %d decisions re-executed by a SEPARATE implementation%n",
            decisions);
        System.out.printf("  outcome   : %d allowed, %d denied%n", allowed, denied);
        System.out.printf("  divergent : %s%d%s%n", divergent == 0 ? G : R, divergent, Z);
        for (String s : details) {
            System.out.printf("    %s%s%s%n", R, s, Z);
        }
        if (rep.intact && divergent == 0 && decisions > 0) {
            System.out.printf(
                "%n  %sOK%s C++ engine and Java auditor agree on every money action%n%n", G, Z);
        } else {
            System.out.printf("%n  %sFAIL%s this log does not verify%n%n", R, Z);
        }
        System.exit(rep.intact && divergent == 0 ? 0 : 1);
    }

    private ReplayAuditor() {}
}
