// caribou-mini.c — minimal, real-time friendly CaribouLite control app
// Features
//  - Hard/soft reset FPGA
//  - Program FPGA from file (SOURCE)
//  - Monitor key SMI/FPGA/modem registers at ~10 Hz
//  - Toggle RX and TX (TX = continuous 600 Hz complex tone)
//  - TX thread owns activation + streaming with prefill; UI is non-blocking
//  - RT scheduling, CPU affinity, mlockall
//
// Notes
//  - Adjust include paths and link flags to your environment.
//  - Ensure EEC=1 (TX_EN on I.MSB). We set the MSB of I for every symbol during TX.
//  - Replace init/shutdown calls with your actual CaribouLite setup if names differ.
//
// Build (example; adapt libs as needed):
//   gcc -O2 -pipe -Wall -Wextra -pthread caribou-mini.c -o caribou-mini -lncurses -lm -lcariboulite
// You may also need: -lat86rf215 -lcariboufpga -lcaribousmi (depending on how the lib is packaged)
// Run with privileges for RT scheduling, or grant capability:
//   sudo setcap 'cap_sys_nice=eip' ./caribou-mini
//   ./caribou-mini [firmware.bit]

// Enable POSIX/GNU extensions for sigaction/siginfo_t, etc.
#ifndef _POSIX_C_SOURCE
#  define _POSIX_C_SOURCE 200809L
#endif
#ifndef _GNU_SOURCE
#  define _GNU_SOURCE
#endif

#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdatomic.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <pthread.h>
#include <sched.h>
#include <sys/mman.h>
#include <time.h>
#include <unistd.h>
#include <ncurses.h>
#include <signal.h>
#include <errno.h>


// CaribouLite headers (adjust paths/names if needed)
#include "cariboulite.h"
#include "cariboulite_setup.h"
#include <at86rf215.h>

CARIBOULITE_CONFIG_DEFAULT(cariboulite_sys);

// ---------------- Configuration -----------------
#ifndef SOURCE
    #define SOURCE "./top.bin"   // default firmware image path (overridden by argv[1])
#endif

#define APP_SAMPLE_RATE     4000000.0     // Hz (adjust to your configured SMI/DUC rate)
#define TONE_HZ             600.0         // FM test tone baseband frequency (complex sinusoid)
#define TX_PREFILL_CHUNKS   12            // initial prefill chunks
#define TX_CHUNK_MAX_SAMPS  16384         // 16k complex samples per write (~64 KB)
#define RING_SAMPLES_POW2   18            // 2^18 complex samples ~ 131 ms @ 2 MS/s (65 ms @ 4 MS/s)

// ---------------- Types & Globals ----------------
typedef struct {
    sys_st *sys;                         // CaribouLite system
    cariboulite_radio_state_st *radio;   // using S1G (low) by default
    at86rf215_st *modem;
    caribou_fpga_st *fpga;
    caribou_smi_st *smi;
} app_ctx_t;

static atomic_bool g_run        = true;
static atomic_bool g_tx_want    = false;  // UI intent: TX on/off
static atomic_bool g_rx_want    = false;  // UI intent: RX on/off
static atomic_bool g_tx_running = false;
static atomic_bool g_rx_running = false;

// Snapshots updated by monitor thread, read by UI
typedef struct {
    uint8_t iqifc0, iqifc1, iqifc2;
    uint8_t rf09_txdfe, rf24_txdfe;
    uint8_t rf09_rxdfe, rf24_rxdfe;
    uint8_t rf09_state, rf24_state;
    uint8_t rf09_irqs,  rf24_irqs;
    uint8_t rf09_irqm,  rf24_irqm;
    uint8_t rf09_pac,   rf24_pac;
    uint8_t rf09_padfe, rf24_padfe;
    uint8_t smi_info_raw;
    uint8_t smi_rx_fifo_empty;
    uint8_t smi_tx_fifo_full;
    uint8_t smi_channel;
    uint8_t smi_direction;
    uint8_t rffe_debug;
    caribou_fpga_io_ctrl_rfm_en rffe_mode;
    uint8_t driver_state;  // 0=idle 1=RX09 2=RX24 3=TX (per your struct)
} mon_snapshot_t;

