#include <cstdio>
#include <thread>
#include <cassert>
#include <string>
#include <atomic>
#include <chrono>
#include <functional>
#include <mutex>
#include <vector>
#include <algorithm>

#include "ultramodern/ultra64.h"
#include "ultramodern/ultramodern.hpp"
#include "blockingconcurrentqueue.h"

#include "ultramodern/threads.hpp"

// Pokemon Snap port: how long the game thread spent blocked inside
// osCreateThread, read and cleared once a tick by the slow-frame report in
// src/rt64_render_context.cpp. On the console this call was a struct fill
// and a queue link; here it starts a real operating system thread and waits
// for it to come up, and the game starts one per object that runs its own
// process -- several at once whenever a course block is entered.
extern "C" {
    std::atomic<int64_t> snap_thread_create_nanos{0};
    std::atomic<uint32_t> snap_thread_create_count{0};
}

// Native APIs only used to set thread names for easier debugging
#ifdef _WIN32
#include <Windows.h>
#else
#include <csetjmp>
#endif

// Pokemon Snap port: a thread that ends while code made at run time is on
// its stack. A thread ends by throwing thread_terminated on its own stack
// (osDestroyThread on itself, which wait_for_resumed calls when another
// thread destroyed this one while it waited), and the throw unwinds to
// _thread_func. Code the live recompiler made -- a mod's own functions, or a
// game or patch function recompiled for a mod's hook -- has no unwind
// information, so the unwind cannot pass it and the process dies (0xE06D7363
// on Windows). The game parks every process in ohWait and ends it from
// outside, so a hook or patch on any function that waits met this as soon
// as its process ended. Such a thread goes back to _thread_func by
// restoring the context saved there instead: nothing on the way has a
// destructor to run (recompiled C, generated code, the wait, which holds no
// lock by then). A thread with no generated code on its stack still throws,
// so without code mods every thread ends exactly as before.
namespace {
    struct GeneratedCode {
        uintptr_t begin;
        uintptr_t end;
    };
    std::mutex generated_code_mutex;
    std::vector<GeneratedCode> generated_code;
    std::atomic<size_t> generated_code_count{0};

    struct ThreadExit {
#ifdef _WIN32
        CONTEXT context;
#else
        sigjmp_buf jump;
#endif
        uintptr_t stack_top;
    };
    thread_local ThreadExit* thread_exit = nullptr;
    thread_local volatile bool thread_exit_taken = false;
}

void ultramodern::register_generated_code(const void* begin, size_t size) {
    std::lock_guard lock{generated_code_mutex};
    const uintptr_t at = reinterpret_cast<uintptr_t>(begin);
    generated_code.push_back(GeneratedCode{at, at + size});
    generated_code_count.store(generated_code.size());
}

void ultramodern::unregister_generated_code(const void* begin) {
    std::lock_guard lock{generated_code_mutex};
    const uintptr_t at = reinterpret_cast<uintptr_t>(begin);
    generated_code.erase(std::remove_if(generated_code.begin(), generated_code.end(),
        [at](const GeneratedCode& c) { return c.begin == at; }), generated_code.end());
    generated_code_count.store(generated_code.size());
}

// Whether any word between here and the thread's entry points into
// generated code, as a return address into it does. A stale word in a live
// frame can say yes wrongly, which only sends the thread home the other way.
static bool generated_code_on_stack() {
    if (thread_exit == nullptr || generated_code_count.load(std::memory_order_relaxed) == 0) {
        return false;
    }
    volatile uintptr_t marker = 0;
    uintptr_t at = reinterpret_cast<uintptr_t>(&marker) & ~uintptr_t(sizeof(uintptr_t) - 1);
    const uintptr_t top = thread_exit->stack_top;
    std::lock_guard lock{generated_code_mutex};
    for (; at + sizeof(uintptr_t) <= top; at += sizeof(uintptr_t)) {
        const uintptr_t word = *reinterpret_cast<const uintptr_t*>(at);
        for (const GeneratedCode& c : generated_code) {
            if (word >= c.begin && word < c.end) {
                return true;
            }
        }
    }
    return false;
}

static ultramodern::threads::callbacks_t threads_callbacks;

void ultramodern::threads::set_callbacks(const callbacks_t& callbacks) {
    threads_callbacks = callbacks;
}

