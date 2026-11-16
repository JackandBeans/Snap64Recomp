/**
 * @file android.cpp
 * @brief See android.h.
 */
#include "android.h"

#include <algorithm>
#include <atomic>
#include <cerrno>
#include <csignal>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <thread>

#include <android/log.h>
#include <dlfcn.h>
#include <fcntl.h>
#include <jni.h>
#include <link.h>
#include <sys/prctl.h>
#include <sys/syscall.h>
#include <ucontext.h>
#include <unistd.h>

#include <SDL2/SDL_system.h>

// The renderer's Vulkan layer keeps the call each thread began last
// (plume_vulkan.cpp, SNAP_VK_OP), in a table a signal handler can read.
extern "C" int snap_vk_op_of(int tid, const char** name, unsigned* count);
extern "C" int snap_vk_op_at(int index, int* tid, const char** name, unsigned* count);

namespace {

// The crash record. Everything from here to crash_handler runs inside a
// signal handler, on a thread that has just faulted: no stdio, no
// allocation, no lock. A line is built in a fixed buffer and goes out with
// write() on the log's own descriptor, which is unbuffered and in append
// mode (main.cpp, snap_bind_stdio), so it lands after the last line printed.
const int kCrashSignals[] = {SIGSEGV, SIGBUS, SIGILL, SIGFPE, SIGABRT, SIGTRAP, SIGSYS};
const int kCrashSignalCount = int(sizeof(kCrashSignals) / sizeof(kCrashSignals[0]));
struct sigaction s_crash_old[kCrashSignalCount];
std::atomic<int> s_crash_recorded{0};
int s_crash_pipe[2] = {-1, -1};
// libmain.so's load bias and end: an address in between, minus the bias, is
// the address in the file, which the build's unstripped copy names. Its code
// is the part a return address can point into.
uintptr_t s_self_bias = 0;
uintptr_t s_self_end = 0;
uintptr_t s_self_code_start = 0;
uintptr_t s_self_code_end = 0;

struct CrashLine {
    char buf[640];
    size_t n = 0;

    void str(const char* s) {
        while ((s != nullptr) && (*s != 0) && (n < sizeof(buf) - 2)) {
            buf[n++] = *s++;
        }
    }

    void dec(uint64_t v) {
        char digits[24];
        int k = 0;
        do {
            digits[k++] = char('0' + (v % 10));
            v /= 10;
        } while (v != 0);
        while ((k > 0) && (n < sizeof(buf) - 2)) {
            buf[n++] = digits[--k];
        }
    }

    void sdec(int64_t v) {
        if (v < 0) {
            str("-");
            dec(uint64_t(0) - uint64_t(v));
        } else {
            dec(uint64_t(v));
        }
    }

    void hex(uint64_t v) {
        char digits[20];
        int k = 0;
        do {
            digits[k++] = "0123456789abcdef"[v & 15];
            v >>= 4;
        } while (v != 0);
        str("0x");
        while ((k > 0) && (n < sizeof(buf) - 2)) {
            buf[n++] = digits[--k];
        }
    }

