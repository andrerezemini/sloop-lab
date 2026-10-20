/* SPDX-License-Identifier: GPL-3.0-only */
/* Host test of the USB start and its retry in src/usb.c (usb_start, usb_retry) on a simulated controller: the SIE
 * answers only with a host on the cable (on the FM-1, powered on without one, it does not: usb.c), and each access
 * without an answer spins its 20,000 polls. The main loop is simulated at one pass every 16 ms (main.c: ~60 frames
 * a second), usb_poll (TIMER5) is called by hand.
 *   boot        without a host: USB stays off; with one: up at once
 *   no host     one try a second, and no main-loop pass waits in fm1_delay_ms (2.7.1 fix: the 25 ms wait of every
 *               try held the UI once a second on battery)
 *   plugged in  a host plugged in later is up within ~1 s; every attach comes >= 20 ms after the pads were
 *               detached (hal/fm1_usb.h)
 *   SIE lost    a session whose SIE stops answering (sie_rd sets usb.up = 0) is started again
 *   suspended   with usb.up = 1 and the bus idle (the host unplugged, or asleep: the firmware cannot tell them apart,
 *               there is no VBUS line) the retry leaves the controller alone, so a sleeping host finds the FM-1 */
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#define RING_PUBLISH() __asm__ volatile("" ::: "memory")
#define FELUCCA_OTA 0
#define FELUCCA_CDC 0
#define FELUCCA_UAC 0

/* hal/fm1_usb.h is read here, its register helpers renamed (never called), so the #include in usb.c finds it read
 * already (#pragma once) and usb.c calls the simulated controller below. The list must name every FM1_INLINE of
 * hal/fm1_usb.h: every helper of the header is marked deprecated here, so a call from usb.c to one the list misses
 * fails the build instead of reaching the registers on the host (a crash) */
#include "../firmware/hal/fm1_cc.h"
#undef FM1_INLINE
#define FM1_INLINE static inline __attribute__((deprecated("not simulated: add it to tests/usb_retry_test.c")))
#pragma GCC diagnostic error "-Wdeprecated-declarations"
#define fm1_usb_reset hw_usb_reset
#define fm1_usb_attach hw_usb_attach
#define fm1_usb_off hw_usb_off
#define fm1_usb_sie_on hw_usb_sie_on
#define fm1_usb_sie_wr_start hw_usb_sie_wr_start
#define fm1_usb_sie_rd_start hw_usb_sie_rd_start
#define fm1_usb_sie_done hw_usb_sie_done
#define fm1_usb_sie_data hw_usb_sie_data
#define fm1_usb_ep0_buf hw_usb_ep0_buf
#define fm1_usb_ep_txbuf hw_usb_ep_txbuf
#define fm1_usb_ep_rxbuf hw_usb_ep_rxbuf
#define fm1_usb_ep0_send hw_usb_ep0_send
#define fm1_usb_ep_send hw_usb_ep_send
#define fm1_usb_ep4_txbuf hw_usb_ep4_txbuf
#define fm1_usb_ep4_send hw_usb_ep4_send
#define fm1_usb_rx_sync hw_usb_rx_sync
#define fm1_usb_ep_enable hw_usb_ep_enable
#define fm1_usb_sof_take hw_usb_sof_take
#pragma GCC diagnostic ignored "-Wint-to-pointer-cast"   /* SIE register macros (never touched here) */
#include "../firmware/hal/fm1_usb.h"
#undef fm1_usb_reset
#undef fm1_usb_attach
#undef fm1_usb_off
#undef fm1_usb_sie_on
#undef fm1_usb_sie_wr_start
#undef fm1_usb_sie_rd_start
#undef fm1_usb_sie_done
#undef fm1_usb_sie_data
#undef fm1_usb_ep0_buf
#undef fm1_usb_ep_txbuf
#undef fm1_usb_ep_rxbuf
#undef fm1_usb_ep0_send
#undef fm1_usb_ep_send
#undef fm1_usb_ep4_txbuf
#undef fm1_usb_ep4_send
#undef fm1_usb_rx_sync
#undef fm1_usb_ep_enable
#undef fm1_usb_sof_take