std::string ultramodern::threads::get_game_thread_name(const OSThread* t) {
    if (threads_callbacks.get_game_thread_name == nullptr) {
        return "Game Thread " + std::to_string(t->id);
    }
    return threads_callbacks.get_game_thread_name(t);
}

extern "C" void bootproc();

thread_local bool is_entrypoint_thread = false;
// Whether this thread is part of the game (i.e. the start thread or one spawned by osCreateThread)
thread_local bool is_game_thread = false;
thread_local PTR(OSThread) thread_self = NULLPTR;

// Pokemon Snap port: every guest thread created, for the stall report
// (src/main.cpp, update_gfx): which thread is running, which are queued,
// which wait on a message queue and on which. The structs live in RDRAM
// for the life of the game; a thread that ended reads as stopped.
static std::mutex& snap_thread_registry_mutex = *new std::mutex();   // never destroyed: game threads may still run at exit
static std::vector<PTR(OSThread)> snap_thread_registry;

void ultramodern::set_entrypoint_thread() {
    ::is_game_thread = true;
    ::is_entrypoint_thread = true;
}

bool ultramodern::is_entrypoint_thread() {
    return ::is_entrypoint_thread;
}

bool ultramodern::is_game_thread() {
    return ::is_game_thread;
}

#if 0
int main(int argc, char** argv) {
    ultramodern::set_entrypoint_thread();

    bootproc();
}
#endif

#if 1
void run_thread_function(uint8_t* rdram, uint64_t addr, uint64_t sp, uint64_t arg);
#else
#define run_thread_function(func, sp, arg) func(arg)
#endif

#if defined(_WIN32)
void ultramodern::set_native_thread_name(const std::string& name) {
    std::wstring wname{name.begin(), name.end()};

    HRESULT r;
    r = SetThreadDescription(
        GetCurrentThread(),
        wname.c_str()
    );
}

void ultramodern::set_native_thread_priority(ThreadPriority pri) {
    int nPriority = THREAD_PRIORITY_NORMAL;

    // Convert ThreadPriority to Win32 priority
    switch (pri) {
        case ThreadPriority::Low:
            nPriority = THREAD_PRIORITY_BELOW_NORMAL;
            break;
        case ThreadPriority::Normal:
            nPriority = THREAD_PRIORITY_NORMAL;
            break;
        case ThreadPriority::High:
            nPriority = THREAD_PRIORITY_ABOVE_NORMAL;
            break;
        case ThreadPriority::VeryHigh:
            nPriority = THREAD_PRIORITY_HIGHEST;
            break;
        case ThreadPriority::Critical:
            nPriority = THREAD_PRIORITY_TIME_CRITICAL;
            break;
        default:
            throw std::runtime_error("Invalid thread priority!");
            break;
    }
    // SetThreadPriority(GetCurrentThread(), nPriority);
}
#elif defined(__linux__)
#include <sys/prctl.h>

void ultramodern::set_native_thread_name(const std::string& name) {
    if (name.length() > 15) {
        // Linux only accepts up to 16 characters including the null terminator for a thread name.
        debug_printf("[Thread] The thread name '%s' will be truncated to 15 characters", name.c_str());
    }

    prctl(PR_SET_NAME, name.c_str());
}

void ultramodern::set_native_thread_priority(ThreadPriority pri) {
    // TODO linux thread priority
    // printf("set_native_thread_priority unimplemented\n");
    // int nPriority = THREAD_PRIORITY_NORMAL;

    // // Convert ThreadPriority to Win32 priority
    // switch (pri) {
    //     case ThreadPriority::Low:
    //         nPriority = THREAD_PRIORITY_BELOW_NORMAL;
    //         break;
    //     case ThreadPriority::Normal:
    //         nPriority = THREAD_PRIORITY_NORMAL;
    //         break;
    //     case ThreadPriority::High:
    //         nPriority = THREAD_PRIORITY_ABOVE_NORMAL;
    //         break;
    //     case ThreadPriority::VeryHigh:
    //         nPriority = THREAD_PRIORITY_HIGHEST;
    //         break;
    //     case ThreadPriority::Critical:
    //         nPriority = THREAD_PRIORITY_TIME_CRITICAL;
    //         break;
    //     default:
    //         throw std::runtime_error("Invalid thread priority!");
    //         break;
    // }
}
#elif defined(__APPLE__)
#include <mach/mach.h>
#include <mach/mach_time.h>
#include <mach/thread_policy.h>

