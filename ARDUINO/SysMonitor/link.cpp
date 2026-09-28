#include "link.h"

#include <stdlib.h>
#include <string.h>

Link link;

namespace {

// The desktop answers immediately and then every polling interval. Four times
// the slowest interval is the point at which the data on screen is certainly
// stale rather than merely late.
const unsigned long kStaleAfterMs = 8000UL;
const unsigned long kDeadAfterMs = 20000UL;
const unsigned long kRequestEveryMs = 2000UL;

// strsep-style split on a single character, in place. Returns the token and
// advances *cursor past it; null once the input is exhausted.
char *nextToken(char **cursor, char sep) {
    if (!*cursor) return nullptr;
    char *start = *cursor;
    char *hit = strchr(start, sep);
    if (hit) {
        *hit = '\0';
        *cursor = hit + 1;
    } else {
        *cursor = nullptr;
    }
    return start;
}

int tokenToInt(char **cursor, char sep) {
    char *t = nextToken(cursor, sep);
    if (!t || !*t) return SYSMON_NA;
    return atoi(t);
}

// For MiB and KiB/s values. atoi() returns a 16-bit int on AVR and silently
// truncates, so "130981" arrives as -91 -- which is what made the memory screen
// read n/a on a 128 GiB machine. Anything that can exceed 32767 must come
// through here.
int32_t tokenToLong(char **cursor, char sep) {
    char *t = nextToken(cursor, sep);
    if (!t || !*t) return SYSMON_NA;
    return atol(t);
}

}  // namespace

void Link::begin() {
    Serial.begin(SYSMON_BAUD);
    m_lastDataMs = millis();
    m_lastRequestMs = 0;
}

void Link::send(const char *payload) {
    const unsigned char sum = sysmonChecksum(payload, (unsigned int)strlen(payload));

    Serial.write(SYSMON_OPEN);
    Serial.write(payload);
    Serial.write(SYSMON_CHECK);
    Serial.write(sysmonHexDigit((unsigned char)(sum >> 4)));
    Serial.write(sysmonHexDigit((unsigned char)(sum & 0x0F)));
    Serial.write(SYSMON_CLOSE);
    Serial.write('\n');
}

void Link::request(uint8_t screen) {
    char payload[8];
    snprintf(payload, sizeof(payload), "Q;D%u", (unsigned)screen);
    send(payload);
    m_lastRequestMs = millis();
}

void Link::requestHello() {
    send("Q;HELLO");
    m_lastRequestMs = millis();
}

// Checks framing and checksum, and hands back the payload span.
bool Link::validate(char *line, uint16_t len, char **payloadOut, uint16_t *payloadLen) {
    if (len < 5) return false;
    if (line[0] != SYSMON_OPEN) return false;

    char *close = (char *)memchr(line, SYSMON_CLOSE, len);
    if (!close) return false;

    char *body = line + 1;
    const uint16_t bodyLen = (uint16_t)(close - body);
    if (bodyLen < 4) return false;

    // "*CC" must be the last three characters of the body.
    char *star = body + bodyLen - 3;
    if (*star != SYSMON_CHECK) return false;

    const int hi = sysmonHexValue(star[1]);
    const int lo = sysmonHexValue(star[2]);
    if (hi < 0 || lo < 0) return false;

    const uint16_t payLen = (uint16_t)(star - body);
    if (sysmonChecksum(body, payLen) != (unsigned char)((hi << 4) | lo)) return false;

    *star = '\0';  // terminate the payload for the tokeniser
    *payloadOut = body;
    *payloadLen = payLen;
    return true;
}

