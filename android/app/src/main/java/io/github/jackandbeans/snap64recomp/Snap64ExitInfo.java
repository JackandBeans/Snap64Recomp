package io.github.jackandbeans.snap64recomp;

import android.app.ActivityManager;
import android.app.ApplicationExitInfo;
import android.content.Context;
import android.os.Build;

import java.io.ByteArrayOutputStream;
import java.io.File;
import java.io.FileOutputStream;
import java.io.InputStream;
import java.io.OutputStream;
import java.nio.charset.StandardCharsets;
import java.util.ArrayList;
import java.util.HashSet;
import java.util.List;

/**
 * What Android kept of the app's last ends, as lines for snap64.log: why each
 * process ended (Android 11 on), and for a native crash the system's own
 * report, its tombstone (Android 12 on), reduced to the signal and the
 * crashing thread's frames. A phone shows a player none of it, and a report
 * needs it. The activity writes the lines to a file before the native side
 * starts, and the native side copies the file into the log (src/android.cpp,
 * android_exit_report).
 *
 * The tombstone is a protocol buffer (Android's
 * system/core/debuggerd/proto/tombstone.proto). Only the few fields named
 * below are read, by a reader of the wire format small enough to keep here:
 * the package carries no protobuf library.
 */
final class Snap64ExitInfo {
    private static final int LAST_ENDS = 3;
    private static final int MAX_FRAMES = 48;

    private Snap64ExitInfo() {
    }

    /**
     * The lines, each ending in a newline, or an empty string where the system
     * has nothing (or is older than Android 11). The newest native crash's
     * whole report is also written to rawReport when that is not null.
     */
    static String collect(Context context, File rawReport) {
        final StringBuilder sb = new StringBuilder();
        if (Build.VERSION.SDK_INT < 30) {
            return "";
        }
        try {
            final ActivityManager manager = (ActivityManager) context.getSystemService(Context.ACTIVITY_SERVICE);
            final List<ApplicationExitInfo> ends = manager.getHistoricalProcessExitReasons(null, 0, LAST_ENDS);
            final long now = System.currentTimeMillis();
            boolean rawWritten = false;
            for (ApplicationExitInfo end : ends) {
                sb.append("the app ended ").append(age(now - end.getTimestamp())).append(" before this start: ")
                  .append(reason(end.getReason())).append(", status ").append(end.getStatus());
                final String said = end.getDescription();
                if (said != null && !said.isEmpty()) {
                    sb.append(", \"").append(oneLine(said)).append('"');
                }
                sb.append('\n');
                if (end.getReason() != ApplicationExitInfo.REASON_CRASH_NATIVE || Build.VERSION.SDK_INT < 31) {
                    continue;
                }
                byte[] report = null;
                try (InputStream in = end.getTraceInputStream()) {
                    if (in == null) {
                        sb.append("  the system kept no report of it\n");
                    } else {
                        report = readAll(in);
                    }
                } catch (Exception e) {
                    sb.append("  the system's report could not be read: ").append(oneLine(e.toString())).append('\n');
                }
                if (report == null) {
                    continue;
                }
                try {
                    describe(report, sb);
                } catch (Exception e) {
                    sb.append("  the system's report (").append(report.length).append(" bytes) could not be decoded: ")
                      .append(oneLine(e.toString())).append('\n');
                }
                if (!rawWritten && rawReport != null) {
                    try (OutputStream out = new FileOutputStream(rawReport)) {
                        out.write(report);
                        rawWritten = true;
                        sb.append("  the whole report is kept as ").append(rawReport.getName())
                          .append(" (").append(report.length).append(" bytes)\n");
                    } catch (Exception e) {
                        // The lines above are the record; the file is a spare.
                    }
                }
            }
        } catch (Throwable t) {
            sb.append("the app's last ends could not be asked for: ").append(oneLine(t.toString())).append('\n');
        }
        return sb.toString();
    }

    private static String age(long ms) {
        final long s = Math.max(0, ms / 1000);
        if (s < 120) {
            return s + " s";
        }
        if (s < 7200) {
            return (s / 60) + " min";
        }
        if (s < 172800) {
            return (s / 3600) + " h";
        }
        return (s / 86400) + " days";
    }

    // ApplicationExitInfo's reasons, by their numbers (the names are the
    // system's own constants).
    private static String reason(int reason) {
        final String[] names = {
            "unknown", "it exited by itself", "killed by a signal", "killed for low memory", "a crash in Java",
            "a native crash", "not responding", "it failed to initialize", "a permission changed",
            "excessive resource use", "the user asked", "the user stopped it", "a dependency died", "another reason",
            "frozen", "the package's state changed", "the package was updated",
        };
        final String name = (reason >= 0 && reason < names.length) ? names[reason] : "a reason this build does not know";
        return name + " (reason " + reason + ")";
    }

