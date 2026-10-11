/* Radio controls only. The backend remains the sole hardware owner.
 * No timer, status subscription, signal to the owner, or command retry is used. */
#include "command_protocol.h"

#include <errno.h>
#include <getopt.h>
#include <inttypes.h>
#include <limits.h>
#include <math.h>
#include <poll.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <time.h>
#include <unistd.h>

typedef struct {
    int socket;
    const char *path;
    double timeout;
    uint64_t next_id;
    control_reply_t state;
    bool state_valid;
} controller_t;

typedef enum { UI_REQUEST, UI_STATUS, UI_HELP, UI_QUIT,
               UI_NOISE, UI_CARRIER, UI_LEVELS } ui_kind_t;

typedef struct {
    ui_kind_t kind;
    control_request_t request;
} ui_command_t;

static volatile sig_atomic_t interrupted;

static void interrupt_handler(int number)
{
    int saved = errno;
    (void)number;
    interrupted = 1;
    errno = saved;
}

static double monotonic_seconds(void)
{
    struct timespec now;
    if (clock_gettime(CLOCK_MONOTONIC, &now)) return 0;
    return (double)now.tv_sec + now.tv_nsec * 1e-9;
}

static void usage(FILE *out, const char *program)
{
    fprintf(out,
        "Usage: %s [--socket PATH] [--timeout SECONDS] [--command 'COMMAND']\n"
        "Radio controller; disconnect releases owned TX and resumes prior RX.\n"
        "  --socket PATH      default /tmp/cariboulite-monitor-<UID>/control.sock\n"
        "  --timeout SECONDS  reply timeout, default 2 (maximum 60)\n"
        "  --command COMMAND  execute once, then disconnect\n"
        "  --help             show this help\n", program);
}

static void command_help(void)
{
    puts("Commands:\n"
         "  status | get          read current settings\n"
         "  rx start | rx stop    start/stop reception\n"
         "  rx frequency MHz      saved RX frequency, for example 430.125\n"
         "  rx mode nbfm|wbfm     WBFM is mono\n"
         "  rx rate 1|2|4         saved RX sample rate in MS/s\n"
         "  squelch noise on|off  noise squelch enable\n"
         "  squelch carrier on|off  carrier squelch enable\n"
         "  squelch levels OPEN CLOSE  noise RMS thresholds: 0 < OPEN < CLOSE <= 8\n"
         "  tx start | tx stop    key/unkey NBFM TX; keep this controller connected\n"
         "  tx frequency MHz      saved TX frequency, for example 430.125\n"
         "  tx power DBM          integer -14..14; backend applies channel limits\n"
         "  tx rate 1|2|4         saved TX sample rate in MS/s\n"
         "  tx source mic|tone    saved TX audio source\n"
         "  tx tone HZ AMP        0 < HZ <= 3000, 0 < AMP <= 1\n"
         "  help                  show commands\n"
         "  q | quit              disconnect; unkey owned TX and resume prior RX\n"
         "Squelch commands also accept the rx prefix. Legacy unprefixed RX commands still work.\n"
         "RX frequency/mode/rate changes require stopped RX.\n"
         "TX settings can be saved during RX; changes require stopped TX.\n"
         "Start/stop acknowledgements can precede completion: use status or the monitor.");
}

static int parse_number(const char *text, double *result)
{
    const char *cursor = text;
    unsigned digits = 0;
    if (*cursor == '+' || *cursor == '-') ++cursor;
    while (*cursor >= '0' && *cursor <= '9') { ++digits; ++cursor; }
    if (*cursor == '.') {
        ++cursor;
        while (*cursor >= '0' && *cursor <= '9') { ++digits; ++cursor; }
    }
    if (!digits) return -1;
    if (*cursor == 'e' || *cursor == 'E') {
        ++cursor;
        if (*cursor == '+' || *cursor == '-') ++cursor;
        unsigned exponent_digits = 0;
        while (*cursor >= '0' && *cursor <= '9') { ++exponent_digits; ++cursor; }
        if (!exponent_digits) return -1;
    }
    if (*cursor) return -1;
    char *end;
    errno = 0;
    double value = strtod(text, &end);
    if (errno || end == text || *end || !isfinite(value)) return -1;
    *result = value;
    return 0;
}

