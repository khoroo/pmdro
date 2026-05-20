#include "../vendor/termbox2.h"

#include <signal.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define WORK_DUR      (25 * 60)
#define SHORT_BREAK   (5 * 60)
#define LONG_BREAK    (15 * 60)
#define CYCLES_BEFORE_LONG 4

typedef enum { TYPE_WORK, TYPE_SHORT, TYPE_LONG } Mode;

static volatile sig_atomic_t running = 1;

static void on_sigint(int sig) { (void)sig; running = 0; }

static const char *mode_label(Mode m) {
    return m == TYPE_WORK ? "WORK" : m == TYPE_SHORT ? "SHORT BREAK" : "LONG BREAK";
}

static const char *mode_label_short(Mode m) {
    return m == TYPE_WORK ? "WORK" : m == TYPE_SHORT ? "SB" : "LB";
}

static bool is_compact(void) {
    return tb_width() < 60 || tb_height() < 12;
}

static int mode_dur(Mode m) {
    return m == TYPE_WORK ? WORK_DUR : m == TYPE_SHORT ? SHORT_BREAK : LONG_BREAK;
}

static uintattr_t mode_fg(Mode m) {
    return m == TYPE_WORK ? TB_RED : m == TYPE_SHORT ? TB_GREEN : TB_CYAN;
}

static void send_notification(const char *title, const char *msg) {
    printf("\033]99;i=pmdro:n=%s;%s\007", title, msg);
    fflush(stdout);
}

typedef struct {
    Mode mode;
    int duration;
    int remaining;
    bool paused;
    bool auto_cycle;
    bool show_hint;
    bool notify_enabled;
    bool notified;
    int cycles;
    struct timespec last_tick;
} Timer;

static void timer_init(Timer *t) {
    t->mode = TYPE_WORK;
    t->duration = WORK_DUR;
    t->remaining = WORK_DUR;
    t->paused = false;
    t->auto_cycle = true;
    t->show_hint = false;
    t->notify_enabled = true;
    t->notified = false;
    t->cycles = 0;
    clock_gettime(CLOCK_MONOTONIC, &t->last_tick);
}

static void timer_reset(Timer *t) {
    t->remaining = t->duration;
    t->paused = false;
    t->notified = false;
    clock_gettime(CLOCK_MONOTONIC, &t->last_tick);
}

static void timer_next(Timer *t) {
    if (t->auto_cycle) {
        if (t->mode == TYPE_WORK) {
            t->cycles++;
            if (t->cycles >= CYCLES_BEFORE_LONG) {
                t->mode = TYPE_LONG;
                t->cycles = 0;
            } else {
                t->mode = TYPE_SHORT;
            }
        } else {
            t->mode = TYPE_WORK;
        }
    } else {
        t->mode = (t->mode + 1) % 3;
    }
    t->duration = mode_dur(t->mode);
    t->remaining = t->duration;
    t->paused = false;
    t->notified = false;
    clock_gettime(CLOCK_MONOTONIC, &t->last_tick);
}

static void timer_tick(Timer *t) {
    if (t->paused || t->remaining <= 0) return;

    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);

    double elapsed = (now.tv_sec - t->last_tick.tv_sec)
                   + (now.tv_nsec - t->last_tick.tv_nsec) / 1e9;

    if (elapsed >= 1.0) {
        int s = (int)elapsed;
        if (s > t->remaining) s = t->remaining;
        t->remaining -= s;
        t->last_tick = now;
        if (t->remaining <= 0) {
            if (t->notify_enabled && !t->notified) {
                send_notification("Pomodoro", mode_label(t->mode));
                t->notified = true;
            }
            if (t->auto_cycle) timer_next(t);
        }
    }
}

static void print_centered(int row, const char *text, uintattr_t fg, uintattr_t bg) {
    int len = (int)strlen(text);
    int col = (tb_width() - len) / 2;
    if (col < 0) col = 0;
    tb_print(col, row, fg, bg, text);
}

static void draw_timer(int row, int sec, uintattr_t fg, uintattr_t bg) {
    char buf[16];
    snprintf(buf, sizeof buf, "%02d:%02d", sec / 60, sec % 60);
    int len = (int)strlen(buf);
    int col = (tb_width() - len) / 2;
    if (col < 0) col = 0;
    for (int i = 0; i < len; i++)
        tb_set_cell(col + i, row, buf[i], fg | TB_BOLD, bg);
}