void ultramodern::set_native_thread_name(const std::string& name) {
    if (name.length() > 15) {
        // Macs seem to only accept up to 16 characters including the null terminator for a thread name.
        debug_printf("[Thread] The thread name '%s' will be truncated to 15 characters", name.c_str());
    }

    pthread_setname_np(name.c_str());
}

void ultramodern::set_native_thread_priority(ThreadPriority pri) {
    // Snap64 Recomp: the two threads that pace the game by time -- the VI
    // retrace (Critical) and the timers (VeryHigh) -- get macOS's
    // time-constraint scheduling policy, the one audio and game engines use
    // for their deadlines. Measured on GitHub's virtual Mac (2026-09-26): a
    // plain 16.7 ms sleep woke 61 ms late there on average, so the game ran
    // at 13 retraces a second; with this policy and mach_wait_until
    // (timer.cpp) the same wait woke within 0.04 ms. A real Mac sleeps
    // precisely either way. Every other thread keeps the default policy: a
    // real-time policy is for a few threads, and the game's own threads hand
    // off through semaphores, whose wakeups the same measurement found
    // prompt.
    if ((pri != ThreadPriority::Critical) && (pri != ThreadPriority::VeryHigh)) {
        return;
    }
    mach_timebase_info_data_t timebase{};
    mach_timebase_info(&timebase);
    const auto to_abs = [&](uint64_t ns) { return uint32_t(ns * timebase.denom / timebase.numer); };
    thread_time_constraint_policy_data_t policy{};
    policy.period = to_abs(16666667);      // one retrace
    policy.computation = to_abs(1000000);  // a millisecond of work in it
    policy.constraint = to_abs(5000000);   // done within five
    policy.preemptible = 1;
    const thread_port_t self = mach_thread_self();
    const kern_return_t result = thread_policy_set(self, THREAD_TIME_CONSTRAINT_POLICY,
        reinterpret_cast<thread_policy_t>(&policy), THREAD_TIME_CONSTRAINT_POLICY_COUNT);
    mach_port_deallocate(mach_task_self(), self);
    if (result != KERN_SUCCESS) {
        fprintf(stderr, "[Thread] macOS refused the time-constraint policy for a pacing thread (%d); the game's pace may drift\n", int(result));
    }
}
#endif

// Pokemon Snap port: how long THIS thread spent handed off to another guest
// thread, and how many times. On the console a context switch was a register
// save and a queue link; here every guest thread is an operating system thread
// and a switch is a semaphore handoff plus two trips through the OS scheduler.
// This game runs a process per object, so a frame performs a great many. Taken
// and reset by the slow-frame report.
static thread_local int64_t snap_tls_switch_nanos = 0;
static thread_local uint32_t snap_tls_switch_count = 0;

extern "C" int64_t snap_switch_take_nanos() {
    const int64_t taken = snap_tls_switch_nanos;
    snap_tls_switch_nanos = 0;
    return taken;
}

extern "C" uint32_t snap_switch_take_count() {
    const uint32_t taken = snap_tls_switch_count;
    snap_tls_switch_count = 0;
    return taken;
}

// Pokemon Snap port: which guest thread the tick's time actually belongs to.
// The slow-frame report meters the main thread's update and draw, but the
// game runs a process per object on its own guest thread, and their work --
// spawning, block loading, decompression -- lands in no meter. Every guest
// thread passes through wait_for_resumed to run, so the span from one wake to
// the next wait is exactly the time that thread held the (cooperative) CPU.
// Buckets are keyed by guest entrypoint; the low bit tags a claimed slot so
// the adopted main thread (entrypoint zero) still gets one. Blocking host
// dequeues in wait_for_external_message pause the clock through the exported
// pause/resume pair, so vblank idle is not billed as running.
struct SnapRunBucket {
    std::atomic<uint32_t> entry{0};
    std::atomic<int64_t> nanos{0};
    std::atomic<uint32_t> wakes{0};
};
static SnapRunBucket snap_run_buckets[48];
static thread_local uint32_t snap_tls_entrypoint = 0;
static thread_local std::chrono::steady_clock::time_point snap_tls_run_start{};