uint8_t Link::dispatch(char *payload, SysData &d) {
    char *cursor = payload;
    char *type = nextToken(&cursor, SYSMON_SEP);
    if (!type || !*type) return 0;

    if (strcmp(type, "HELLO") == 0) {
        nextToken(&cursor, SYSMON_SEP);  // app name, unused
        nextToken(&cursor, SYSMON_SEP);  // protocol version
        const int cores = tokenToInt(&cursor, SYSMON_SEP);
        if (cores > 0) {
            d.announcedCores =
                (uint8_t)(cores > SYSMON_MAX_CORES ? SYSMON_MAX_CORES : cores);
        }
        return 0;  // not a screen dataset
    }

    if (type[0] != 'D') return 0;
    const uint8_t id = (uint8_t)atoi(type + 1);

    switch (id) {
        case SCR_CPU:
            d.cpuUsage = tokenToInt(&cursor, SYSMON_SEP);
            d.cpuFreq = tokenToInt(&cursor, SYSMON_SEP);
            d.cpuTemp = tokenToInt(&cursor, SYSMON_SEP);
            d.procCount = tokenToInt(&cursor, SYSMON_SEP);
            d.uptime = tokenToInt(&cursor, SYSMON_SEP);
            break;

        case SCR_CORES: {
            int n = tokenToInt(&cursor, SYSMON_SEP);
            if (n < 0) n = 0;
            if (n > SYSMON_MAX_CORES) n = SYSMON_MAX_CORES;
            d.coreCount = (uint8_t)n;
            for (int i = 0; i < n; i++) {
                char *group = nextToken(&cursor, SYSMON_SEP);
                if (!group) { d.coreCount = (uint8_t)i; break; }
                char *g = group;
                const int usage = tokenToInt(&g, SYSMON_SUBSEP);
                const int freq = tokenToInt(&g, SYSMON_SUBSEP);
                d.coreUsage[i] = (int8_t)(usage > 100 ? 100 : usage);
                d.coreFreq[i] = (int16_t)freq;
            }
            break;
        }

        case SCR_MEM:
            d.memUsed = tokenToLong(&cursor, SYSMON_SEP);
            d.memTotal = tokenToLong(&cursor, SYSMON_SEP);
            d.memCached = tokenToLong(&cursor, SYSMON_SEP);
            d.commitUsed = tokenToLong(&cursor, SYSMON_SEP);
            d.commitLimit = tokenToLong(&cursor, SYSMON_SEP);
            break;

        case SCR_GPU:
            d.gpuUsage = tokenToInt(&cursor, SYSMON_SEP);
            d.gpuPower = tokenToInt(&cursor, SYSMON_SEP);
            d.gpuClock = tokenToInt(&cursor, SYSMON_SEP);
            d.vramUsed = tokenToLong(&cursor, SYSMON_SEP);
            d.vramTotal = tokenToLong(&cursor, SYSMON_SEP);
            d.fps = tokenToInt(&cursor, SYSMON_SEP);
            d.gpuTemp = tokenToInt(&cursor, SYSMON_SEP);
            break;

        case SCR_POWER:
            d.pwrTotal = tokenToInt(&cursor, SYSMON_SEP);
            d.pwrCpu = tokenToInt(&cursor, SYSMON_SEP);
            d.pwrGpu = tokenToInt(&cursor, SYSMON_SEP);
            d.pwrOther = tokenToInt(&cursor, SYSMON_SEP);
            break;

        case SCR_NET:
            d.netRx = tokenToLong(&cursor, SYSMON_SEP);
            d.netTx = tokenToLong(&cursor, SYSMON_SEP);
            d.netRxTotal = tokenToLong(&cursor, SYSMON_SEP);
            d.netTxTotal = tokenToLong(&cursor, SYSMON_SEP);
            d.netLink = tokenToInt(&cursor, SYSMON_SEP);
            break;

        case SCR_PROC: {
            d.procCpu = tokenToInt(&cursor, SYSMON_SEP);
            int n = tokenToInt(&cursor, SYSMON_SEP);
            if (n < 0) n = 0;
            if (n > SYSMON_PROC_ROWS) n = SYSMON_PROC_ROWS;
            d.procRows = (uint8_t)n;
            for (int i = 0; i < n; i++) {
                char *group = nextToken(&cursor, SYSMON_SEP);
                if (!group) { d.procRows = (uint8_t)i; break; }
                char *g = group;
                char *name = nextToken(&g, SYSMON_SUBSEP);
                const int cpu = tokenToInt(&g, SYSMON_SUBSEP);
                const int32_t mem = tokenToLong(&g, SYSMON_SUBSEP);

                strncpy(d.procs[i].name, name ? name : "?", PROC_NAME_BUF - 1);
                d.procs[i].name[PROC_NAME_BUF - 1] = '\0';
                d.procs[i].cpu = (int8_t)(cpu > 100 ? 100 : cpu);
                d.procs[i].mem = mem;
            }
            break;
        }

        default:
            return 0;
    }

    return id;
}

void Link::logLine(const char *line, uint16_t len) {
    char *slot = m_diag[m_diagHead];
    uint16_t n = len;
    if (n > DIAG_LEN - 1) n = DIAG_LEN - 1;
    memcpy(slot, line, n);
    slot[n] = '\0';

    m_diagHead = (uint8_t)((m_diagHead + 1) % DIAG_LINES);
    if (m_diagCount < DIAG_LINES) m_diagCount++;
}

const char *Link::diagLine(uint8_t index) const {
    if (index >= m_diagCount) return "";
    // 0 is the newest, so walk backwards from the write head.
    const uint8_t slot =
        (uint8_t)((m_diagHead + DIAG_LINES - 1 - index) % DIAG_LINES);
    return m_diag[slot];
}

uint8_t Link::poll(SysData &data) {
    uint8_t got = 0;

    while (Serial.available()) {
        const char c = (char)Serial.read();

        if (c == '\n' || c == '\r') {
            if (m_len > 0 && !m_overflow) {
                m_buf[m_len] = '\0';

                // Log before parsing: validate() writes a terminator over the
                // checksum marker, so afterwards the raw line no longer exists.
                logLine(m_buf, m_len);
                m_lastLen = m_len;

                char *payload = nullptr;
                uint16_t payLen = 0;
                if (validate(m_buf, m_len, &payload, &payLen)) {
                    const uint8_t id = dispatch(payload, data);
                    m_lastDataMs = millis();
                    m_linkUp = true;
                    m_stale = false;
                    m_accepted++;
                    if (m_len > m_tickMaxLen) m_tickMaxLen = m_len;
                    if (m_tickCount < 255) m_tickCount++;
                    if (id) got = id;
                } else {
                    m_rejected++;
                }
            }
            m_len = 0;
            m_overflow = false;
            continue;
        }

        if (m_len < sizeof(m_buf) - 1) {
            m_buf[m_len++] = c;
        } else {
            // Drop the whole line rather than a truncated tail that might still
            // pass as valid framing.
            m_overflow = true;
            m_len = 0;
        }
    }

    return got;
}

void Link::tick(uint8_t currentScreen) {
    const unsigned long now = millis();
    const unsigned long since = now - m_lastDataMs;

    if (since > kDeadAfterMs) {
        m_linkUp = false;
        m_stale = true;
    } else if (since > kStaleAfterMs) {
        m_stale = true;
    }

    // Keep asking while the stream is not arriving. Harmless when the desktop is
    // alive -- it just answers again -- and it is what recovers the link after
    // the agent restarts.
    if ((m_stale || !m_linkUp) && (now - m_lastRequestMs > kRequestEveryMs)) {
        if (!m_linkUp) requestHello();
        request(currentScreen);
    }
}