    void end() {
        buf[n++] = '\n';
        size_t at = 0;
        while (at < n) {
            const ssize_t w = write(STDERR_FILENO, buf + at, n - at);
            if (w <= 0) {
                if ((w < 0) && (errno == EINTR)) {
                    continue;
                }
                break;
            }
            at += size_t(w);
        }
        n = 0;
    }
};

const char* crash_signal_name(int sig) {
    switch (sig) {
        case SIGSEGV: return "SIGSEGV";
        case SIGBUS: return "SIGBUS";
        case SIGILL: return "SIGILL";
        case SIGFPE: return "SIGFPE";
        case SIGABRT: return "SIGABRT";
        case SIGTRAP: return "SIGTRAP";
        case SIGSYS: return "SIGSYS";
        default: return "a signal";
    }
}

// Memory that may not be there, read without faulting: the kernel copies it
// into a pipe for the write and answers EFAULT where it is missing.
bool crash_read(uintptr_t address, void* out, size_t size) {
    if (s_crash_pipe[1] < 0) {
        return false;
    }
    ssize_t w;
    do {
        w = write(s_crash_pipe[1], reinterpret_cast<const void*>(address), size);
    } while ((w < 0) && (errno == EINTR));
    if (w <= 0) {
        return false;
    }
    // Whatever went in comes out again, a short write included.
    ssize_t r;
    do {
        r = read(s_crash_pipe[0], out, size_t(w));
    } while ((r < 0) && (errno == EINTR));
    return (w == ssize_t(size)) && (r == w);
}

// A return address as it is stored may carry a pointer authentication code
// (the system's libraries sign theirs on this class of processor): XPACLRI
// takes it off, and is a no-op where there is none.
uintptr_t crash_plain(uintptr_t address) {
#if defined(__aarch64__)
    uintptr_t plain;
    __asm__ volatile(
        "mov x30, %[in]\n\t"
        "hint #7\n\t"
        "mov %[out], x30\n\t"
        : [out] "=r"(plain)
        : [in] "r"(address)
        : "x30");
    return plain;
#else
    return address;
#endif
}

struct CrashFrame {
    uintptr_t address;
    uintptr_t offset; // in the mapped file
    bool found;
    char path[112];
};
const int kCrashMaxFrames = 34;
CrashFrame s_crash_frames[kCrashMaxFrames];

uintptr_t crash_parse_hex(const char*& p) {
    uintptr_t v = 0;
    for (;; p++) {
        const char c = *p;
        if ((c >= '0') && (c <= '9')) {
            v = (v << 4) | uintptr_t(c - '0');
        } else if ((c >= 'a') && (c <= 'f')) {
            v = (v << 4) | uintptr_t(c - 'a' + 10);
        } else {
            return v;
        }
    }
}

// One pass over /proc/self/maps for every frame: the mapped file each
// address is in, and where in it.
void crash_resolve(CrashFrame* frames, int count) {
    const int fd = open("/proc/self/maps", O_RDONLY | O_CLOEXEC);
    if (fd < 0) {
        return;
    }
    static char chunk[4096];
    static char line[400];
    size_t length = 0;
    for (;;) {
        const ssize_t got = read(fd, chunk, sizeof(chunk));
        if (got <= 0) {
            if ((got < 0) && (errno == EINTR)) {
                continue;
            }
            break;
        }
        for (ssize_t i = 0; i < got; i++) {
            if (chunk[i] != '\n') {
                if (length < sizeof(line) - 1) {
                    line[length++] = chunk[i];
                }
                continue;
            }
            line[length] = 0;
            length = 0;
            // "7b0a1000-7b0a5000 r-xp 00012000 fd:05 1234    /path"
            const char* p = line;
            const uintptr_t start = crash_parse_hex(p);
            if (*p != '-') {
                continue;
            }
            p++;
            const uintptr_t end = crash_parse_hex(p);
            while (*p == ' ') { p++; }
            while ((*p != 0) && (*p != ' ')) { p++; } // permissions
            while (*p == ' ') { p++; }
            const uintptr_t offset = crash_parse_hex(p);
            while (*p == ' ') { p++; }
            while ((*p != 0) && (*p != ' ')) { p++; } // device
            while (*p == ' ') { p++; }
            while ((*p != 0) && (*p != ' ')) { p++; } // inode
            while (*p == ' ') { p++; }
            for (int f = 0; f < count; f++) {
                CrashFrame& frame = frames[f];
                if (frame.found || (frame.address < start) || (frame.address >= end)) {
                    continue;
                }
                frame.found = true;
                frame.offset = frame.address - start + offset;
                // The end of a long path says more than its start.
                const size_t whole = strlen(p);
                const char* from = (whole > sizeof(frame.path) - 1) ? (p + (whole - (sizeof(frame.path) - 1))) : p;
                size_t k = 0;
                while ((from[k] != 0) && (k < sizeof(frame.path) - 1)) {
                    frame.path[k] = from[k];
                    k++;
                }
                frame.path[k] = 0;
            }
        }
    }
    close(fd);
}

// The one line buffer, made when the library loads: a static inside the
// handler would be made at its first use, behind a lock.
CrashLine s_crash_line;

void crash_record(int sig, siginfo_t* info, ucontext_t* context) {
    CrashLine& line = s_crash_line;
    char thread[20] = {0};
    prctl(PR_GET_NAME, reinterpret_cast<unsigned long>(thread), 0, 0, 0);
    const int tid = int(syscall(SYS_gettid));

    // First what costs nothing to say and matters most.
    line.str("[SNAP-CRASH] ");
    line.str(crash_signal_name(sig));
    line.str(" (signal ");
    line.dec(uint64_t(sig));
    line.str("), code ");
    line.sdec((info != nullptr) ? info->si_code : 0);
    if ((info != nullptr) && (info->si_code > 0) && (sig != SIGABRT) && (sig != SIGSYS)) {
        line.str(", at address ");
        line.hex(reinterpret_cast<uintptr_t>(info->si_addr));
    }
    line.str(", in thread \"");
    line.str(thread);
    line.str("\" (id ");
    line.dec(uint64_t(tid));
    line.str(")");
    line.end();

    const char* call = nullptr;
    unsigned count = 0;
    line.str("[SNAP-CRASH] ");
    if (snap_vk_op_of(tid, &call, &count) > 0) {
        line.str("the last Vulkan call this thread began: ");
        line.str(call);
        line.str(" (its call number ");
        line.dec(count);
        line.str(")");
    } else {
        line.str("this thread has made no Vulkan call");
    }
    line.end();
    int others = 0;
    for (int i = 0;; i++) {
        int other = 0;
        const int has = snap_vk_op_at(i, &other, &call, &count);
        if (has < 0) {
            break;
        }
        if ((has == 0) || (other == tid)) {
            continue;
        }
        if (others >= 8) {
            break;
        }
        others++;
        line.str("[SNAP-CRASH] thread id ");
        line.dec(uint64_t(other));
        line.str(" began last: ");
        line.str(call);
        line.str(" (its call number ");
        line.dec(count);
        line.str(")");
        line.end();
    }

    // Then the code addresses: where it stopped, and the callers by the
    // frame records on the thread's stack.
    uintptr_t pc = 0;
    uintptr_t sp = 0;
    uintptr_t fp = 0;
    uintptr_t lr = 0;
    if (context != nullptr) {
#if defined(__aarch64__)
        pc = uintptr_t(context->uc_mcontext.pc);
        sp = uintptr_t(context->uc_mcontext.sp);
        fp = uintptr_t(context->uc_mcontext.regs[29]);
        lr = uintptr_t(context->uc_mcontext.regs[30]);
#elif defined(__x86_64__)
        pc = uintptr_t(context->uc_mcontext.gregs[REG_RIP]);
        sp = uintptr_t(context->uc_mcontext.gregs[REG_RSP]);
        fp = uintptr_t(context->uc_mcontext.gregs[REG_RBP]);
#endif
    }
    int frames = 0;
    const auto push = [&frames](uintptr_t address) {
        if ((address != 0) && (frames < kCrashMaxFrames)) {
            s_crash_frames[frames] = CrashFrame{};
            s_crash_frames[frames].address = address;
            frames++;
        }
    };
    push(pc);
    const bool hasLink = (lr != 0);
    if (hasLink) {
        push(crash_plain(lr));
    }
    uintptr_t at = fp;
    for (int depth = 0; (depth < 30) && (at != 0); depth++) {
        if (((at & 7) != 0) || (at < sp)) {
            break;
        }
        uintptr_t record[2] = {0, 0};
        if (!crash_read(at, record, sizeof(record))) {
            break;
        }
        push(crash_plain(record[1]));
        if ((record[0] <= at) || ((record[0] - at) > (uintptr_t(16) << 20))) {
            break;
        }
        at = record[0];
    }
    crash_resolve(s_crash_frames, frames);
    for (int i = 0; i < frames; i++) {
        const CrashFrame& frame = s_crash_frames[i];
        line.str("[SNAP-CRASH] #");
        if (i < 10) {
            line.str("0");
        }
        line.dec(uint64_t(i));
        line.str(" ");
        line.hex(frame.address);
        if ((s_self_end != 0) && (frame.address >= s_self_bias) && (frame.address < s_self_end)) {
            line.str(" libmain.so +");
            line.hex(frame.address - s_self_bias);
        } else if (frame.found) {
            line.str(" ");
            line.str((frame.path[0] != 0) ? frame.path : "(memory with no file)");
            line.str(" +");
            line.hex(frame.offset);
        } else {
            line.str(" (in no mapped memory)");
        }
        if (i == 0) {
            line.str("  <- where it stopped");
        } else if ((i == 1) && hasLink) {
            line.str("  <- the link register");
        }
        line.end();
    }
    // The words on the stack that point into libmain.so's code: the callers
    // by the return addresses they left, for a stack whose frame records do
    // not lead back to them (a driver built without any). A word can be a
    // stale one from an earlier call, so these are leads, not the chain.
    if ((s_self_code_end != 0) && (sp != 0)) {
        int said = 0;
        uintptr_t words[64];
        for (int block = 0; (block < 16) && (said < 16); block++) {
            const uintptr_t from = sp + uintptr_t(block) * sizeof(words);
            if (!crash_read(from, words, sizeof(words))) {
                break;
            }
            for (int w = 0; (w < 64) && (said < 16); w++) {
                const uintptr_t value = crash_plain(words[w]);
                if ((value < s_self_code_start) || (value >= s_self_code_end)) {
                    continue;
                }
                said++;
                line.str("[SNAP-CRASH] on the stack, ");
                line.hex(uint64_t(block) * sizeof(words) + uint64_t(w) * sizeof(uintptr_t));
                line.str(" up: libmain.so +");
                line.hex(value - s_self_bias);
                line.end();
            }
        }
    }
    line.str("[SNAP-CRASH] the system makes its own report next; the next start copies it into the log");
    line.end();
}

void crash_handler(int sig, siginfo_t* info, void* context) {
    const int kept = errno;
    if (s_crash_recorded.exchange(1) == 0) {
        crash_record(sig, info, static_cast<ucontext_t*>(context));
    }
    // The system's own handler gets the signal next. Put back, it runs when
    // the instruction faults again on return, and its report is made as if
    // this handler had never been here.
    for (int i = 0; i < kCrashSignalCount; i++) {
        if (kCrashSignals[i] == sig) {
            sigaction(sig, &s_crash_old[i], nullptr);
        }
    }
    errno = kept;
    if ((info == nullptr) || (info->si_code <= 0) || (sig == SIGABRT)) {
        // Sent by a call (abort, a kill), not raised by an instruction:
        // nothing raises it again on return, so it is sent again here.
        syscall(SYS_tgkill, getpid(), int(syscall(SYS_gettid)), sig);
    }
}

int crash_find_self(struct dl_phdr_info* info, size_t /*size*/, void* data) {
    const uintptr_t probe = reinterpret_cast<uintptr_t>(data);
    for (int i = 0; i < info->dlpi_phnum; i++) {
        const ElfW(Phdr)& header = info->dlpi_phdr[i];
        if (header.p_type != PT_LOAD) {
            continue;
        }
        const uintptr_t start = uintptr_t(info->dlpi_addr + header.p_vaddr);
        if ((probe < start) || (probe >= start + header.p_memsz)) {
            continue;
        }
        s_self_bias = uintptr_t(info->dlpi_addr);
        for (int j = 0; j < info->dlpi_phnum; j++) {
            const ElfW(Phdr)& other = info->dlpi_phdr[j];
            if (other.p_type == PT_LOAD) {
                s_self_end = std::max(s_self_end, uintptr_t(info->dlpi_addr + other.p_vaddr + other.p_memsz));
                if ((other.p_flags & PF_X) != 0) {
                    s_self_code_start = uintptr_t(info->dlpi_addr + other.p_vaddr);
                    s_self_code_end = s_self_code_start + other.p_memsz;
                }
            }
        }
        return 1;
    }
    return 0;
}

} // namespace