static void snap_run_mark_stop(std::chrono::steady_clock::time_point now) {
    if (snap_tls_run_start.time_since_epoch().count() == 0) {
        return;
    }
    const int64_t ns = std::chrono::duration_cast<std::chrono::nanoseconds>(now - snap_tls_run_start).count();
    snap_tls_run_start = {};
    const uint32_t tag = snap_tls_entrypoint | 1u;
    for (auto& bucket : snap_run_buckets) {
        uint32_t e = bucket.entry.load(std::memory_order_relaxed);
        if (e == 0) {
            uint32_t expected = 0;
            if (bucket.entry.compare_exchange_strong(expected, tag, std::memory_order_relaxed)) {
                e = tag;
            }
            else {
                e = expected;
            }
        }
        if (e != tag) {
            continue;
        }
        bucket.nanos.fetch_add(ns, std::memory_order_relaxed);
        bucket.wakes.fetch_add(1, std::memory_order_relaxed);
        return;
    }
}

static void snap_run_mark_start(std::chrono::steady_clock::time_point now) {
    snap_tls_run_start = now;
}

extern "C" void snap_run_clock_pause() {
    snap_run_mark_stop(std::chrono::steady_clock::now());
}

extern "C" void snap_run_clock_resume() {
    snap_run_mark_start(std::chrono::steady_clock::now());
}

extern "C" uint32_t snap_run_table_take(uint32_t* entries, int64_t* nanos, uint32_t* wakes, uint32_t cap) {
    uint32_t written = 0;
    for (auto& bucket : snap_run_buckets) {
        const uint32_t e = bucket.entry.exchange(0, std::memory_order_relaxed);
        const int64_t ns = bucket.nanos.exchange(0, std::memory_order_relaxed);
        const uint32_t w = bucket.wakes.exchange(0, std::memory_order_relaxed);
        if (e != 0 && written < cap) {
            entries[written] = e & ~1u;
            nanos[written] = ns;
            wakes[written] = w;
            written++;
        }
    }
    return written;
}

void wait_for_resumed(RDRAM_ARG UltraThreadContext* thread_context) {
    const auto snapSwitchStart = std::chrono::steady_clock::now();
    snap_run_mark_stop(snapSwitchStart);
    thread_context->running.wait();
    const auto snapSwitchEnd = std::chrono::steady_clock::now();
    snap_tls_switch_nanos += std::chrono::duration_cast<std::chrono::nanoseconds>(
        snapSwitchEnd - snapSwitchStart).count();
    snap_tls_switch_count++;
    snap_run_mark_start(snapSwitchEnd);
    // If this thread's context was replaced by another thread or deleted, destroy it again from its own context.
    // This will trigger thread cleanup instead.
    if (TO_PTR(OSThread, ultramodern::this_thread())->context != thread_context) {
        osDestroyThread(PASS_RDRAM NULLPTR);
    }
}

void resume_thread(OSThread* t) {
    debug_printf("[Thread] Resuming execution of thread %d\n", t->id);
    t->context->running.signal();
}

void run_next_thread(RDRAM_ARG1) {
    if (ultramodern::thread_queue_empty(PASS_RDRAM ultramodern::running_queue)) {
        throw std::runtime_error("No threads left to run!\n");
    }

    OSThread* to_run = TO_PTR(OSThread, ultramodern::thread_queue_pop(PASS_RDRAM ultramodern::running_queue));
    debug_printf("[Scheduling] Resuming execution of thread %d\n", to_run->id);
    to_run->context->running.signal();
}

void ultramodern::run_next_thread_and_wait(RDRAM_ARG1) {
    UltraThreadContext* cur_context = TO_PTR(OSThread, thread_self)->context;
    run_next_thread(PASS_RDRAM1);
    wait_for_resumed(PASS_RDRAM cur_context);
}

void ultramodern::resume_thread_and_wait(RDRAM_ARG OSThread *t) {
    UltraThreadContext* cur_context = TO_PTR(OSThread, thread_self)->context;
    resume_thread(t);
    wait_for_resumed(PASS_RDRAM cur_context);
}