    private static String oneLine(String s) {
        final String flat = s.replace('\r', ' ').replace('\n', ' ');
        return (flat.length() > 600) ? flat.substring(0, 600) + "..." : flat;
    }

    private static byte[] readAll(InputStream in) throws java.io.IOException {
        final ByteArrayOutputStream all = new ByteArrayOutputStream();
        final byte[] buf = new byte[65536];
        int n;
        while ((n = in.read(buf)) > 0) {
            all.write(buf, 0, n);
        }
        return all.toByteArray();
    }

    // The wire format: a field is a varint tag (number << 3 | type), then a
    // varint (type 0), eight bytes (1), a length and that many bytes (2), or
    // four bytes (5).
    private static final class Reader {
        final byte[] b;
        int p;
        final int end;

        Reader(byte[] b, int p, int end) {
            this.b = b;
            this.p = p;
            this.end = end;
        }

        boolean more() {
            return p < end;
        }

        long varint() {
            long v = 0;
            for (int shift = 0; shift < 64; shift += 7) {
                if (p >= end) {
                    throw new IllegalStateException("a number runs past the end");
                }
                final byte x = b[p++];
                v |= (long) (x & 0x7F) << shift;
                if (x >= 0) {
                    return v;
                }
            }
            throw new IllegalStateException("a number is too long");
        }

        private int length() {
            final long n = varint();
            if (n < 0 || n > end - p) {
                throw new IllegalStateException("a field runs past the end");
            }
            return (int) n;
        }

        Reader sub() {
            final int n = length();
            final Reader r = new Reader(b, p, p + n);
            p += n;
            return r;
        }

        String str() {
            final int n = length();
            final String s = new String(b, p, n, StandardCharsets.UTF_8);
            p += n;
            return s;
        }

        void skip(int type) {
            switch (type) {
                case 0:
                    varint();
                    break;
                case 1:
                    p += 8;
                    break;
                case 2: {
                    // Not "p += length()": Java reads p before the call moves it.
                    final int n = length();
                    p += n;
                    break;
                }
                case 5:
                    p += 4;
                    break;
                default:
                    throw new IllegalStateException("field type " + type);
            }
            if (p > end) {
                throw new IllegalStateException("a field runs past the end");
            }
        }
    }

    /**
     * A tombstone as lines: the signal, the abort message and causes when
     * there are any, then the crashing thread's frames. Tombstone: tid 6,
     * signal_info 10, abort_message 14, causes 15, threads 16 (a map: key 1,
     * Thread 2). Signal: number 1, name 2, code 3, code_name 4,
     * has_fault_address 8, fault_address 9. Cause: human_readable 1. Thread:
     * id 1, name 2, current_backtrace 4, backtrace_note 7. BacktraceFrame:
     * rel_pc 1, function_name 4, function_offset 5, file_name 6, build_id 8.
     */
    static void describe(byte[] report, StringBuilder sb) {
        long tid = 0;
        int number = 0;
        int code = 0;
        String name = "";
        String codeName = "";
        boolean hasFault = false;
        long fault = 0;
        String abort = "";
        final ArrayList<String> causes = new ArrayList<>();

        Reader r = new Reader(report, 0, report.length);
        while (r.more()) {
            final long tag = r.varint();
            final int field = (int) (tag >>> 3);
            final int type = (int) (tag & 7);
            if (field == 6 && type == 0) {
                tid = r.varint();
            } else if (field == 10 && type == 2) {
                final Reader s = r.sub();
                while (s.more()) {
                    final long t = s.varint();
                    final int f = (int) (t >>> 3);
                    final int w = (int) (t & 7);
                    if (f == 1 && w == 0) {
                        number = (int) s.varint();
                    } else if (f == 2 && w == 2) {
                        name = s.str();
                    } else if (f == 3 && w == 0) {
                        code = (int) s.varint();
                    } else if (f == 4 && w == 2) {
                        codeName = s.str();
                    } else if (f == 8 && w == 0) {
                        hasFault = s.varint() != 0;
                    } else if (f == 9 && w == 0) {
                        fault = s.varint();
                    } else {
                        s.skip(w);
                    }
                }
            } else if (field == 14 && type == 2) {
                abort = r.str();
            } else if (field == 15 && type == 2) {
                final Reader c = r.sub();
                while (c.more()) {
                    final long t = c.varint();
                    if ((int) (t >>> 3) == 1 && (int) (t & 7) == 2) {
                        causes.add(c.str());
                    } else {
                        c.skip((int) (t & 7));
                    }
                }
            } else {
                r.skip(type);
            }
        }
        sb.append("  signal ").append(number).append(" (").append(name).append("), code ").append(code)
          .append(" (").append(codeName).append(')');
        if (hasFault) {
            sb.append(", fault address 0x").append(Long.toHexString(fault));
        }
        sb.append('\n');
        if (!abort.isEmpty()) {
            sb.append("  abort message: ").append(oneLine(abort)).append('\n');
        }
        for (String cause : causes) {
            sb.append("  cause: ").append(oneLine(cause)).append('\n');
        }

        // The crashing thread, among all the process's threads.
        boolean found = false;
        r = new Reader(report, 0, report.length);
        while (r.more()) {
            final long tag = r.varint();
            final int field = (int) (tag >>> 3);
            final int type = (int) (tag & 7);
            if (field != 16 || type != 2) {
                r.skip(type);
                continue;
            }
            final Reader entry = r.sub();
            long key = -1;
            Reader thread = null;
            while (entry.more()) {
                final long t = entry.varint();
                final int f = (int) (t >>> 3);
                final int w = (int) (t & 7);
                if (f == 1 && w == 0) {
                    key = entry.varint();
                } else if (f == 2 && w == 2) {
                    thread = entry.sub();
                } else {
                    entry.skip(w);
                }
            }
            if (thread != null && key == tid) {
                describeThread(thread, tid, sb);
                found = true;
            }
        }
        if (!found) {
            sb.append("  the report has no frames for the crashing thread (id ").append(tid).append(")\n");
        }
    }