static mon_snapshot_t g_mon = {0};

// TX ring buffer
static const size_t RING_SAMPLES = (1u << RING_SAMPLES_POW2);
static cariboulite_sample_complex_int16 *g_tx_ring = NULL;  // [I,Q] as struct {int16_t i; int16_t q;}
static size_t g_tx_idx = 0;                                 // producer index (thread-owned)

// latest samples for UI
static atomic_int g_last_tx_i = 0;
static atomic_int g_last_tx_q = 0;
static atomic_int g_last_rx_i = 0;
static atomic_int g_last_rx_q = 0;

// ---------------- Utilities ----------------
static inline void set_rt_and_affinity(int prio, int cpu)
{
    (void)prio; (void)cpu; // Temporary disable RT unitl stable
//     struct sched_param sp = { .sched_priority = prio };
//     pthread_setschedparam(pthread_self(), SCHED_FIFO, &sp);
// #ifdef __linux__
//     if (cpu >= 0) {
//         cpu_set_t set; CPU_ZERO(&set); CPU_SET(cpu, &set);
//         pthread_setaffinity_np(pthread_self(), sizeof(set), &set);
//     }
// #endif
//     mlockall(MCL_CURRENT | MCL_FUTURE);
}


static void app_sa_handler(int sig, siginfo_t *si, void *uctx)
{
    (void)si; (void)uctx;
    atomic_store(&g_tx_want, false);
    atomic_store(&g_rx_want, false);
    atomic_store(&g_run, false);
    const char msg[] = "caught signal, shutting down...\n";
    write(STDERR_FILENO, msg, sizeof(msg)-1);
}

static int install_app_signal_handlers(void)
{
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_sigaction = app_sa_handler;
    sa.sa_flags = SA_SIGINFO;

    int rc = 0;
    rc |= sigaction(SIGINT,  &sa, NULL);
    rc |= sigaction(SIGTERM, &sa, NULL);
    rc |= sigaction(SIGSEGV, &sa, NULL);
    rc |= sigaction(SIGHUP,  &sa, NULL);
    return rc;
}

static inline int16_t set_tx_en(int16_t i) { return i | 0x8000; } // EEC=1 => TX_EN on I.MSB

static void force_radio_idle(app_ctx_t *ctx)
{
    // best-effort: turn both dirs off on the current radio
    if (ctx && ctx->radio) {
        cariboulite_radio_activate_channel(ctx->radio, cariboulite_channel_dir_tx, false);
        cariboulite_radio_activate_channel(ctx->radio, cariboulite_channel_dir_rx, false);
    }
}

static void fill_tx_ring_tone(double fs_hz)
{
    // Generate a continuous complex sinusoid at TONE_HZ (cos + j*sin),
    // amplitude ~0.5 FS, and set TX_EN bit on I.MSB.
    const double two_pi_f_over_fs = 2.0 * M_PI * TONE_HZ / fs_hz;
    double ph = 0.0;

    for (size_t n = 0; n < RING_SAMPLES; n++) {
        double c = cos(ph), s = sin(ph);
        int16_t I = (int16_t)(c * 4000.0);
        int16_t Q = (int16_t)(s * 4000.0);
        I = set_tx_en(I);
        g_tx_ring[n].i = I; g_tx_ring[n].q = Q;
        ph += two_pi_f_over_fs;
        if (ph >= 2.0 * M_PI) ph -= 2.0 * M_PI;
    }
}

