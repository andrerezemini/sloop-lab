/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Charles Vestal: from X0X (github.com/charlesvestal/fm1-x0x, commit 00bff73),
 * after Melodee's dual-core audio by Kerem Kilic (github.com/keremimo/melodee). Brought into SLOOP
 * by the sloop-lab dualcore branch (github.com/andrerezemini/sloop-lab). Modified after 2.7.1 (sloop-lab
 * fix-core): the mailbox of X0X 1.0.5+ / Felucca 1.6.1 (Leo Kuroshita, Hügelton Instruments) with Jangada's
 * layout (zednaked, github.com/zednaked/jangada), Melodee's EMU guards for core 1, the fault park and the
 * stack check. */
/* The AC79's second core (docs/plans/2026-10-04-dual-core-and-clock.md), with what Melodee's
 * dual-core audio (github.com/keremimo/melodee, GPL-3.0) found on the device.
 *
 * Start (the AC79 SDK's EnableOtherCpu): core 1's interrupt bank off (0x1EEF300..), its entry
 * address into the word at 0x01C7FFF8, bit 3 of 0x10008 held while it starts, C1_CON (0x1EEE004)
 * bit 3 set (enabled) and bit 1 cleared (out of hold). It boots through the chip's ROM, which the
 * PC limits refuse, so they are open until it has reported in from fm1_c1_main. Hold is bit 1
 * set, then bit 3 cleared.
 *
 * Work: one job at a time, a function and an argument; core 0 hands it over (mb.job) and waits
 * for it (mb.done), single writer each way, csync around each (the cores share the caches).
 * (after 2.7.1) The mailbox of X0X 1.0.5+ / Felucca 1.6.1 (after Jangada's measurements, fm1-x0x#10, and
 * Melodee#21): on some FM-1s core 1 reads words that are not in RAM, so it runs only the next job
 * (done + 1, done kept in a register) and only when the check word core 0 wrote with it matches;
 * anything else it counts (mb.bad) and leaves, and core 0 holds it at its next wait: one core from then
 * on. Each core's words on cache lines of their own (Jangada's layout). Its own stack limit in its EMU
 * bank (Melodee, X0X, Felucca): a job that goes within 256 bytes of the stack's end traps on core 1,
 * which parks (fm1_c1_fault) and is held too.
 *
 * Flash: the waiting loop is in RAM (.c1_text), so an idle core 1 never fetches from the flash;
 * it works only for the audio interrupt, which waits for it before it returns, and core 0 turns
 * the flash off only with interrupts off. So the flash is never off under it. */
#pragma once
#include <stdint.h>

#define FM1_C1_CON (*(volatile uint32_t *)0x1EEE004u)
#define FM1_C1_CLK (*(volatile uint32_t *)0x00010008u)
#define FM1_C1_T4 (*(volatile uint32_t *)0x10804u)       /* TIMER4, 24 MHz */
#define FM1_C1_SYNC() __asm__ volatile("csync" ::: "memory")

extern void fm1_c1_entry(void);
extern uint32_t _c1_ustack[], _c1_red[], _c1_sstack_top[];
#define FM1_C1_STACK_WORDS (2816u / 4u)
#define FM1_C1_RED_WORDS (256u / 4u)             /* app.ld: the red zone right below the stack */
#define FM1_C1_MARK 0x43314D4Bu                  /* "C1MK": how deep core 1's stack has gone */
#define FM1_C1_FAULT 0xC1DEAD00u                 /* fm1_c1_trace after a fault on core 1 (| the vector) */
typedef struct {
    volatile uint32_t job, arg;                 /* core 0: job + 1 hands fn(arg) over */
    void (*volatile fn)(uint32_t);
    volatile uint32_t check;                    /* FM1_C1_CHECK of fn, arg and job */
    uint32_t pad0[12];
    volatile uint32_t alive, done;              /* core 1: started; the last job it finished */
    volatile uint32_t bad, bad_job, bad_done, bad_fn;   /* jobs it would not run: how many, the last one (console) */
    uint32_t pad1[10];
} fm1_c1_mb_t;
static fm1_c1_mb_t fm1_c1_mb __attribute__((aligned(64)));
#define FM1_C1_CHECK(fn, arg, job) ((uint32_t)(uintptr_t)(fn) ^ (arg) ^ (job) * 0x9E3779B1u)
volatile uint32_t fm1_c1_trace;                 /* breadcrumbs (fm1_cpu1.S, fm1_c1_main, fm1_c1_fault) */
volatile uint32_t fm1_c1_fault_pc;              /* where core 1 faulted (fm1_c1_fault) */
static uint8_t fm1_c1_on;                       /* started and answering: core 0 may hand it work */

