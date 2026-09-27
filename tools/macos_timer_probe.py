#!/usr/bin/env python3
"""How precisely this Mac can wait 16.7 milliseconds, the VI retrace's period.

    python3 tools/macos_timer_probe.py

The runtime paces the game's 60 Hz retrace with std::this_thread::sleep_until,
which macOS serves with nanosleep. On a real Mac that wakes within a fraction
of a millisecond; a virtual Mac (GitHub's runners are Apple Virtualization
guests) may not. Sixty waits of one retrace period, three ways -- the plain
sleep, mach_wait_until, and mach_wait_until on a thread given the
time-constraint scheduling policy that audio code uses -- and for each the
mean, median and worst overshoot in milliseconds. A mean under one
millisecond is a machine that can hold the console's pace; tens of
milliseconds is one that cannot, whatever the port does.
"""
import ctypes
import subprocess
import sys
import threading
import time

PERIOD_NS = 16_666_667
ROUNDS = 60


def main():
    if sys.platform != 'darwin':
        print('this probe is for macOS')
        return 0
    libc = ctypes.CDLL('/usr/lib/libSystem.B.dylib')

    class TimebaseInfo(ctypes.Structure):
        _fields_ = [('numer', ctypes.c_uint32), ('denom', ctypes.c_uint32)]

    tb = TimebaseInfo()
    libc.mach_timebase_info(ctypes.byref(tb))
    libc.mach_absolute_time.restype = ctypes.c_uint64
    libc.mach_wait_until.argtypes = [ctypes.c_uint64]
    libc.mach_thread_self.restype = ctypes.c_uint32

    def ns_to_abs(ns):
        return int(ns * tb.denom / tb.numer)

    def abs_to_ms(a):
        return a * tb.numer / tb.denom / 1e6

    def probe(wait):
        over = []
        for _ in range(ROUNDS):
            t0 = libc.mach_absolute_time()
            target = t0 + ns_to_abs(PERIOD_NS)
            wait(target)
            over.append(abs_to_ms(libc.mach_absolute_time() - target))
        over.sort()
        return sum(over) / len(over), over[len(over) // 2], over[-1]

    def plain_sleep(target):
        remaining = abs_to_ms(target - libc.mach_absolute_time()) / 1e3
        if remaining > 0:
            time.sleep(remaining)

    def mach_wait(target):
        libc.mach_wait_until(target)

    results = {}
    results['sleep_until (nanosleep)'] = probe(plain_sleep)
    results['mach_wait_until'] = probe(mach_wait)

    class TimeConstraint(ctypes.Structure):
        _fields_ = [('period', ctypes.c_uint32), ('computation', ctypes.c_uint32),
                    ('constraint', ctypes.c_uint32), ('preemptible', ctypes.c_int32)]

    def constrained():
        policy = TimeConstraint(ns_to_abs(PERIOD_NS), ns_to_abs(1_000_000), ns_to_abs(5_000_000), 1)
        r = libc.thread_policy_set(libc.mach_thread_self(), 2, ctypes.byref(policy), 4)
        results['mach_wait_until, time-constraint thread (policy set: %s)' % ('ok' if r == 0 else 'refused %d' % r)] = probe(mach_wait)

    th = threading.Thread(target=constrained)
    th.start()
    th.join()

    for cmd in (['sysctl', '-n', 'kern.hv_vmm_present'], ['sysctl', '-n', 'hw.model'], ['sysctl', '-n', 'hw.ncpu']):
        try:
            print('%s: %s' % (cmd[-1], subprocess.run(cmd, capture_output=True, text=True).stdout.strip()))
        except OSError:
            pass
    for name, (mean, median, worst) in results.items():
        print('%-64s overshoot mean %6.2f ms, median %6.2f ms, worst %7.2f ms' % (name, mean, median, worst))
    return 0


if __name__ == '__main__':
    sys.exit(main())
