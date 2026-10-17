/* Physical baseline runner. Kept beside menu-owned pipelines until extraction.
 * This exercises their DSP/streaming paths, not scanf/ncurses interaction.
 */
int app_baseline_test(sys_st* sys, const char* firmware, const char* capture,
                      const char* playback, unsigned seconds,
                      volatile sig_atomic_t* running)
{
    int result = -1;
    tx_pipeline_t tx = {0};
    rx_pipeline_t rx = {0};
    const unsigned menus[] = {11, 12, 14, 14};
    const bool transmitting[] = {true, false, true, false};
    cariboulite_radio_state_st* radio = &sys->radio_high;
    printf("BASELINE,event,menu,direction,rate,elapsed_s,rssi_dbm,status\n");
    fflush(stdout);
    if (caribou_fpga_hard_reset(&sys->fpga) != 0) goto done;
    printf("BASELINE,reset,0,none,0,0,,ok\n");
    sys->force_fpga_reprogramming = true;
    if (cariboulite_configure_fpga(sys, cariboulite_firmware_source_file, firmware) < 0 ||
        caribou_fpga_soft_reset(&sys->fpga) != 0) goto done;
    io_utils_usleep(100000);
    caribou_fpga_get_versions(&sys->fpga, NULL);
    printf("BASELINE,firmware,3,none,0,0,,ok\n");
    fflush(stdout);

    for (size_t session = 0; session < 4 && *running; ++session) {
        for (unsigned rate = 2000000; rate <= 4000000 && *running; rate *= 2) {
            const bool is_tx = transmitting[session];
            const char* direction = is_tx ? "tx" : "rx";
            const unsigned menu = menus[session];
            printf("BASELINE,prepare,%u,%s,%u,0,,HiF\n", menu, direction, rate);
            fflush(stdout);
            // No workers remain while resetting shared FIFO state.
            if (caribou_fpga_soft_reset(&sys->fpga) != 0) goto done;
            tx_params_t tp = {
                .freq_hz = 430100000.0, .tx_power_dbm = -3, .rf_fs = rate,
                .tone_mode = menu == 11, .tone_hz = 600.0f, .tone_amp = 0.4f,
                .mic_dev = menu == 11 ? NULL : capture,
                .out_scale = 4000.0f, .f_dev_hz = NBFM_DEFAULT_DEVIATION_HZ,
            };
            rx_params_t rp = {
                .freq_hz = 430100000.0, .pcm_dev = playback,
                .deemph_tau_s = 50e-6f, .pcm_gain = 8000.0f,
                .fs_rf = rate, .fs_audio = 48000.0f,
            };
            if (menu == 14) {
                if (!monitor_init_pipelines(&tx, &rx, sys, radio, &tp, &rp)) goto done;
                if ((is_tx ? tx_pipeline_start(&tx) : rx_pipeline_start(&rx)) != 0) goto done;
            } else if (is_tx) {
                if (tx_pipeline_init(&tx, sys, radio, &tp) != 0 ||
                    tx_pipeline_start(&tx) != 0) goto done;
            } else {
                if (rx_pipeline_init(&rx, sys, radio, &rp) != 0 ||
                    rx_pipeline_start(&rx) != 0) goto done;
            }
            printf("BASELINE,start,%u,%s,%u,0,,ok\n", menu, direction, rate);
            fflush(stdout);
            uint64_t started = mono_ns();
            size_t previous_gets = 0;
            for (unsigned tick = 0; tick < seconds && *running; ++tick) {
                // Check cancellation at 100 ms intervals.
                for (int i = 0; i < 10 && *running; ++i) usleep(100000);
                if (!*running) goto done;
                double elapsed = (mono_ns() - started) / 1e9;
                rf10_stats_t stats;
                if (is_tx) {
                    tx_pipeline_stats_t snapshot;
                    tx_pipeline_get_stats(&tx, &snapshot);
                    stats = snapshot.txq;
                } else {
                    rx_pipeline_stats_t snapshot;
                    rx_pipeline_get_stats(&rx, &snapshot);
                    stats = snapshot.rxq;
                }
                if (stats.gets == previous_gets ||
                    (is_tx && !nbfm_tx_active) || (!is_tx && !nbfm_rx_active)) {
                    printf("BASELINE,stalled,%u,%s,%u,%.3f,,error\n", menu, direction, rate, elapsed);
                    goto done;
                }
                previous_gets = stats.gets;
                if (!is_tx) {
                    float rssi = 0;
                    HW_LOCK();
                    int rc = cariboulite_radio_get_rssi(radio, &rssi);
                    HW_UNLOCK();
                    if (rc != 0 || !isfinite(rssi)) goto done;
                    printf("BASELINE,rssi,%u,%s,%u,%.3f,%.2f,ok\n", menu, direction, rate, elapsed, rssi);
                } else {
                    printf("BASELINE,progress,%u,%s,%u,%.3f,,ok\n", menu, direction, rate, elapsed);
                }
                fflush(stdout);
            }
            // Normal TX stop includes the existing 2475 Hz Quindar tail.
            tx_pipeline_destroy(&tx);
            rx_pipeline_destroy(&rx);
            printf("BASELINE,stop,%u,%s,%u,0,,ok\n", menu, direction, rate);
            fflush(stdout);
            for (int i = 0; i < 20 && *running; ++i) usleep(100000);
        }
    }
    if (*running) result = 0;
done:
    tx_pipeline_destroy(&tx);
    rx_pipeline_destroy(&rx);
    printf("BASELINE,complete,0,none,0,0,,%s\n", result == 0 ? "awaiting-listener" : "failed");
    fflush(stdout);
    return result;
}