/* Parse the whole input before connecting or sending any request. */
static int parse_command(const char *input, ui_command_t *command)
{
    if (strlen(input) >= 512) return -1;
    char text[512];
    strcpy(text, input);
    char *words[5], *save = NULL;
    size_t count = 0;
    for (char *word = strtok_r(text, " \t\r\n", &save); word;
         word = strtok_r(NULL, " \t\r\n", &save)) {
        if (count == sizeof(words) / sizeof(words[0])) return -1;
        words[count++] = word;
    }
    memset(command, 0, sizeof(*command));
    command->kind = UI_REQUEST;
    if (!count) return 1;
    char **tokens = words;
    const char *name = tokens[0];
    /* Normalize RX and squelch groups to the existing request mapping.
     * Only RX settings can follow rx; a nested tx/help/quit is invalid. */
    if (!strcmp(name, "rx")) {
        if (count < 2) return -1;
        ++tokens;
        --count;
        name = tokens[0];
        if (strcmp(name, "start") && strcmp(name, "stop")
            && strcmp(name, "frequency") && strcmp(name, "freq")
            && strcmp(name, "mode") && strcmp(name, "rate")
            && strcmp(name, "sample-rate") && strcmp(name, "squelch")) return -1;
    }
    if (!strcmp(name, "squelch")) {
        if (count < 2) return -1;
        ++tokens;
        --count;
        name = tokens[0];
        if (strcmp(name, "noise") && strcmp(name, "carrier") && strcmp(name, "levels"))
            return -1;
    }
    if (count == 1 && (!strcmp(name, "q") || !strcmp(name, "quit"))) {
        command->kind = UI_QUIT;
    } else if (count == 1 && (!strcmp(name, "help") || !strcmp(name, "?"))) {
        command->kind = UI_HELP;
    } else if (count == 1 && (!strcmp(name, "get") || !strcmp(name, "status"))) {
        command->kind = UI_STATUS;
        command->request.command = CONTROL_GET;
    } else if (count == 1 && (!strcmp(name, "start") || !strcmp(name, "stop"))) {
        command->request.command = !strcmp(name, "start") ? CONTROL_START : CONTROL_STOP;
    } else if (count == 2 && (!strcmp(name, "frequency") || !strcmp(name, "freq"))) {
        double value;
        if (parse_number(tokens[1], &value) || value <= 0 ||
            !isfinite(value * 1e6)) return -1;
        command->request.command = CONTROL_FREQUENCY;
        command->request.frequency_hz = value * 1e6;
    } else if (count == 2 && !strcmp(name, "mode") &&
               (!strcmp(tokens[1], "nbfm") || !strcmp(tokens[1], "wbfm"))) {
        command->request.command = CONTROL_MODE;
        command->request.wbfm = !strcmp(tokens[1], "wbfm");
    } else if (count == 2 && (!strcmp(name, "rate") || !strcmp(name, "sample-rate"))) {
        double rate;
        if (parse_number(tokens[1], &rate) || (rate != 1 && rate != 2 && rate != 4))
            return -1;
        command->request.command = CONTROL_SAMPLE_RATE;
        command->request.sample_rate_hz = (unsigned)rate * 1000000;
    } else if (count == 2 && (!strcmp(name, "noise") || !strcmp(name, "carrier")) &&
               (!strcmp(tokens[1], "on") || !strcmp(tokens[1], "off"))) {
        command->kind = !strcmp(name, "noise") ? UI_NOISE : UI_CARRIER;
        command->request.noise_enabled = !strcmp(tokens[1], "on");
        command->request.carrier_enabled = !strcmp(tokens[1], "on");
    } else if (count == 3 && !strcmp(name, "levels")) {
        double opening, closing;
        if (parse_number(tokens[1], &opening) || parse_number(tokens[2], &closing) ||
            opening <= 0 || closing > 8 || opening >= closing) return -1;
        command->kind = UI_LEVELS;
        command->request.noise_open_rms = (float)opening;
        command->request.noise_close_rms = (float)closing;
    } else if (count == 2 && !strcmp(name, "tx") &&
               (!strcmp(tokens[1], "start") || !strcmp(tokens[1], "stop"))) {
        command->request.command = !strcmp(tokens[1], "start") ? CONTROL_TX_START : CONTROL_TX_STOP;
    } else if (count == 3 && !strcmp(name, "tx") &&
               (!strcmp(tokens[1], "frequency") || !strcmp(tokens[1], "freq"))) {
        double value;
        if (parse_number(tokens[2], &value) || value <= 0 || !isfinite(value * 1e6)) return -1;
        command->request.command = CONTROL_TX_FREQUENCY;
        command->request.tx_frequency_hz = value * 1e6;
    } else if (count == 3 && !strcmp(name, "tx") && !strcmp(tokens[1], "power")) {
        const char *digit = tokens[2];
        if (*digit == '+' || *digit == '-') ++digit;
        if (!*digit) return -1;
        for (; *digit; ++digit) if (*digit < '0' || *digit > '9') return -1;
        double power;
        if (parse_number(tokens[2], &power) || power < -14 || power > 14) return -1;
        command->request.command = CONTROL_TX_POWER;
        command->request.tx_power_dbm = (int)power;
    } else if (count == 3 && !strcmp(name, "tx") &&
               (!strcmp(tokens[1], "rate") || !strcmp(tokens[1], "sample-rate"))) {
        double rate;
        if (parse_number(tokens[2], &rate) || (rate != 1 && rate != 2 && rate != 4)) return -1;
        command->request.command = CONTROL_TX_SAMPLE_RATE;
        command->request.tx_sample_rate_hz = (unsigned)rate * 1000000;
    } else if (count == 3 && !strcmp(name, "tx") && !strcmp(tokens[1], "source") &&
               (!strcmp(tokens[2], "mic") || !strcmp(tokens[2], "tone"))) {
        command->request.command = CONTROL_TX_SOURCE;
        command->request.tx_tone_mode = !strcmp(tokens[2], "tone");
    } else if (count == 4 && !strcmp(name, "tx") && !strcmp(tokens[1], "tone")) {
        double hz, amplitude;
        if (parse_number(tokens[2], &hz) || parse_number(tokens[3], &amplitude) ||
            hz <= 0 || hz > 3000 || amplitude <= 0 || amplitude > 1) return -1;
        command->request.command = CONTROL_TX_TONE;
        command->request.tx_tone_hz = (float)hz;
        command->request.tx_tone_amp = (float)amplitude;
        if (command->request.tx_tone_hz <= 0 || command->request.tx_tone_amp <= 0) return -1;
    } else return -1;
    return 0;
}