static void _thread_func(RDRAM_ARG PTR(OSThread) self_, PTR(thread_func_t) entrypoint, PTR(void) arg, UltraThreadContext* thread_context) {
    OSThread *self = TO_PTR(OSThread, self_);
    debug_printf("[Thread] Thread created: %d\n", self->id);
    thread_self = self_;
    is_game_thread = true;
    // Attribute this guest thread's running time to its entrypoint. Cleared
    // start so a pooled worker's park time is never billed to the coroutine
    // it picks up next.
    snap_tls_entrypoint = (uint32_t)entrypoint;
    snap_tls_run_start = {};

    // Signal the initialized semaphore to indicate that this thread can be started.
    //
    // Signalled before the thread is named and prioritised, not after. The
    // creating thread is blocked on this semaphore, so everything ahead of it
    // is charged to whoever called osCreateThread -- and this game creates a
    // process per object, so entering a course block calls it a dozen times in
    // one frame. Naming builds a wide string and calls into the debugger
    // interface; both are the new thread's own business and nothing observes
    // them. The thread still cannot run until osStartThread, so moving them
    // after the signal changes no ordering the guest can see.
    thread_context->initialized.signal();

    // Set the thread name
    ultramodern::set_native_thread_name(ultramodern::threads::get_game_thread_name(self));
    ultramodern::set_native_thread_priority(ultramodern::ThreadPriority::High);

    debug_printf("[Thread] Thread waiting to be started: %d\n", self->id);

    // Wait until the thread is marked as running.
    try {
        wait_for_resumed(PASS_RDRAM thread_context);
    } catch (ultramodern::thread_terminated& terminated) {
    }

    // Make sure the thread wasn't replaced or destroyed before it was started.
    if (self->context == thread_context) {
        debug_printf("[Thread] Thread started: %d\n", self->id);
        // Where a thread ending with generated code on its stack comes back
        // to (osDestroyThread): it resumes after the capture with the flag set.
        ThreadExit exit_point;
        exit_point.stack_top = reinterpret_cast<uintptr_t>(&exit_point);
        thread_exit_taken = false;
#ifdef _WIN32
        RtlCaptureContext(&exit_point.context);
#else
        sigsetjmp(exit_point.jump, 0);
#endif
        if (!thread_exit_taken) {
            thread_exit = &exit_point;
            try {
                // Run the thread's function with the provided argument.
                run_thread_function(PASS_RDRAM entrypoint, self->sp, arg);
            } catch (ultramodern::thread_terminated& terminated) {
            }
        }
        thread_exit = nullptr;
        thread_exit_taken = false;
    }
    else {
        debug_printf("[Thread] Thread destroyed before being started: %d\n", self->id);
    }

    // Check if the thread hasn't been destroyed or replaced. If so, then the thread terminated or destroyed itself,
    // so mark this thread as destroyed and run the next queued thread.
    if (self->context == thread_context) {
        self->context = nullptr;
        run_next_thread(PASS_RDRAM1);
    }

    // Close this coroutine's final running leg (wake to return) before the
    // worker parks, so the time is charged to the thread that spent it.
    snap_run_mark_stop(std::chrono::steady_clock::now());

    // Dispose of this thread now that it's completed or terminated.
    ultramodern::cleanup_thread(thread_context);
}

// Pokemon Snap port: a pool of parked host threads for the game's
// coroutines. This game creates an OS thread per object process, and a
// course block boundary creates a dozen in one tick; a real host thread
// birth plus its initialization handshake measured ~3ms apiece, which put
// 37ms of thread creation inside single game ticks -- the largest single
// component of the block-crossing stutter. A parked worker turns the same
// handshake into a semaphore wake. Workers are detached and live for the
// process: a parked thread costs a stack, and destroying one buys nothing.
struct PooledHostThread {
    moodycamel::LightweightSemaphore taskReady;
    std::function<void()> task;
    std::thread thread;
};
// Snap64 Recomp: the pool's objects are never destroyed. Its workers and the
// replenisher are detached threads that live for the process, and at exit
// they are still running when the process's globals are destroyed; on macOS
// a lock on a destroyed mutex fails with EINVAL and libc++ throws, which
// ended every quit with "mutex lock failed" on the replenisher and a crash
// report (found on GitHub's Mac, 2026-09-26). Windows and Linux happen to
// tolerate the same. Leaked on purpose: the process is ending.
static std::mutex& pool_mutex = *new std::mutex();
static std::vector<PooledHostThread*>& pool_parked = *new std::vector<PooledHostThread*>();
// Course coroutines hold their workers for as long as the object lives, and
// a block crossing spawns the new block's objects before the old block's
// despawns return theirs -- so the parked supply can run dry mid-course (a
// later crossing measured 22 synchronous creates in one tick, 36ms). The
// replenisher keeps a float of parked workers topped up from its own thread:
// every take pokes it, and creation cost lands here instead of on a game
// thread. The synchronous path below remains only as a fallback for a burst
// deeper than the float.
static constexpr size_t pool_float_target = 32;
static moodycamel::LightweightSemaphore& pool_replenish_wake = *new moodycamel::LightweightSemaphore();