void fm1_c1_main(void);
void __attribute__((section(".c1_text"), noreturn, used)) fm1_c1_main(void)
{
    /* (after 2.7.1) its own guards, as Melodee, X0X and Felucca set them: a stack limit in core 1's EMU bank (core 0's
     * is at 0x1EEF0D0) over both its stacks less the user stack's lowest 256 bytes, EMU_CON bit 3 (the limit) on,
     * bit 2 (the divide-by-zero trap, off on core 0 too: fm1_irq.h) and bits 16..20 off, whatever a reset or the
     * firmware before left there */
    uint32_t lo = (uint32_t)(uintptr_t)_c1_ustack + 256u, hi = (uint32_t)(uintptr_t)_c1_sstack_top - 1u;
    uint32_t done, last_bad = 0xFFFFFFFFu;
    *(volatile uint32_t *)0x1EEF2D4u = 0xFFFFFFFFu;     /* EMU_MSG: what was left, cleared */
    *(volatile uint32_t *)0x1EEF2D8u = hi;
    *(volatile uint32_t *)0x1EEF2DCu = lo;
    *(volatile uint32_t *)0x1EEF2E0u = hi;
    *(volatile uint32_t *)0x1EEF2E4u = lo;
    *(volatile uint32_t *)0x1EEF2D0u = (*(volatile uint32_t *)0x1EEF2D0u & ~((1u << 2) | (0x1Fu << 16))) | (1u << 3);
    fm1_c1_trace = 0xC1000002;
    done = fm1_c1_mb.done;
    fm1_c1_mb.alive = 1;
    FM1_C1_SYNC();
    for (;;) {
        uint32_t j = fm1_c1_mb.job;
        FM1_C1_SYNC();
        if (j == done)
            continue;
        if (j == done + 1u) {
            void (*fn)(uint32_t) = fm1_c1_mb.fn;
            uint32_t arg = fm1_c1_mb.arg;
            if (fn && fm1_c1_mb.check == FM1_C1_CHECK(fn, arg, j)) {
                fn(arg);                        /* the work itself runs from the flash */
                done = j;
                FM1_C1_SYNC();
                fm1_c1_mb.done = j;
                FM1_C1_SYNC();
                continue;
            }
            fm1_c1_mb.bad_fn = (uint32_t)(uintptr_t)fn;
        }
        if (j != last_bad) {                    /* each wrong value once (it stays until core 0 holds it) */
            last_bad = j;
            fm1_c1_mb.bad_job = j;
            fm1_c1_mb.bad_done = done;
            FM1_C1_SYNC();
            fm1_c1_mb.bad++;
            FM1_C1_SYNC();
        }
    }
}

/* a fault taken on core 1 (fm1_irq.h fm1_fault_c, after cnum): core 1 must not run core 0's crash report (it would
 * stop the audio and, in fm1_reboot, hold itself before the reset), so it leaves a breadcrumb and parks here, in RAM;
 * core 0 sees the crumb (fm1_cpu1_wait, cpu1.c cpu1_guard_half), holds it and renders on its own */
void fm1_c1_fault(uint32_t vec, uint32_t pc);
void __attribute__((section(".c1_text"), noreturn, used)) fm1_c1_fault(uint32_t vec, uint32_t pc)
{
    fm1_c1_fault_pc = pc;
    fm1_c1_trace = FM1_C1_FAULT | (vec & 0xFFu);
    FM1_C1_SYNC();
    for (;;)
        ;
}

static void fm1_cpu1_hold(void)
{
    fm1_cpu1_halt();                            /* (2.7.1, fm1_sys.h) the bus-invalid traps off around the hold */
    FM1_C1_SYNC();
    fm1_c1_on = 0;
}

/* 0 = core 1 runs and answered; else it is held again. The top of RAM must be writable
 * (fm1_guard_unlock_top) and the PC limits open (fm1_guard_pc_open): the caller's */