static void disconnect_controller(controller_t *controller)
{
    if (controller->socket >= 0) close(controller->socket);
    controller->socket = -1;
    controller->state_valid = false;
}

static int wait_ready(int descriptor, short events, double deadline)
{
    while (!interrupted) {
        double remaining = deadline - monotonic_seconds();
        if (remaining <= 0) { errno = ETIMEDOUT; return -1; }
        int milliseconds = (int)ceil(remaining * 1000);
        struct pollfd waiter = { .fd = descriptor, .events = events };
        int result = poll(&waiter, 1, milliseconds);
        if (result < 0 && errno == EINTR) continue;
        if (result <= 0) {
            if (!result) errno = ETIMEDOUT;
            return -1;
        }
        /* A final packet can arrive together with POLLHUP. Read it first. */
        if (waiter.revents & events) return 0;
        errno = ECONNRESET;
        return -1;
    }
    errno = EINTR;
    return -1;
}

static int connect_controller(controller_t *controller)
{
    struct sockaddr_un address = { .sun_family = AF_UNIX };
    if (!controller->path[0] || strlen(controller->path) >= sizeof(address.sun_path)) {
        errno = ENAMETOOLONG;
        return -1;
    }
    strcpy(address.sun_path, controller->path);
    int descriptor = socket(AF_UNIX, SOCK_SEQPACKET | SOCK_CLOEXEC | SOCK_NONBLOCK, 0);
    if (descriptor < 0) return -1;
    if (connect(descriptor, (struct sockaddr *)&address, sizeof(address))) {
        if (errno != EINPROGRESS && errno != EAGAIN) goto failed;
        if (wait_ready(descriptor, POLLOUT, monotonic_seconds() + controller->timeout))
            goto failed;
        int error = 0;
        socklen_t bytes = sizeof(error);
        if (getsockopt(descriptor, SOL_SOCKET, SO_ERROR, &error, &bytes)) goto failed;
        if (error) { errno = error; goto failed; }
    }
    controller->socket = descriptor;
    return 0;
failed:
    {
        int saved = errno;
        close(descriptor);
        errno = saved;
        return -1;
    }
}

