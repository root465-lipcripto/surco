#include "cli.h"

#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <poll.h>
#include <signal.h>
#include <termios.h>
#include <time.h>
#include <unistd.h>

static volatile sig_atomic_t interrupted;

static void stop_capture(int signum)
{
    (void)signum;
    interrupted = 1;
}

static speed_t baud_rate(unsigned baud)
{
    switch (baud) {
    case 9600: return B9600;
    case 19200: return B19200;
    case 38400: return B38400;
    case 57600: return B57600;
    case 115200: return B115200;
    default: return 0;
    }
}

static int64_t milliseconds(void)
{
    struct timespec now;
    if (clock_gettime(CLOCK_MONOTONIC, &now) != 0)
        return -1;
    return (int64_t)now.tv_sec * 1000 + now.tv_nsec / 1000000;
}

typedef struct {
    uint32_t next_channel, channels;
    uint64_t pending, last, samples, invalid;
    int32_t values[SURCO_MAX_CHANNELS];
} capture_state;

static bool serial_frame(char *line, uint64_t *timestamp, uint64_t *channel, int32_t *value)
{
    char *first = line;
    while (*first && *first != ',')
        ++first;
    if (!*first)
        return false;
    *first++ = '\0';
    char *second = first;
    while (*second && *second != ',')
        ++second;
    if (!*second)
        return false;
    *second++ = '\0';
    return parse_u64(line, timestamp) && parse_u64(first, channel) && parse_i32(second, value);
}

static surco_status handle_frame(capture_state *state, char *line, surco_encoder *writer)
{
    uint64_t time, channel;
    int32_t value;
    if (!serial_frame(line, &time, &channel, &value) || channel >= state->channels ||
        (state->samples && time < state->last)) {
        state->next_channel = 0;
        ++state->invalid;
        return SURCO_OK;
    }
    if (channel == 0) {
        if (state->next_channel)
            ++state->invalid;
        state->pending = time;
        state->next_channel = 0;
    }
    if (channel != state->next_channel || time != state->pending) {
        state->next_channel = 0;
        ++state->invalid;
        return SURCO_OK;
    }
    state->values[state->next_channel++] = value;
    if (state->next_channel < state->channels)
        return SURCO_OK;
    state->next_channel = 0;
    state->last = time;
    ++state->samples;
    return surco_encoder_push(writer, time, state->values, state->channels);
}

static surco_status receive_frames(int fd, unsigned timeout_ms, uint64_t limit,
    uint32_t channels, surco_encoder *writer)
{
    capture_state state = {.channels = channels};
    int64_t now = milliseconds();
    if (now < 0)
        return SURCO_IO;
    int64_t deadline = now + timeout_ms;
    char line[128];
    size_t used = 0;
    bool discard = false, hung_up = false;
    surco_status status = SURCO_OK;
    while (!interrupted && state.samples < limit) {
        now = milliseconds();
        if (now < 0)
            return SURCO_IO;
        if (now >= deadline)
            break;
        struct pollfd event = {.fd = fd, .events = POLLIN};
        const int ready = poll(&event, 1, (int)(deadline - now));
        if (ready < 0 && errno == EINTR)
            continue;
        if (ready < 0 || (event.revents & POLLNVAL))
            return SURCO_IO;
        if (!ready)
            break;
        if (!(event.revents & POLLIN)) {
            hung_up = (event.revents & POLLHUP) != 0;
            if (hung_up)
                break;
            return SURCO_IO;
        }
        char data[512];
        const ssize_t n = read(fd, data, sizeof data);
        if (n < 0 && (errno == EAGAIN || errno == EINTR))
            continue;
        if (n < 0)
            return SURCO_IO;
        if (n == 0) {
            hung_up = true;
            break;
        }
        for (ssize_t i = 0; i < n && state.samples < limit && !interrupted; ++i) {
            const char ch = data[i];
            if (ch != '\n') {
                if (ch == '\0' || used == sizeof line - 1)
                    discard = true;
                if (!discard)
                    line[used++] = ch;
                continue;
            }
            if (discard) {
                ++state.invalid;
                state.next_channel = 0;
            } else {
                if (used && line[used - 1] == '\r')
                    --used;
                line[used] = '\0';
                const uint64_t previous = state.samples;
                status = handle_frame(&state, line, writer);
                if (status != SURCO_OK)
                    return status;
                if (state.samples != previous) {
                    now = milliseconds();
                    if (now < 0)
                        return SURCO_IO;
                    deadline = now + timeout_ms;
                }
            }
            used = 0;
            discard = false;
        }
    }
    if (used || discard || state.next_channel)
        ++state.invalid;
    const char *reason = interrupted ? "signal" : state.samples == limit ? "sample limit" :
        hung_up ? "device disconnected" : "idle timeout";
    fprintf(stderr, "capture: %" PRIu64 " samples, %" PRIu64 " invalid or incomplete frames (%s)\n",
        state.samples, state.invalid, reason);
    return status;
}

surco_status capture_serial(const char *device, unsigned baud, unsigned timeout_ms,
    uint64_t limit, uint32_t channels, surco_encoder *writer)
{
    const speed_t speed = baud_rate(baud);
    if (!speed)
        return SURCO_ARGUMENT;
    const int fd = open(device, O_RDONLY | O_NOCTTY | O_NONBLOCK | O_CLOEXEC);
    if (fd < 0) {
        perror(device);
        return SURCO_IO;
    }

    struct termios previous;
    surco_status status = SURCO_IO;
    if (tcgetattr(fd, &previous) != 0) {
        perror("capture terminal");
        close(fd);
        return status;
    }
    struct termios raw = previous;
    cfmakeraw(&raw);
    raw.c_cflag &= (tcflag_t)~(CSTOPB | CRTSCTS);
    raw.c_cflag |= CLOCAL | CREAD;
    raw.c_iflag &= (tcflag_t)~(IXOFF | IXANY);
    if (cfsetispeed(&raw, speed) != 0 || cfsetospeed(&raw, speed) != 0 ||
        tcsetattr(fd, TCSANOW, &raw) != 0) {
        perror("configure capture terminal");
        close(fd);
        return status;
    }

    const struct sigaction handler = {.sa_handler = stop_capture};
    struct sigaction old_int, old_term;
    interrupted = 0;
    if (sigaction(SIGINT, &handler, &old_int) == 0) {
        if (sigaction(SIGTERM, &handler, &old_term) == 0) {
            status = receive_frames(fd, timeout_ms, limit, channels, writer);
            sigaction(SIGTERM, &old_term, NULL);
        }
        sigaction(SIGINT, &old_int, NULL);
    }
    if (tcsetattr(fd, TCSANOW, &previous) != 0 &&
        errno != EIO && errno != ENXIO && status == SURCO_OK)
        status = SURCO_IO;
    close(fd);
    return status;
}