static int fm1_cpu1_start(void)
{
    uint32_t saved, t0, i;
    if (fm1_c1_on)
        return 0;
    fm1_cpu1_hold();
    for (i = 0; i < FM1_C1_RED_WORDS; i++)      /* the red zone below the stack too (fm1_cpu1_stack_ok) */
        _c1_red[i] = FM1_C1_MARK;
    for (i = 0; i < FM1_C1_STACK_WORDS; i++)
        _c1_ustack[i] = FM1_C1_MARK;
    fm1_c1_mb.alive = 0;
    fm1_c1_mb.bad = 0;
    fm1_c1_trace = 0;
    fm1_c1_mb.done = fm1_c1_mb.job;
    fm1_c1_mb.fn = 0;
    FM1_C1_SYNC();
    for (i = 0; i < 32u; i++)                   /* its interrupt bank: all off (core 0's is at 0x1EEF100) */
        *(volatile uint32_t *)(0x1EEF300u + 4u * i) = 0;
    *(volatile uint32_t *)0x01C7FFF8u = (uint32_t)(uintptr_t)&fm1_c1_entry;
    saved = FM1_C1_CLK;
    FM1_C1_CLK = saved | 0x8u;
    FM1_C1_SYNC();
    FM1_C1_CON |= 0x8u;
    FM1_C1_CON &= ~0x2u;
    t0 = FM1_C1_T4;
    while (!fm1_c1_mb.alive && FM1_C1_T4 - t0 < 24000u * 20u)   /* 20 ms */
        FM1_C1_SYNC();
    FM1_C1_CLK = saved;
    FM1_C1_SYNC();
    if (!fm1_c1_mb.alive) {
        fm1_cpu1_hold();
        return -1;
    }
    fm1_c1_on = 1;
    return 0;
}

/* hand core 1 a job: 1 = it took it (fm1_cpu1_wait before touching what it works on) */
static inline int fm1_cpu1_run(void (*fn)(uint32_t), uint32_t arg)
{
    uint32_t job;
    if (!fm1_c1_on)
        return 0;
    job = fm1_c1_mb.job + 1u;
    fm1_c1_mb.fn = fn;
    fm1_c1_mb.arg = arg;
    fm1_c1_mb.check = FM1_C1_CHECK(fn, arg, job);   /* written before the job (Felucca 1.6.1) */
    FM1_C1_SYNC();
    fm1_c1_mb.job = job;
    FM1_C1_SYNC();
    return 1;
}

/* 0 = done; -1 = it read a wrong job, faulted or did not finish in time: core 1 is held from now on (the caller
 * does the work) */
static inline int fm1_cpu1_wait(void)
{
    uint32_t t0 = FM1_C1_T4;
    for (;;) {
        FM1_C1_SYNC();
        if (fm1_c1_mb.bad || (fm1_c1_trace & 0xFFFFFF00u) == FM1_C1_FAULT) {   /* a wrong job read, a fault: at once */
            fm1_cpu1_hold();
            return -1;
        }
        if (fm1_c1_mb.done == fm1_c1_mb.job) {
            FM1_C1_SYNC();                      /* (after 2.7.1, as Felucca PR 267) its writes seen before acc[1] is read */
            return 0;
        }
        if (FM1_C1_T4 - t0 > 24000u * 4u) {     /* 4 ms (as Jangada and Felucca 1.6.1): far past any block's work
                                                 * (0.73 ms), less than a half (5.8 ms): one late half at most */
            fm1_cpu1_hold();
            return -1;
        }
    }
}

/* the red zone below core 1's user stack and the stack's lowest 256 bytes still hold the mark fm1_cpu1_start wrote:
 * 0 = a job has gone within 256 bytes of the stack's end (or past it). Read by core 0 when core 1 is idle (after a
 * half: its waiting loop uses only the top of the stack). cpu1.c then holds core 1 for good: one core, as after a
 * timeout, before an overflow can reach the audio buffers below */
static int fm1_cpu1_stack_ok(void)
{
    uint32_t i, bad = 0;
    for (i = 0; i < FM1_C1_RED_WORDS; i++)
        bad |= (_c1_red[i] ^ FM1_C1_MARK) | (_c1_ustack[i] ^ FM1_C1_MARK);
    return bad == 0;
}