static void _pooled_thread_main(PooledHostThread* self) {
    while (true) {
        self->taskReady.wait();
        self->task();
        std::lock_guard<std::mutex> lock(pool_mutex);
        pool_parked.push_back(self);
    }
}

static PooledHostThread* pool_make_worker() {
    PooledHostThread* worker = new PooledHostThread();
    // The worker runs the game's own threads, so it gets their stack
    // (ultramodern::threads::start_detached_game_host_thread).
    ultramodern::threads::start_detached_game_host_thread([worker]() { _pooled_thread_main(worker); });
    return worker;
}

static void _pool_replenisher_main() {
    ultramodern::set_native_thread_name("Pool Replenisher");
    while (true) {
        pool_replenish_wake.wait();
        while (true) {
            {
                std::lock_guard<std::mutex> lock(pool_mutex);
                if (pool_parked.size() >= pool_float_target) {
                    break;
                }
            }
            PooledHostThread* worker = pool_make_worker();
            std::lock_guard<std::mutex> lock(pool_mutex);
            pool_parked.push_back(worker);
        }
    }
}

static PooledHostThread* pool_take_or_create() {
    PooledHostThread* worker = nullptr;
    {
        std::lock_guard<std::mutex> lock(pool_mutex);
        if (!pool_parked.empty()) {
            worker = pool_parked.back();
            pool_parked.pop_back();
        }
    }
    pool_replenish_wake.signal();
    if (worker != nullptr) {
        return worker;
    }
    return pool_make_worker();
}

void ultramodern::prewarm_thread_pool(uint32_t count) {
    // Detached like the workers: it parks in a semaphore wait for the life of
    // the process, and a joinable static thread would terminate() at exit.
    static const bool replenisher_started = [] {
        std::thread{_pool_replenisher_main}.detach();
        return true;
    }();
    (void)replenisher_started;
    std::vector<PooledHostThread*> made;
    made.reserve(count);
    for (uint32_t i = 0; i < count; i++) {
        made.push_back(pool_make_worker());
    }
    std::lock_guard<std::mutex> lock(pool_mutex);
    for (PooledHostThread* worker : made) {
        pool_parked.push_back(worker);
    }
}

extern "C" void osStartThread(RDRAM_ARG PTR(OSThread) t_) {
    OSThread* t = TO_PTR(OSThread, t_);
    debug_printf("[os] Start Thread %d\n", t->id);

    // If this is a game thread, insert the new thread into the running queue and then check the running queue.
    if (thread_self) {
        ultramodern::schedule_running_thread(PASS_RDRAM t_);
        ultramodern::check_running_queue(PASS_RDRAM1);
    }
    // Otherwise, immediately start the thread and terminate this one.
    else {
        t->state = OSThreadState::QUEUED;
        resume_thread(t);
        //throw ultramodern::thread_terminated{};
    }
}

extern "C" void osCreateThread(RDRAM_ARG PTR(OSThread) t_, OSId id, PTR(thread_func_t) entrypoint, PTR(void) arg, PTR(void) sp, OSPri pri) {
    debug_printf("[os] Create Thread %d\n", id);
    const auto snapCreateStart = std::chrono::steady_clock::now();
    OSThread *t = TO_PTR(OSThread, t_);
    
    t->next = NULLPTR;
    t->queue = NULLPTR;
    t->priority = pri;
    t->id = id;
    t->state = OSThreadState::STOPPED;
    t->sp = sp - 0x10; // Set up the first stack frame

    {
        std::lock_guard<std::mutex> lock(snap_thread_registry_mutex);
        bool known = false;
        for (PTR(OSThread) seen : snap_thread_registry) {
            known = known || (seen == t_);
        }
        if (!known) {
            snap_thread_registry.push_back(t_);
        }
    }

    // Hand the thread body to a parked pool worker instead of birthing a
    // host thread: the handshake below used to include a real thread
    // creation and measured ~3ms; a semaphore wake is microseconds. The
    // context's host_thread member stays default-constructed -- pooled
    // workers are never owned by a context and never joined.
    UltraThreadContext* context = new UltraThreadContext{};
    t->context = context;
    PooledHostThread* worker = pool_take_or_create();
    worker->task = [=]() {
        _thread_func(PASS_RDRAM t_, entrypoint, arg, context);
    };
    worker->taskReady.signal();

    // Wait until the thread is initialized to indicate that it's ready to be started.
    context->initialized.wait();
    debug_printf("[os] Thread %d is ready to be started\n", t->id);

    snap_thread_create_nanos.fetch_add(
        std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::steady_clock::now() - snapCreateStart).count(),
        std::memory_order_relaxed);
    snap_thread_create_count.fetch_add(1, std::memory_order_relaxed);
}