// ---------------- Threads ----------------
static void *monitor_thread(void *arg)
{
    app_ctx_t *ctx = (app_ctx_t *)arg;
    const int period_ms = 100; // 10 Hz

        while (atomic_load(&g_run)) {
        if (!ctx || !ctx->fpga || !ctx->modem || !ctx->smi) {
            usleep(1000);
            continue;
        }

        caribou_fpga_smi_fifo_status_st st = {0};
        int rc = caribou_fpga_get_smi_ctrl_fifo_status(ctx->fpga, &st);
        if (rc == 0) {
            g_mon.smi_info_raw      = *((uint8_t*)&st);
            g_mon.smi_rx_fifo_empty = st.rx_fifo_empty;
            g_mon.smi_tx_fifo_full  = st.tx_fifo_full;
            g_mon.smi_channel       = st.smi_channel;
            g_mon.smi_direction     = st.smi_direction;
        } else {
            // If driver not ready, don’t crash the app — just note it
            g_mon.smi_info_raw = 0xFF;
        }

        uint8_t d[3] = {0};
        at86rf215_read_buffer(ctx->modem, REG_RF_IQIFC0, d, 3);
        g_mon.iqifc0 = d[0]; g_mon.iqifc1 = d[1]; g_mon.iqifc2 = d[2];

        at86rf215_read_buffer(ctx->modem, REG_RF09_TXDFE, d, 1); g_mon.rf09_txdfe = d[0];
        at86rf215_read_buffer(ctx->modem, REG_RF24_TXDFE, d, 1); g_mon.rf24_txdfe = d[0];
        at86rf215_read_buffer(ctx->modem, REG_RF09_RXDFE, d, 1); g_mon.rf09_rxdfe = d[0];
        at86rf215_read_buffer(ctx->modem, REG_RF24_RXDFE, d, 1); g_mon.rf24_rxdfe = d[0];

        at86rf215_read_buffer(ctx->modem, REG_RF09_STATE, d, 1); g_mon.rf09_state = d[0];
        at86rf215_read_buffer(ctx->modem, REG_RF24_STATE, d, 1); g_mon.rf24_state = d[0];
        at86rf215_read_buffer(ctx->modem, REG_RF09_IRQS,  d, 1); g_mon.rf09_irqs  = d[0];
        at86rf215_read_buffer(ctx->modem, REG_RF24_IRQS,  d, 1); g_mon.rf24_irqs  = d[0];
        at86rf215_read_buffer(ctx->modem, REG_RF09_IRQM,  d, 1); g_mon.rf09_irqm  = d[0];
        at86rf215_read_buffer(ctx->modem, REG_RF24_IRQM,  d, 1); g_mon.rf24_irqm  = d[0];
        at86rf215_read_buffer(ctx->modem, REG_RF09_PAC,   d, 1); g_mon.rf09_pac   = d[0];
        at86rf215_read_buffer(ctx->modem, REG_RF24_PAC,   d, 1); g_mon.rf24_pac   = d[0];
        at86rf215_read_buffer(ctx->modem, REG_RF09_PADFE, d, 1); g_mon.rf09_padfe = d[0];
        at86rf215_read_buffer(ctx->modem, REG_RF24_PADFE, d, 1); g_mon.rf24_padfe = d[0];

        uint8_t debug=0; caribou_fpga_io_ctrl_rfm_en mode=0;
        if (caribou_fpga_get_io_ctrl_mode(ctx->fpga, &debug, &mode) == 0) {
            g_mon.rffe_debug = debug; g_mon.rffe_mode = mode;
        }

        // driver state snapshot (guard)
        if (ctx->smi) g_mon.driver_state = (uint8_t)ctx->smi->state;

        struct timespec ts = { .tv_sec = 0, .tv_nsec = 100 * 1000000L };
        nanosleep(&ts, NULL);
    }
    return NULL;
}

static void *rx_thread(void *arg)
{
    app_ctx_t *ctx = (app_ctx_t *)arg;
    set_rt_and_affinity(40, 1);  // moderate RT prio on another core

    size_t batch = caribou_smi_get_native_batch_samples(ctx->smi);
    if (batch == 0) batch = 4096;

    cariboulite_sample_complex_int16 *buf = (cariboulite_sample_complex_int16*)malloc(sizeof(*buf) * batch);
    cariboulite_sample_meta *md = (cariboulite_sample_meta*)malloc(sizeof(*md) * batch);

    bool running = false;

    while (atomic_load(&g_run)) {
        bool want = atomic_load(&g_rx_want);

        if (want && !running) {
            cariboulite_radio_activate_channel(ctx->radio, cariboulite_channel_dir_rx, true);
            running = true;
            atomic_store(&g_rx_running, true);
        }
        if (!want && running) {
            cariboulite_radio_activate_channel(ctx->radio, cariboulite_channel_dir_rx, false);
            running = false;
            atomic_store(&g_rx_running, false);
        }

        if (running) {
            int got = cariboulite_radio_read_samples(ctx->radio, buf, md, batch);
            if (got > 0) {
                cariboulite_sample_complex_int16 s = buf[got>>1];
                atomic_store(&g_last_rx_i, s.i);
                atomic_store(&g_last_rx_q, s.q);
            } else if (got == 0) {
                sched_yield();
            } else {
                usleep(200);
            }
        } else {
            usleep(1000);
        }
    }

    
    if (running) { cariboulite_radio_activate_channel(ctx->radio, cariboulite_channel_dir_rx, false); }
    atomic_store(&g_rx_running, false);
    free(md); free(buf);
    return NULL;
}

