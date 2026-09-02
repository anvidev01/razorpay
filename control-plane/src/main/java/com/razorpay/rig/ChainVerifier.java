package com.razorpay.rig;

import java.io.IOException;
import java.nio.ByteBuffer;
import java.nio.ByteOrder;
import java.nio.channels.FileChannel;
import java.nio.file.Path;
import java.nio.file.StandardOpenOption;
import java.security.MessageDigest;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.List;
import java.util.function.Predicate;

/**
 * Reads and verifies the WAL written by the C++ engine, using a completely separate
 * implementation: its own record parser, its own CRC32C, its own SHA-256 chaining.
 *
 * That independence is the point. Re-running the C++ verifier only proves the code is
 * self-consistent. A second implementation, in a different language, agreeing on every
 * byte proves the audit log is a well-specified artefact a third party can check.
 */
public final class ChainVerifier {

    public static final class Report {
        public long records, breakSeq;
        public boolean intact = true;
        public String detail = "";
        public final List<WalRecord> decisions = new ArrayList<>();
    }

    /** Memory-maps the log read-only. No JNI, no Panama, no --enable-preview. */
    public static Report verify(Path path, Predicate<WalRecord> keep) throws IOException {
        Report rep = new Report();
        try (FileChannel ch = FileChannel.open(path, StandardOpenOption.READ)) {
            long size = ch.size();
            if (size == 0) return rep;
            ByteBuffer buf = ch.map(FileChannel.MapMode.READ_ONLY, 0, size)
                               .order(ByteOrder.LITTLE_ENDIAN);
            byte[] expectPrev = new byte[WalRecord.HASH_BYTES];
            MessageDigest sha;
            try { sha = MessageDigest.getInstance("SHA-256"); }
            catch (Exception e) { throw new IOException(e); }

            while (buf.remaining() >= WalRecord.HEADER_BYTES) {
                int start = buf.position();
                long len = Integer.toUnsignedLong(buf.getInt(start));
                if (len < WalRecord.HEADER_BYTES + 64 || len > buf.remaining()) {
                    rep.intact = false; rep.detail = "truncated or implausible record length"; break;
                }
                byte[] rec = new byte[(int) len];
                buf.get(rec);

                ByteBuffer h = ByteBuffer.wrap(rec).order(ByteOrder.LITTLE_ENDIAN);
                h.getInt();                       // len
                int crc      = h.getInt();
                long seq     = h.getLong();
                long wallNs  = h.getLong();
                long monoNs  = h.getLong();
                int type     = h.get() & 0xFF;
                int version  = h.get() & 0xFF;
                int flags    = h.getShort() & 0xFFFF;

                int payLen = (int) len - WalRecord.HEADER_BYTES - 64;
                byte[] payload  = Arrays.copyOfRange(rec, WalRecord.HEADER_BYTES,
                                                     WalRecord.HEADER_BYTES + payLen);
                byte[] prevHash = Arrays.copyOfRange(rec, WalRecord.HEADER_BYTES + payLen,
                                                     WalRecord.HEADER_BYTES + payLen + 32);
                byte[] thisHash = Arrays.copyOfRange(rec, WalRecord.HEADER_BYTES + payLen + 32,
                                                     (int) len);

                int wantCrc = Crc32c.compute(rec, 8, (int) len - 64 - 8);
                if (wantCrc != crc) {
                    rep.intact = false; rep.breakSeq = seq;
                    rep.detail = "crc mismatch (record body altered)"; break;
                }
                if (!Arrays.equals(prevHash, expectPrev)) {
                    rep.intact = false; rep.breakSeq = seq;
                    rep.detail = "chain break: prev_hash does not match preceding record"; break;
                }
                sha.reset();
                sha.update(prevHash);
                sha.update(rec, 0, WalRecord.HEADER_BYTES + payLen);
                if (!Arrays.equals(sha.digest(), thisHash)) {
                    rep.intact = false; rep.breakSeq = seq;
                    rep.detail = "chain break: this_hash does not match record contents"; break;
                }

                WalRecord r = new WalRecord(len, crc, seq, wallNs, monoNs, type, version, flags,
                                            payload, prevHash, thisHash);
                if (keep != null && keep.test(r)) rep.decisions.add(r);
                expectPrev = thisHash;
                rep.records++;
            }
        }
        return rep;
    }

    private ChainVerifier() {}
}