/* the simulated controller */
static uint32_t now_us;                 /* the clock: the main loop and fm1_delay_ms move it */
static int host;                        /* a host on the cable: the SIE answers */
static uint32_t con0, attached;         /* CON0 (bit 2: the SIE out of reset); the pads attached (PHY, D+ pull-up) */
static uint32_t detached_at, attaches, early, dead, waited;
static uint8_t sie[32], sie_r;
static void fm1_usb_reset(void) { con0 = 0, attached = 0, detached_at = now_us; }
static void fm1_usb_off(void) { con0 = 0, attached = 0, detached_at = now_us; }
static void fm1_usb_attach(void *ep0)
{
    (void)ep0;
    if (attached || now_us - detached_at < 20000u)
        early++;                        /* hal/fm1_usb.h: detached (reset) >= 20 ms first */
    attached = 1, con0 = 0x3Du, attaches++;
}
static uint32_t fm1_usb_sie_on(void) { return con0 & 4u; }
static void fm1_usb_sie_wr_start(uint32_t r, uint32_t v)
{
    if (host)
        sie[r & 31u] = (uint8_t)v;
}
static void fm1_usb_sie_rd_start(uint32_t r) { sie_r = (uint8_t)(r & 31u); }
static uint32_t fm1_usb_sie_done(void)
{
    if (host)
        return 0x8000u;
    dead++;                             /* a poll without an answer */
    return 0;
}
static uint32_t fm1_usb_sie_data(void) { return sie[sie_r]; }
static void fm1_usb_ep0_buf(void *p) { (void)p; }
static void fm1_usb_ep_txbuf(uint32_t ep, void *p) { (void)ep, (void)p; }
static void fm1_usb_ep_rxbuf(uint32_t ep, void *p) { (void)ep, (void)p; }
static void fm1_usb_ep0_send(void *p, uint32_t n) { (void)p, (void)n; }
static void fm1_usb_ep_send(uint32_t ep, void *p, uint32_t n) { (void)ep, (void)p, (void)n; }
static void fm1_usb_ep4_txbuf(void *p) { (void)p; }
static void fm1_usb_ep4_send(void *p, uint32_t n) { (void)p, (void)n; }
static void fm1_usb_rx_sync(void) {}
static void fm1_usb_ep_enable(uint32_t eps) { (void)eps; }
static uint32_t fm1_usb_sof_take(void) { return 0; }
static void fm1_delay_ms(uint32_t ms) { now_us += ms * 1000u, waited += ms; }
#include "../firmware/src/usb.c"

static int fails;
static void check(const char *what, int ok)
{
    printf("%-72s %s\n", what, ok ? "ok" : "FAIL");
    if (!ok)
        fails++;
}

/* main-loop passes for ms milliseconds: usb_retry, then the rest of a 16 ms frame. The longest wait of a pass in
 * fm1_delay_ms goes to *wait_max; stop early once usb.up is 1 if until_up */
static uint32_t wait_max;
static uint32_t run_loop(uint32_t ms, int until_up)
{
    uint32_t t0 = now_us;
    while (now_us - t0 < ms * 1000u) {
        uint32_t w = waited;
        usb_retry(now_us / 1000u);
        if (waited - w > wait_max)
            wait_max = waited - w;
        if (until_up && usb.up)
            break;
        now_us += 16000u;
    }
    return (now_us - t0) / 1000u;
}

int main(void)
{
    uint32_t tries, took, i, at, att;
    now_us = 123000u;                   /* (the logo shown before usb_start) */
    host = 1;                           /* powered from a computer */
    usb_start();
    check("boot with a host: USB up at once, the pads attached", usb.up && attached && !early);
    usb.up = 0, con0 = attached = 0, detached_at = now_us;    /* (the cable out, a new boot) */

    host = 0;                           /* on battery: no cable */
    usb_start();
    check("boot without a host: USB off (the SIE does not answer)", !usb.up && dead > 0 && !early);

    now_us += 930000u;                  /* main.c: 930 ms of logo, then the main loop */
    tries = usb.retries, wait_max = 0;
    run_loop(10000u, 0);
    check("no host: one try a second (10 s: 9..11 tries)", usb.retries - tries >= 9u && usb.retries - tries <= 11u);
    check("no host: still off", !usb.up);
    printf("  longest wait of a main-loop pass: %u ms\n", wait_max);
    check("no host: no main-loop pass waits in fm1_delay_ms (2.7.1)", wait_max == 0);
    check("no host: every attach >= 20 ms after the pads were detached", !early);

    att = attaches;                     /* the worst case: plugged in just after a try */
    while (attaches == att)
        run_loop(16u, 0);
    host = 1;
    took = run_loop(3000u, 1);
    printf("  plugged in: up after %u ms\n", took);
    check("a host plugged in later: USB up within 1.1 s", usb.up && took <= 1100u);
    check("plugged in: attached >= 20 ms after the pads were detached, no wait in a pass", !early && wait_max == 0);

    usb.config = 1;                     /* a session ... */
    host = 0;                           /* ... whose SIE stops answering */
    for (i = 0; i < 40u && usb.up; i++)
        usb_poll();
    check("SIE lost in a session: usb_poll stops (usb.up = 0)", !usb.up);
    host = 1;
    att = attaches;
    took = run_loop(3000u, 1);
    check("SIE lost: started again within 1.1 s, no wait in a pass", usb.up && took <= 1100u && attaches == att + 1u &&
                                                                       !early && wait_max == 0);

    usb.config = 1;                     /* configured again; then the bus goes idle with usb.up = 1: the host was */
    at = attaches;                      /* unplugged (the SIE still answers: usb_poll) or is asleep */
    for (i = 0; i < 400u; i++) {        /* 200 ms of TIMER5 polls, the frame number frozen */
        usb_poll();
        now_us += 500u;
    }
    check("bus idle 200 ms: usb.suspended (the UI hides USB)", usb.suspended);
    run_loop(3000u, 0);
    check("suspended: the retry leaves it attached and polled (a sleeping host finds it)",
          usb.up && attached && attaches == at);

    if (fails) {
        printf("%d FAILED\n", fails);
        return 1;
    }
    puts("usb start / retry: no-host tries, plug-in, SIE lost, suspended bus passed");
    return 0;
}