static void *tx_thread(void *arg)
{
    app_ctx_t *ctx = (app_ctx_t *)arg;
    set_rt_and_affinity(80, 2);  // high RT prio, third core

    size_t native = caribou_smi_get_native_batch_samples(ctx->smi);
    size_t chunk  = native ? native : 1024;
    if (chunk > 2048) chunk = 2048;
    if (chunk > RING_SAMPLES)       chunk = RING_SAMPLES;

    bool running = false;

    while (atomic_load(&g_run)) {
        bool want = atomic_load(&g_tx_want);

        if (want && !running) {
            // Radio setup (idempotent)
            double f = 430.1e6; cariboulite_radio_set_frequency(ctx->radio, true, &f);
            cariboulite_radio_set_tx_power(ctx->radio, -10);
            cariboulite_radio_set_tx_bandwidth(ctx->radio, cariboulite_radio_tx_cut_off_80khz);

            // Activate and prefill
            if (cariboulite_radio_activate_channel(ctx->radio, cariboulite_channel_dir_tx, true) == 0) {
                running = true;
                atomic_store(&g_tx_running, true);
                for (int i = 0; i < TX_PREFILL_CHUNKS; i++) {
                    int sent = cariboulite_radio_write_samples(ctx->radio, &g_tx_ring[g_tx_idx], chunk);
                    if (sent <= 0) break;
                    g_tx_idx = (g_tx_idx + (size_t)sent) % RING_SAMPLES;
                }
                sched_yield();
            }
        }

        if (!want && running) {
            cariboulite_radio_activate_channel(ctx->radio, cariboulite_channel_dir_tx, false);
            running = false;
            atomic_store(&g_tx_running, false);
        }

        if (running) {
            int sent = cariboulite_radio_write_samples(ctx->radio, &g_tx_ring[g_tx_idx], chunk);
            if (sent > 0) {
                cariboulite_sample_complex_int16 s = g_tx_ring[g_tx_idx];
                atomic_store(&g_last_tx_i, s.i);
                atomic_store(&g_last_tx_q, s.q);
                g_tx_idx = (g_tx_idx + (size_t)sent) % RING_SAMPLES;
            } else if (sent == 0) {
                //sched_yield();
                usleep(500);
            } else {
                atomic_store(&g_tx_want, false); // fatal error, stop
                usleep(1000);
            }
        } else {
            usleep(1000);
        }
    }

    if (running) { cariboulite_radio_activate_channel(ctx->radio, cariboulite_channel_dir_tx, false); }
    atomic_store(&g_tx_running, false);
    return NULL;
}

