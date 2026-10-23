/*
 * uipad: a game pad that is not there, for tools/android_check.py's pad stage.
 *
 * On an Android emulator, with adb as root, this makes a pad through Linux's
 * uinput: the kernel sees a new input device, Android's input system takes
 * it for a game pad (the ids are an Xbox 360 controller's, whose key layout
 * every Android ships), and SDL in the app opens it as it would a real one.
 * So the whole path from a pad's button to the game is the real one; only
 * the thumb is missing. A phone without root cannot run it.
 *
 *     uipad <fifo>
 *
 * After "uipad ready" it reads commands, one a line, from the named FIFO,
 * opening it again whenever a writer closes it:
 *
 *     press NAME MS     press NAME, hold it MS milliseconds, let go
 *     quit              remove the pad and end
 *
 * NAME: A B X Y LB RB LT RT START BACK LS RS (the buttons and triggers),
 * DU DD DL DR (the D-pad), LSU LSD LSL LSR and RSU RSD RSL RSR (the left and
 * right sticks pushed fully up, down, left, right). The Xbox button is left
 * out: Android's layout sends it as the Home key. Every press and release is
 * printed with the milliseconds since the start.
 *
 * Built with the NDK's clang for the emulator (BUILDING.md, step 16):
 *     $NDK/toolchains/llvm/prebuilt/linux-x86_64/bin/x86_64-linux-android29-clang -O2 -o uipad tools/android/uipad.c
 */
#include <errno.h>
#include <fcntl.h>
#include <linux/uinput.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <time.h>
#include <unistd.h>

static int fd = -1;
static struct timespec start;

static long ms_since_start(void) {
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    return (now.tv_sec - start.tv_sec) * 1000L + (now.tv_nsec - start.tv_nsec) / 1000000L;
}

static void emit(int type, int code, int value) {
    struct input_event ev;
    memset(&ev, 0, sizeof ev);
    ev.type = type;
    ev.code = code;
    ev.value = value;
    if (write(fd, &ev, sizeof ev) != (ssize_t)sizeof ev) {
        printf("uipad %ld write failed: %s\n", ms_since_start(), strerror(errno));
    }
}

static void sync_now(void) {
    emit(EV_SYN, SYN_REPORT, 0);
}

static void abs_setup(int code, int lo, int hi, int flat) {
    struct uinput_abs_setup a;
    memset(&a, 0, sizeof a);
    a.code = code;
    a.absinfo.minimum = lo;
    a.absinfo.maximum = hi;
    a.absinfo.flat = flat;
    ioctl(fd, UI_SET_ABSBIT, code);
    ioctl(fd, UI_ABS_SETUP, &a);
}

struct control {
    const char* name;
    int type;    /* EV_KEY or EV_ABS */
    int code;
    int on;      /* the value while pressed; the rest value is 0 */
};

static const struct control controls[] = {
    {"A", EV_KEY, BTN_A, 1},           {"B", EV_KEY, BTN_B, 1},
    {"X", EV_KEY, BTN_X, 1},           {"Y", EV_KEY, BTN_Y, 1},
    {"LB", EV_KEY, BTN_TL, 1},         {"RB", EV_KEY, BTN_TR, 1},
    {"BACK", EV_KEY, BTN_SELECT, 1},   {"START", EV_KEY, BTN_START, 1},
    {"LS", EV_KEY, BTN_THUMBL, 1},     {"RS", EV_KEY, BTN_THUMBR, 1},
    {"LT", EV_ABS, ABS_Z, 255},        {"RT", EV_ABS, ABS_RZ, 255},
    {"DU", EV_ABS, ABS_HAT0Y, -1},     {"DD", EV_ABS, ABS_HAT0Y, 1},
    {"DL", EV_ABS, ABS_HAT0X, -1},     {"DR", EV_ABS, ABS_HAT0X, 1},
    {"LSU", EV_ABS, ABS_Y, -32767},    {"LSD", EV_ABS, ABS_Y, 32767},
    {"LSL", EV_ABS, ABS_X, -32767},    {"LSR", EV_ABS, ABS_X, 32767},
    {"RSU", EV_ABS, ABS_RY, -32767},   {"RSD", EV_ABS, ABS_RY, 32767},
    {"RSL", EV_ABS, ABS_RX, -32767},   {"RSR", EV_ABS, ABS_RX, 32767},
};