static bool busy_admission(controller_t *controller, const control_reply_t *reply)
{
    if (reply->id || reply->ok || strcmp(reply->code, "BUSY")) return false;
    fprintf(stderr, "Another controller is already connected; the backend is busy. Disconnect that controller before reconnecting.\n");
    disconnect_controller(controller);
    return true;
}

/* 0 success, 1 backend rejection, -1 disconnected/unknown. A request is sent
 * at most once; timeout, malformed reply and mismatched IDs close the socket. */
static int exchange(controller_t *controller, control_request_t *request)
{
    char packet[CONTROL_PROTOCOL_MAX_PACKET_BYTES];
    if (controller->next_id == UINT64_MAX) {
        fprintf(stderr, "Request ID limit reached; reconnect using a new controller.\n");
        disconnect_controller(controller);
        return -1;
    }
    request->id = ++controller->next_id;
    request->protocol_version = 2;
    int length = control_request_format(request, packet, sizeof(packet));
    if (length < 0) {
        fprintf(stderr, "Invalid command values; no request was sent.\n");
        return 1;
    }
    double deadline = monotonic_seconds() + controller->timeout;
    bool sent = false;
    while (!interrupted) {
        ssize_t bytes = send(controller->socket, packet, (size_t)length, MSG_NOSIGNAL);
        if (bytes == length) { sent = true; break; }
        if (bytes < 0 && errno == EINTR) continue;
        if (bytes < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
            if (!wait_ready(controller->socket, POLLOUT, deadline)) continue;
        } else if (bytes >= 0) errno = EPROTO;
        /* The server can queue BUSY and close immediately after accepting.
         * In that race send() fails before we reach the ordinary reply read. */
        int saved = errno;
        ssize_t received = recv(controller->socket, packet, sizeof(packet),
                                MSG_DONTWAIT | MSG_TRUNC);
        control_reply_t admission;
        if (received > 0 && (size_t)received <= sizeof(packet) &&
            !control_reply_parse(packet, (size_t)received, &admission) &&
            busy_admission(controller, &admission)) return 1;
        errno = saved;
        goto failure;
    }
    if (!sent) { errno = EINTR; goto failure; }
    if (wait_ready(controller->socket, POLLIN, deadline)) goto failure;
    ssize_t bytes = recv(controller->socket, packet, sizeof(packet), MSG_TRUNC);
    if (bytes <= 0) {
        if (!bytes) errno = ECONNRESET;
        goto failure;
    }
    control_reply_t reply;
    if ((size_t)bytes > sizeof(packet) || control_reply_parse(packet, (size_t)bytes, &reply)) {
        errno = EPROTO;
        goto failure;
    }
    /* ID zero belongs only to an unsolicited admission rejection. It cannot
     * acknowledge any request or supply a usable controller session. */
    if (busy_admission(controller, &reply)) return 1;
    if (reply.id != request->id) { errno = EPROTO; goto failure; }
    controller->state = reply;
    controller->state_valid = true;
    if (!reply.ok) {
        fprintf(stderr, "Command rejected: %s. Current settings follow.\n", reply.code);
        return 1;
    }
    if (!strcmp(reply.code, "ACCEPTED"))
        puts("Transition accepted; use status or the passive monitor to observe completion.");
    return 0;
failure:
    fprintf(stderr, "%s: %s.\n", sent ? "Reply unavailable" : "Request not sent", strerror(errno));
    if (sent && request->command != CONTROL_GET)
        fprintf(stderr, "Command outcome is uncertain; it was not retried. Reconnect and use status before deciding what to do.\n");
    disconnect_controller(controller);
    return -1;
}