// ---------------- UI & Main ----------------
static void draw_ui(const app_ctx_t *ctx)
{
    erase();

    time_t now = time(NULL);
    mvprintw(0, 0, "caribou-mini — %s", ctime(&now));

    mvprintw(2, 0, "Keys: [H]ard reset  [S]oft reset  [P]rogram FPGA  [T]X toggle  [R]X toggle  [Q]uit");

    mvprintw(4, 0, "TX want: %s   RX want: %s", atomic_load(&g_tx_want)?"ON":"OFF", atomic_load(&g_rx_want)?"ON":"OFF");

    mvprintw(6, 0, "SMI info: 0x%02X  RX_EMPTY:%d  TX_FULL:%d  CH:%d  DIR:%d  drv_state:%u",
             g_mon.smi_info_raw, g_mon.smi_rx_fifo_empty, g_mon.smi_tx_fifo_full,
             g_mon.smi_channel, g_mon.smi_direction, g_mon.driver_state);

    mvprintw(8, 0, "IQIFC0:0x%02X  IQIFC1:0x%02X  IQIFC2:0x%02X",
             g_mon.iqifc0, g_mon.iqifc1, g_mon.iqifc2);
    mvprintw(9, 0, "RF09 TXDFE:0x%02X RXDFE:0x%02X   RF24 TXDFE:0x%02X RXDFE:0x%02X",
             g_mon.rf09_txdfe, g_mon.rf09_rxdfe, g_mon.rf24_txdfe, g_mon.rf24_rxdfe);
    mvprintw(10, 0, "RF09 STATE:0x%02X IRQM:0x%02X IRQS:0x%02X PAC:0x%02X PADFE:0x%02X",
             g_mon.rf09_state, g_mon.rf09_irqm, g_mon.rf09_irqs, g_mon.rf09_pac, g_mon.rf09_padfe);
    mvprintw(11, 0, "RF24 STATE:0x%02X IRQM:0x%02X IRQS:0x%02X PAC:0x%02X PADFE:0x%02X",
             g_mon.rf24_state, g_mon.rf24_irqm, g_mon.rf24_irqs, g_mon.rf24_pac, g_mon.rf24_padfe);

    mvprintw(13, 0, "RFFE: debug=%u  mode=%s", g_mon.rffe_debug, caribou_fpga_get_mode_name(g_mon.rffe_mode));

    mvprintw(15, 0, "Last TX sample: I=0x%04X Q=0x%04X   Last RX sample: I=0x%04X Q=0x%04X",
             (uint16_t)(atomic_load(&g_last_tx_i) & 0xFFFF), (uint16_t)(atomic_load(&g_last_tx_q) & 0xFFFF),
             (uint16_t)(atomic_load(&g_last_rx_i) & 0xFFFF), (uint16_t)(atomic_load(&g_last_rx_q) & 0xFFFF));

    refresh();
}

static void program_fpga(app_ctx_t *ctx, const char *fw)
{
    // stop streaming during reprogram
    atomic_store(&g_tx_want, false);
    atomic_store(&g_rx_want, false);
    // brief wait for threads to quiesce
    for (int i = 0; i < 50 && (atomic_load(&g_tx_running) || atomic_load(&g_rx_running)); ++i) usleep(1000);
 
    
    // Hard reset first
    caribou_fpga_hard_reset(ctx->fpga);

    // Tell the library we want to (re)program now
    ctx->sys->force_fpga_reprogramming = true;

    // Configure from file path `fw`
    int r = cariboulite_configure_fpga(ctx->sys, cariboulite_firmware_source_file, fw);
    if (r < 0) {
        mvprintw(17, 0, "FPGA programming failed (%d)\n", r);
        return;
    }

    mvprintw(17, 0, "FPGA programmed from '%s'\n", fw);

    // Soft reset + brief settle, then read versions
    caribou_fpga_soft_reset(ctx->fpga);
    io_utils_usleep(100000);
    caribou_fpga_get_versions(ctx->fpga, NULL);
}