    private static void describeThread(Reader thread, long tid, StringBuilder sb) {
        String name = "";
        final ArrayList<String> frames = new ArrayList<>();
        final ArrayList<String> notes = new ArrayList<>();
        final HashSet<String> filesSaid = new HashSet<>();
        while (thread.more()) {
            final long t = thread.varint();
            final int f = (int) (t >>> 3);
            final int w = (int) (t & 7);
            if (f == 2 && w == 2) {
                name = thread.str();
            } else if (f == 7 && w == 2) {
                notes.add(thread.str());
            } else if (f == 4 && w == 2) {
                final Reader frame = thread.sub();
                long relPc = 0;
                String function = "";
                long functionOffset = 0;
                String file = "";
                String buildId = "";
                while (frame.more()) {
                    final long ft = frame.varint();
                    final int ff = (int) (ft >>> 3);
                    final int fw = (int) (ft & 7);
                    if (ff == 1 && fw == 0) {
                        relPc = frame.varint();
                    } else if (ff == 4 && fw == 2) {
                        function = frame.str();
                    } else if (ff == 5 && fw == 0) {
                        functionOffset = frame.varint();
                    } else if (ff == 6 && fw == 2) {
                        file = frame.str();
                    } else if (ff == 8 && fw == 2) {
                        buildId = frame.str();
                    } else {
                        frame.skip(fw);
                    }
                }
                if (frames.size() >= MAX_FRAMES) {
                    continue;
                }
                // The app's own files without the install folder, whose name is random and long.
                String shown = file.isEmpty() ? "(no file)" : file;
                if (shown.startsWith("/data/app/")) {
                    shown = "(the app) " + shown.substring(shown.lastIndexOf('/') + 1);
                }
                final StringBuilder row = new StringBuilder();
                row.append("  #").append(frames.size() < 10 ? "0" : "").append(frames.size())
                   .append(" pc 0x").append(Long.toHexString(relPc)).append("  ").append(shown);
                if (!function.isEmpty()) {
                    row.append(" (").append(oneLine(function)).append("+0x").append(Long.toHexString(functionOffset)).append(')');
                }
                // A file's build id once: it says which build's symbols name the addresses.
                if (!buildId.isEmpty() && filesSaid.add(file)) {
                    row.append(" [build id ").append(buildId).append(']');
                }
                frames.add(row.toString());
            } else {
                thread.skip(w);
            }
        }
        sb.append("  the crashing thread is \"").append(name).append("\" (id ").append(tid).append("), ")
          .append(frames.size()).append(" frames:\n");
        for (String note : notes) {
            sb.append("  note: ").append(oneLine(note)).append('\n');
        }
        for (String frame : frames) {
            sb.append(frame).append('\n');
        }
    }
}