static void print_state(const controller_t *controller)
{
    if (!controller->state_valid) return;
    const control_reply_t *state = &controller->state;
    char actual[64];
    if (state->actual_frequency_hz > 0)
        snprintf(actual, sizeof(actual), "%.6f MHz", state->actual_frequency_hz / 1e6);
    else
        strcpy(actual, "unavailable");
    printf("%s %s %s | requested %.6f MHz, actual %s | %s %.0f MS/s\n"
           "  squelch: noise %s, carrier %s, open %.3f, close %.3f | config=%" PRIu64
           " pipeline=%" PRIu64 "\n",
           state->simulated ? "SIMULATED" : "LIVE", state->channel, state->state,
           state->requested_frequency_hz / 1e6, actual,
           state->wbfm ? "WBFM mono" : "NBFM", state->sample_rate_hz / 1e6,
           state->noise_enabled ? "on" : "off", state->carrier_enabled ? "on" : "off",
           (double)state->noise_open_rms, (double)state->noise_close_rms,
           state->config_generation, state->pipeline_generation);
    if (state->protocol_version == 2) {
        char tx_actual[64];
        char tx_power[40];
        if (state->tx_actual_frequency_hz > 0)
            snprintf(tx_actual, sizeof(tx_actual), "%.6f MHz", state->tx_actual_frequency_hz / 1e6);
        else strcpy(tx_actual, "unavailable");
        if (state->tx_actual_power_dbm != INT_MIN)
            snprintf(tx_power, sizeof(tx_power), "%d dBm", state->tx_actual_power_dbm);
        else strcpy(tx_power, "unavailable");
        printf("  TX: requested %.6f MHz, actual %s | %.0f MS/s | power requested %d dBm, estimate %s\n"
               "      source %s, tone %.1f Hz amplitude %.3f, deviation +/-%.0f Hz | pipeline=%" PRIu64 "\n"
               "      requested=%s, resume RX=%s, transition=%s\n",
               state->tx_requested_frequency_hz / 1e6, tx_actual, state->tx_sample_rate_hz / 1e6,
               state->tx_requested_power_dbm, tx_power,
               state->tx_tone_mode ? "tone" : "mic", (double)state->tx_tone_hz,
               (double)state->tx_tone_amp, (double)state->tx_deviation_hz,
               state->tx_pipeline_generation, state->tx_requested ? "yes" : "no",
               state->rx_resume ? "yes" : "no", state->transition ? "yes" : "no");
    }
    fflush(stdout);
}

static int refresh_state(controller_t *controller)
{
    if (controller->socket < 0 && connect_controller(controller)) {
        fprintf(stderr, "Cannot connect to %s: %s.\n", controller->path, strerror(errno));
        return -1;
    }
    control_request_t request = { .command = CONTROL_GET };
    return exchange(controller, &request);
}

static int execute_command(controller_t *controller, ui_command_t *command)
{
    if (command->kind == UI_HELP) { command_help(); return 0; }
    if (command->kind == UI_QUIT) return 0;
    if (command->kind == UI_STATUS) {
        int result = refresh_state(controller);
        print_state(controller);
        return result;
    }
    bool squelch = command->kind == UI_NOISE || command->kind == UI_CARRIER ||
                   command->kind == UI_LEVELS;
    /* Refresh on every partial squelch edit so another controller's latest
     * settings are preserved as far as this request/response protocol allows. */
    if (controller->socket < 0 || !controller->state_valid || squelch) {
        int result = refresh_state(controller);
        if (result) return result;
    }
    if (squelch) {
        control_request_t requested = command->request;
        command->request = (control_request_t){
            .command = CONTROL_SQUELCH,
            .noise_enabled = controller->state.noise_enabled,
            .carrier_enabled = controller->state.carrier_enabled,
            .noise_open_rms = controller->state.noise_open_rms,
            .noise_close_rms = controller->state.noise_close_rms,
        };
        if (command->kind == UI_NOISE)
            command->request.noise_enabled = requested.noise_enabled;
        else if (command->kind == UI_CARRIER)
            command->request.carrier_enabled = requested.carrier_enabled;
        else {
            command->request.noise_open_rms = requested.noise_open_rms;
            command->request.noise_close_rms = requested.noise_close_rms;
        }
    }
    int result = exchange(controller, &command->request);
    print_state(controller);
    return result;
}