static const struct control* find(const char* name) {
    for (size_t i = 0; i < sizeof controls / sizeof controls[0]; i++) {
        if (strcmp(controls[i].name, name) == 0) {
            return &controls[i];
        }
    }
    return NULL;
}

int main(int argc, char** argv) {
    if (argc != 2) {
        fprintf(stderr, "usage: uipad <fifo>\n");
        return 2;
    }
    setvbuf(stdout, NULL, _IOLBF, 0);
    clock_gettime(CLOCK_MONOTONIC, &start);
    fd = open("/dev/uinput", O_WRONLY | O_NONBLOCK);
    if (fd < 0) {
        printf("uipad cannot open /dev/uinput: %s (root is needed)\n", strerror(errno));
        return 1;
    }
    ioctl(fd, UI_SET_EVBIT, EV_KEY);
    ioctl(fd, UI_SET_EVBIT, EV_ABS);
    ioctl(fd, UI_SET_EVBIT, EV_SYN);
    for (size_t i = 0; i < sizeof controls / sizeof controls[0]; i++) {
        if (controls[i].type == EV_KEY) {
            ioctl(fd, UI_SET_KEYBIT, controls[i].code);
        }
    }
    abs_setup(ABS_X, -32768, 32767, 128);
    abs_setup(ABS_Y, -32768, 32767, 128);
    abs_setup(ABS_RX, -32768, 32767, 128);
    abs_setup(ABS_RY, -32768, 32767, 128);
    abs_setup(ABS_Z, 0, 255, 0);
    abs_setup(ABS_RZ, 0, 255, 0);
    abs_setup(ABS_HAT0X, -1, 1, 0);
    abs_setup(ABS_HAT0Y, -1, 1, 0);
    struct uinput_setup setup;
    memset(&setup, 0, sizeof setup);
    setup.id.bustype = BUS_USB;
    setup.id.vendor = 0x045E;     /* an Xbox 360 controller's ids */
    setup.id.product = 0x028E;
    setup.id.version = 0x0110;
    snprintf(setup.name, UINPUT_MAX_NAME_SIZE, "Snap64 Test Pad");
    if (ioctl(fd, UI_DEV_SETUP, &setup) < 0 || ioctl(fd, UI_DEV_CREATE) < 0) {
        printf("uipad cannot make the device: %s\n", strerror(errno));
        return 1;
    }
    printf("uipad %ld ready\n", ms_since_start());
    char line[128];
    for (;;) {
        FILE* in = fopen(argv[1], "r");
        if (in == NULL) {
            printf("uipad cannot open %s: %s\n", argv[1], strerror(errno));
            break;
        }
        int quit = 0;
        while (!quit && fgets(line, sizeof line, in) != NULL) {
            char word[16] = {0};
            char name[16] = {0};
            int ms = 0;
            const int got = sscanf(line, "%15s %15s %d", word, name, &ms);
            if (got >= 1 && strcmp(word, "quit") == 0) {
                quit = 1;
            } else if (got == 3 && strcmp(word, "press") == 0) {
                const struct control* c = find(name);
                if (c == NULL) {
                    printf("uipad %ld no such control: %s\n", ms_since_start(), name);
                    continue;
                }
                emit(c->type, c->code, c->on);
                sync_now();
                printf("uipad %ld press %s\n", ms_since_start(), name);
                usleep((useconds_t)ms * 1000);
                emit(c->type, c->code, 0);
                sync_now();
                printf("uipad %ld release %s\n", ms_since_start(), name);
            } else if (got >= 1) {
                printf("uipad %ld not understood: %s", ms_since_start(), line);
            }
        }
        fclose(in);
        if (quit) {
            break;
        }
    }
    ioctl(fd, UI_DEV_DESTROY);
    close(fd);
    printf("uipad %ld removed\n", ms_since_start());
    return 0;
}