// Pokemon Snap port: one line per guest thread, for the stall report. The
// structs are read as they are, without the scheduler's locks: the report
// runs when the game has stopped, and a lock a stuck thread holds would
// stop the reporter too. A message queue's list of threads blocked on
// receiving is at the queue's own address, the list blocked on sending
// four bytes in (OSMesgQueue, ultra64.h), which is how a queue is named.
extern "C" void snap_dump_game_threads(uint8_t* rdram) {
    std::vector<PTR(OSThread)> threads;
    {
        std::lock_guard<std::mutex> lock(snap_thread_registry_mutex);
        threads = snap_thread_registry;
    }
    for (PTR(OSThread) t_ : threads) {
        const OSThread* t = TO_PTR(OSThread, t_);
        const char* state = "in an unknown state";
        switch (t->state) {
            case OSThreadState::STOPPED: state = "stopped"; break;
            case OSThreadState::QUEUED: state = "queued to run"; break;
            case OSThreadState::RUNNING: state = "running"; break;
            case OSThreadState::BLOCKED: state = "blocked"; break;
        }
        if (t->queue == ultramodern::running_queue) {
            printf("[SNAP-HANG]   thread %d (pri %d) %s, in the running queue\n", (int)t->id, (int)t->priority, state);
        }
        else if (t->queue != NULLPTR) {
            const uint32_t list = (uint32_t)t->queue;
            printf("[SNAP-HANG]   thread %d (pri %d) %s on message queue 0x%08X (%s)\n", (int)t->id, (int)t->priority, state,
                   list & ~7u, ((list & 7u) == 4u) ? "send" : "receive");
        }
        else {
            printf("[SNAP-HANG]   thread %d (pri %d) %s\n", (int)t->id, (int)t->priority, state);
        }
    }
    fflush(stdout);
}

extern "C" void osStopThread(RDRAM_ARG PTR(OSThread) t_) {
    if (t_ == NULLPTR) {
        t_ = thread_self;
    }
    // Check if the thread is stopping itself (arg is null or thread_self).
    if (t_ == thread_self) {
        ultramodern::run_next_thread_and_wait(PASS_RDRAM1);
    }
    else {
        assert(false);
    }
}

extern "C" void osDestroyThread(RDRAM_ARG PTR(OSThread) t_) {
    if (t_ == NULLPTR) {
        t_ = thread_self;
    }
    OSThread* t = TO_PTR(OSThread, t_);
    // Check if the thread is destroying itself (arg is null or thread_self)
    if (t_ == thread_self) {
        // Generated code on the stack cannot be unwound through: go back to
        // _thread_func without unwinding (see register_generated_code).
        if (generated_code_on_stack()) {
            static std::atomic<bool> said{false};
            if (!said.exchange(true)) {
                printf("[SNAP-MODS] a process ended with a mod's code on its stack; its thread returned without unwinding (said once)\n");
                fflush(stdout);
            }
            thread_exit_taken = true;
#ifdef _WIN32
            RtlRestoreContext(&thread_exit->context, nullptr);
#else
            siglongjmp(thread_exit->jump, 1);
#endif
        }
        throw ultramodern::thread_terminated{};
    }
    // Otherwise if the thread isn't stopped, remove it from its currrent queue., 
    if (t->state != OSThreadState::STOPPED) {
        ultramodern::thread_queue_remove(PASS_RDRAM t->queue, t_);
    }
    // Check if the thread has already been destroyed to prevent destroying it again.
    UltraThreadContext* cur_context = t->context;
    if (cur_context != nullptr) {
        // Mark the target thread as destroyed and resume it. When it starts it'll check this and terminate itself instead of resuming.
        t->context = nullptr;
        cur_context->running.signal();
    }
}