int main(int argc, char **argv)
{
    char default_path[sizeof(((struct sockaddr_un *)0)->sun_path)];
    snprintf(default_path, sizeof(default_path), "/tmp/cariboulite-monitor-%lu/control.sock",
             (unsigned long)geteuid());
    controller_t controller = { .socket = -1, .path = default_path, .timeout = 2 };
    const char *once = NULL;
    static const struct option options[] = {
        {"socket", required_argument, NULL, 's'},
        {"timeout", required_argument, NULL, 't'},
        {"command", required_argument, NULL, 'c'},
        {"help", no_argument, NULL, 'h'},
        {NULL, 0, NULL, 0},
    };
    int option;
    while ((option = getopt_long(argc, argv, "", options, NULL)) != -1) {
        if (option == 's') controller.path = optarg;
        else if (option == 't') {
            if (parse_number(optarg, &controller.timeout) || controller.timeout <= 0 ||
                controller.timeout > 60) {
                fprintf(stderr, "Timeout must be a finite value greater than 0 and at most 60 seconds.\n");
                return 2;
            }
        } else if (option == 'c') once = optarg;
        else if (option == 'h') { usage(stdout, argv[0]); command_help(); return 0; }
        else { usage(stderr, argv[0]); return 2; }
    }
    if (optind != argc) { usage(stderr, argv[0]); return 2; }
    ui_command_t initial_command;
    if (once) {
        int result = parse_command(once, &initial_command);
        if (result) { fprintf(stderr, "Invalid command. Use --help for supported commands.\n"); return 2; }
        if (initial_command.kind == UI_HELP) { command_help(); return 0; }
        if (initial_command.kind == UI_QUIT) return 0;
        if (initial_command.request.command == CONTROL_TX_START) {
            fprintf(stderr, "TX start requires an interactive controller session; --command would disconnect and immediately unkey TX.\n");
            return 2;
        }
    }
    struct sigaction action = { .sa_handler = interrupt_handler };
    sigemptyset(&action.sa_mask);
    if (sigaction(SIGINT, &action, NULL) || sigaction(SIGTERM, &action, NULL)) {
        perror("Cannot install signal handlers");
        return 1;
    }
    setvbuf(stdin, NULL, _IONBF, 0);
    bool terminal = isatty(STDIN_FILENO);
    if (!once) {
        puts("CaribouLite radio controller. Type help for commands; q disconnects and unkeys any owned TX.");
    }
    int startup = refresh_state(&controller);
    if (once) {
        int result = startup;
        if (!startup) {
            if (initial_command.kind == UI_STATUS) print_state(&controller);
            else result = execute_command(&controller, &initial_command);
        }
        disconnect_controller(&controller);
        return result ? 1 : 0;
    }
    print_state(&controller);
    if (terminal) { fputs("radio> ", stdout); fflush(stdout); }
    while (!interrupted) {
        struct pollfd descriptors[] = {
            { .fd = STDIN_FILENO, .events = POLLIN },
            { .fd = controller.socket, .events = POLLIN },
        };
        int result = poll(descriptors, 2, -1);
        if (result < 0 && errno == EINTR) continue;
        if (result < 0) { perror("Input wait failed"); break; }
        if (controller.socket >= 0 && descriptors[1].revents) {
            fprintf(stderr, "Backend connection closed or sent an unexpected packet. Enter status to reconnect.\n");
            disconnect_controller(&controller);
        }
        if (!(descriptors[0].revents & (POLLIN | POLLHUP))) continue;
        char line[512];
        if (!fgets(line, sizeof(line), stdin)) break;
        if (!strchr(line, '\n') && !feof(stdin)) {
            int character;
            while ((character = fgetc(stdin)) != '\n' && character != EOF && !interrupted) {}
            fprintf(stderr, "Command too long; no request was sent.\n");
        } else {
            ui_command_t command;
            int parsed = parse_command(line, &command);
            if (parsed < 0) fprintf(stderr, "Invalid command. Type help for supported commands.\n");
            else if (!parsed) {
                if (command.kind == UI_QUIT) break;
                execute_command(&controller, &command);
            }
        }
        if (terminal) { fputs("radio> ", stdout); fflush(stdout); }
    }
    disconnect_controller(&controller);
    if (terminal) puts("\nController disconnected; owned TX is released and prior RX resumes.");
    return 0;
}