static void draw_line(int row, uintattr_t fg, uintattr_t bg) {
    int w = tb_width();
    int s = (w - 30) / 2; if (s < 0) s = 0;
    int e = s + 30; if (e > w) e = w;
    for (int i = s; i < e; i++) tb_set_cell(i, row, '-', fg, bg);
}

static void draw_compact(const Timer *t) {
    int h = tb_height();
    uintattr_t fg = mode_fg(t->mode);
    uintattr_t bg = TB_DEFAULT;

    draw_timer(h / 2 - 1, t->remaining, fg | TB_BOLD, bg);
    print_centered(h / 2, mode_label_short(t->mode), fg, bg);

    if (t->auto_cycle)
        print_centered(h / 2 + 1, "auto", TB_DEFAULT, bg);
    else
        print_centered(h / 2 + 1, "manual", TB_DEFAULT, bg);

    if (t->show_hint)
        print_centered(h / 2 + 2, " sp r t a n h q", TB_DEFAULT, bg);
}

static void render(const Timer *t) {
    tb_clear();

    uintattr_t fg = mode_fg(t->mode);
    uintattr_t bg = TB_DEFAULT;

    if (is_compact()) {
        draw_compact(t);
    } else {
        int h = tb_height();
        int mid = h / 2;

        draw_line(mid - 2, fg, bg);
        draw_timer(mid, t->remaining, fg, bg);
        draw_line(mid + 2, fg, bg);

        print_centered(h - 4, mode_label(t->mode), fg | TB_BOLD, bg);

        if (t->auto_cycle)
            print_centered(h - 3, "auto", TB_DEFAULT, bg);
        else
            print_centered(h - 3, "manual", TB_DEFAULT, bg);

        if (t->show_hint) {
            const char *hint = "space=pause  r=reset  t=mode  a=auto  n=";
            const char *n_label = t->notify_enabled ? "notify" : "notify";
            const char *hint_end = "  h=hint  q=quit";
            int n_len = (int)strlen(n_label);
            int hint_len = (int)strlen(hint);
            int end_len = (int)strlen(hint_end);
            int total = hint_len + n_len + end_len;
            int col = (tb_width() - total) / 2;
            if (col < 0) col = 0;
            tb_print(col, h - 2, TB_DEFAULT, bg, hint);
            tb_print(col + hint_len, h - 2, t->notify_enabled ? TB_GREEN : TB_DEFAULT, bg, n_label);
            tb_print(col + hint_len + n_len, h - 2, TB_DEFAULT, bg, hint_end);
        }
    }

    if (t->paused)
        print_centered(tb_height() / 2, "[ PAUSED ]", TB_YELLOW | TB_BOLD, bg);

    tb_present();
}

static void handle_key(Timer *t, struct tb_event *ev) {
    uint32_t ch = ev->ch;
    uint16_t key = ev->key;

    if (ch == ' ' || key == TB_KEY_SPACE) {
        t->paused = !t->paused;
        if (!t->paused) clock_gettime(CLOCK_MONOTONIC, &t->last_tick);
    } else if (ch == 'r' || ch == 'R') {
        timer_reset(t);
    } else if (ch == 't' || ch == 'T') {
        t->auto_cycle = false;
        timer_next(t);
    } else if (ch == 'a' || ch == 'A') {
        t->auto_cycle = !t->auto_cycle;
    } else if (ch == 'h' || ch == 'H') {
        t->show_hint = !t->show_hint;
    } else if (ch == 'n' || ch == 'N') {
        t->notify_enabled = !t->notify_enabled;
        t->notified = false;
    } else if (ch == 'q' || ch == 'Q' || key == TB_KEY_ESC) {
        running = 0;
    }
}

int main(void) {
    struct sigaction sa = { .sa_handler = on_sigint };
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    sigaction(SIGINT, &sa, nullptr);

    int rc = tb_init();
    if (rc < 0) {
        fprintf(stderr, "termbox2 init failed: %d\n", rc);
        return EXIT_FAILURE;
    }

    tb_hide_cursor();

    Timer timer;
    timer_init(&timer);

    while (running) {
        render(&timer);
        timer_tick(&timer);

        struct tb_event ev;
        int ret = tb_peek_event(&ev, 200);
        if (ret == TB_OK && ev.type == TB_EVENT_KEY)
            handle_key(&timer, &ev);
    }

    tb_shutdown();
    return EXIT_SUCCESS;
}