extern "C" void osSetThreadPri(RDRAM_ARG PTR(OSThread) t_, OSPri pri) {
    if (t_ == NULLPTR) {
        t_ = thread_self;
    }
    OSThread* t = TO_PTR(OSThread, t_);

    if (t->priority != pri) {
        t->priority = pri;

        if (t_ != ultramodern::this_thread() && t->state != OSThreadState::STOPPED) {
            ultramodern::thread_queue_remove(PASS_RDRAM t->queue, t_);
            ultramodern::thread_queue_insert(PASS_RDRAM t->queue, t_);
        }

        ultramodern::check_running_queue(PASS_RDRAM1);
    }
}

extern "C" OSPri osGetThreadPri(RDRAM_ARG PTR(OSThread) t) {
    if (t == NULLPTR) {
        t = thread_self;
    }
    return TO_PTR(OSThread, t)->priority;
}

extern "C" OSId osGetThreadId(RDRAM_ARG PTR(OSThread) t) {
    if (t == NULLPTR) {
        t = thread_self;
    }
    return TO_PTR(OSThread, t)->id;
}

PTR(OSThread) ultramodern::this_thread() {
    return thread_self;
}

static std::thread thread_cleaner_thread;
static moodycamel::BlockingConcurrentQueue<UltraThreadContext*> deleted_threads{};
extern std::atomic_bool exited;

void thread_cleaner_func() {
    using namespace std::chrono_literals;
    while (!exited) {
        UltraThreadContext* to_delete;
        if (deleted_threads.wait_dequeue_timed(to_delete, 10ms)) {
            debug_printf("[Cleanup] Deleting thread context %p\n", to_delete);

            // Pooled contexts never own a host thread; joining a default
            // std::thread throws.
            if (to_delete->host_thread.joinable()) {
                to_delete->host_thread.join();
            }
            delete to_delete;
        }
    }
}

void ultramodern::init_thread_cleanup() {
    thread_cleaner_thread = std::thread{thread_cleaner_func};
    // The game creates a dozen coroutine threads at every course block
    // boundary; have that many workers parked before it ever asks.
    ultramodern::prewarm_thread_pool(48);
}

void ultramodern::cleanup_thread(UltraThreadContext *cur_context) {
    deleted_threads.enqueue(cur_context);
}

void ultramodern::join_thread_cleaner_thread() {
    thread_cleaner_thread.join();
}

#if defined(__APPLE__)
#include <pthread.h>

// A pthread with the game's stack running a heap-held function, which it
// deletes when done. False when the thread could not be created.
static bool start_big_stack_pthread(std::function<void()>* heapFunc, bool detached, pthread_t* out) {
    pthread_attr_t attr;
    if (pthread_attr_init(&attr) != 0) {
        return false;
    }
    pthread_attr_setstacksize(&attr, ultramodern::threads::game_host_stack_bytes);
    pthread_attr_setdetachstate(&attr, detached ? PTHREAD_CREATE_DETACHED : PTHREAD_CREATE_JOINABLE);
    pthread_t thread;
    const int err = pthread_create(&thread, &attr, [](void* arg) -> void* {
        std::function<void()>* f = static_cast<std::function<void()>*>(arg);
        (*f)();
        delete f;
        return nullptr;
    }, heapFunc);
    pthread_attr_destroy(&attr);
    if (err != 0) {
        fprintf(stderr, "[Thread] a game thread with an %zu-byte stack could not be created (error %d); "
                "it runs on a default thread instead\n", ultramodern::threads::game_host_stack_bytes, err);
        return false;
    }
    if (out != nullptr) {
        *out = thread;
    }
    return true;
}

std::thread ultramodern::threads::make_game_host_thread(std::function<void()> func) {
    return std::thread{[func = std::move(func)]() mutable {
        std::function<void()>* heapFunc = new std::function<void()>(std::move(func));
        pthread_t inner;
        if (start_big_stack_pthread(heapFunc, false, &inner)) {
            pthread_join(inner, nullptr);
        } else {
            (*heapFunc)();
            delete heapFunc;
        }
    }};
}

void ultramodern::threads::start_detached_game_host_thread(std::function<void()> func) {
    std::function<void()>* heapFunc = new std::function<void()>(std::move(func));
    if (!start_big_stack_pthread(heapFunc, true, nullptr)) {
        std::function<void()> fallback = std::move(*heapFunc);
        delete heapFunc;
        std::thread{std::move(fallback)}.detach();
    }
}
#else
std::thread ultramodern::threads::make_game_host_thread(std::function<void()> func) {
    return std::thread{std::move(func)};
}

void ultramodern::threads::start_detached_game_host_thread(std::function<void()> func) {
    std::thread{std::move(func)}.detach();
}
#endif