int main(int argc, char **argv)
{
    const char *fw_path = (argc > 1) ? argv[1] : SOURCE;
    
    app_ctx_t ctx;
    memset(&ctx, 0, sizeof(ctx));
    
    // ---- Bringup (test-app style, robust) ----
    // Use the same global singleton the test app uses.
    extern sys_st cariboulite_sys;          // declared by CARIBOULITE_CONFIG_DEFAULT
    // Make sure the default config symbol exists in one TU:
    // CARIBOULITE_CONFIG_DEFAULT(cariboulite_sys);  // put this at top of THIS file if not elsewhere

    // Init the driver (not the lightweight lib init)
    cariboulite_sys.force_fpga_reprogramming = 0;
    if (cariboulite_init_driver(&cariboulite_sys, NULL) != 0) {
        fprintf(stderr, "cariboulite_init_driver failed\n");
        return 1;
    }

    // Wire our ctx to the SAME objects the driver mode sets up
    ctx.sys   = &cariboulite_sys;
    ctx.radio = &ctx.sys->radio_low;    // (or radio_high if you prefer HiF)
    ctx.modem = &ctx.sys->modem;
    ctx.fpga  = &ctx.sys->fpga;
    ctx.smi   = &ctx.sys->smi;
    if (!ctx.sys || !ctx.fpga || !ctx.modem || !ctx.smi || !ctx.radio) {
        fprintf(stderr, "internal pointers are NULL after driver init\n");
        cariboulite_release_driver(&cariboulite_sys);
        return 1;
    }

    // Use the driver’s signal handler pattern OR keep yours — your handler is fine.
    if (install_app_signal_handlers() != 0) {
        perror("sigaction");
        cariboulite_release_driver(&cariboulite_sys);
        return 1;
    }
   
    
    // Prepare TX ring and tone
    g_tx_ring = (cariboulite_sample_complex_int16*)malloc(sizeof(*g_tx_ring) * RING_SAMPLES);
    if (!g_tx_ring) { fprintf(stderr, "malloc g_tx_ring failed\n"); return 1; }
    fill_tx_ring_tone(APP_SAMPLE_RATE);

    
    // ---- UI ----
    initscr(); cbreak(); noecho(); nodelay(stdscr, TRUE);

    // One-time show versions
    mvprintw(0, 0, "Board & library versions\n");
    cariboulite_print_board_info(ctx.sys, false);
    caribou_fpga_get_versions(ctx.fpga, NULL);
    cariboulite_lib_version_st v={0}; cariboulite_lib_version(&v);
    mvprintw(1, 0, "Lib ver: %d.%d.%d\n", v.major_version, v.minor_version, v.revision);
    refresh();
    napms(800);
    
    // Threads
    pthread_t th_mon, th_rx, th_tx;
    pthread_create(&th_mon, NULL, monitor_thread, &ctx);
    pthread_create(&th_rx,  NULL, rx_thread,      &ctx);
    pthread_create(&th_tx,  NULL, tx_thread,      &ctx);

    bool need_redraw = true;
    while (atomic_load(&g_run)) {
        int ch = getch();
        if (ch != ERR) {
            switch (ch) {
                case 'q': case 'Q': 
                    atomic_store(&g_tx_want, false);
                    atomic_store(&g_rx_want, false);
                    force_radio_idle(&ctx);
                    atomic_store(&g_run, false); 
                    break;
                case 'h': case 'H':
                    atomic_store(&g_tx_want, false);
                    atomic_store(&g_rx_want, false);
                    for (int i = 0; i < 50 && (atomic_load(&g_tx_running) || atomic_load(&g_rx_running)); ++i) usleep(1000);
                    caribou_fpga_hard_reset(ctx.fpga);
                    need_redraw = true;
                    break;
                case 's': case 'S':
                    atomic_store(&g_tx_want, false);
                    atomic_store(&g_rx_want, false);
                    for (int i = 0; i < 50 && (atomic_load(&g_tx_running) || atomic_load(&g_rx_running)); ++i) usleep(1000);
                    caribou_fpga_soft_reset(ctx.fpga);
                    need_redraw = true;
                    break;
                case 'p': case 'P': program_fpga(&ctx, fw_path); need_redraw = true; break;
                case 't': case 'T': atomic_store(&g_tx_want, !atomic_load(&g_tx_want)); break;
                case 'r': case 'R': atomic_store(&g_rx_want, !atomic_load(&g_rx_want)); break;
                default: break;
            }
        }
        if (need_redraw || ch != ERR) {
            draw_ui(&ctx);
            need_redraw = false;
        }
        napms(50); // UI pacing; TX/RX run independently at RT priority
    }

    // ---- Shutdown ----
    atomic_store(&g_tx_want, false);
    atomic_store(&g_rx_want, false);
    force_radio_idle(&ctx);             
    usleep(10*1000);                    // give HW/driver a moment
    pthread_join(th_tx, NULL);
    pthread_join(th_rx, NULL);
    pthread_join(th_mon, NULL);

    endwin();
    free(g_tx_ring);

    cariboulite_release_driver(&cariboulite_sys);

    return 0;
}