namespace snap {

void android_exit_report() {
    const char* own = SDL_AndroidGetInternalStoragePath();
    if (own == nullptr) {
        return;
    }
    const std::string path = std::string(own) + "/exit_info.txt";
    FILE* note = fopen(path.c_str(), "r");
    if (note == nullptr) {
        return;
    }
    char row[1024];
    while (fgets(row, sizeof(row), note) != nullptr) {
        size_t n = strlen(row);
        while ((n > 0) && ((row[n - 1] == '\n') || (row[n - 1] == '\r'))) {
            row[--n] = 0;
        }
        if (n > 0) {
            printf("[SNAP-EXIT] %s\n", row);
        }
    }
    fclose(note);
    remove(path.c_str());
}

void android_crash_install() {
    if (pipe2(s_crash_pipe, O_CLOEXEC | O_NONBLOCK) != 0) {
        s_crash_pipe[0] = -1;
        s_crash_pipe[1] = -1;
    }
    dl_iterate_phdr(crash_find_self, reinterpret_cast<void*>(&crash_handler));
    struct sigaction action {};
    action.sa_sigaction = crash_handler;
    action.sa_flags = SA_SIGINFO | SA_ONSTACK;
    sigemptyset(&action.sa_mask);
    int put = 0;
    for (int i = 0; i < kCrashSignalCount; i++) {
        if (sigaction(kCrashSignals[i], &action, &s_crash_old[i]) == 0) {
            put++;
        }
    }
    printf("[SNAP-CRASH] the crash record is on for %d signals; libmain.so is loaded at 0x%llx\n",
           put, static_cast<unsigned long long>(s_self_bias));
}

std::string android_pick_document() {
    std::string result;
    JNIEnv* env = static_cast<JNIEnv*>(SDL_AndroidGetJNIEnv());
    jobject activity = static_cast<jobject>(SDL_AndroidGetActivity());
    if ((env == nullptr) || (activity == nullptr)) {
        return result;
    }
    jclass cls = env->GetObjectClass(activity);
    jmethodID pick = env->GetMethodID(cls, "pickDocument", "()Ljava/lang/String;");
    if (pick == nullptr) {
        // Not the port's activity: no picker.
        env->ExceptionClear();
    } else {
        jstring chosen = static_cast<jstring>(env->CallObjectMethod(activity, pick));
        if (env->ExceptionCheck()) {
            env->ExceptionClear();
        } else if (chosen != nullptr) {
            if (const char* utf = env->GetStringUTFChars(chosen, nullptr)) {
                result = utf;
                env->ReleaseStringUTFChars(chosen, utf);
            }
            env->DeleteLocalRef(chosen);
        }
    }
    env->DeleteLocalRef(cls);
    env->DeleteLocalRef(activity);
    return result;
}

int android_logcat_pipe() {
    int fds[2];
    if (pipe(fds) != 0) {
        return -1;
    }
    const int readEnd = fds[0];
    std::thread([readEnd] {
        std::string line;
        char buf[4096];
        for (;;) {
            const ssize_t n = read(readEnd, buf, sizeof(buf));
            if (n <= 0) {
                break;
            }
            for (ssize_t i = 0; i < n; i++) {
                if (buf[i] == '\n') {
                    __android_log_write(ANDROID_LOG_INFO, "Snap64", line.c_str());
                    line.clear();
                } else {
                    line.push_back(buf[i]);
                }
            }
        }
        if (!line.empty()) {
            __android_log_write(ANDROID_LOG_INFO, "Snap64", line.c_str());
        }
        close(readEnd);
    }).detach();
    return fds[1];
}

} // namespace snap
