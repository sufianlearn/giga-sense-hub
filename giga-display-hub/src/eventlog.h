#ifndef EVENTLOG_H
#define EVENTLOG_H

// ---------------------------------------------------------------------------
//  eventlog.h  –  Circular event log buffer for GigaSenseHub
// ---------------------------------------------------------------------------
//
//  Stores the last MAX_EVENTS events in a ring buffer.  Each event has:
//    - timestamp (millis() at creation, displayed as uptime)
//    - severity  (INFO, WARN, ALERT)
//    - message   (short text, up to 63 chars)
//
//  The log lives in RAM only — it resets on power cycle.  This is intentional:
//  flash wear from logging every event would kill the STM32H7 flash in weeks.
//
// ---------------------------------------------------------------------------

#include <cstdint>
#include <cstring>
#include <cstdio>

#define MAX_EVENTS      64
#define EVENT_MSG_LEN   64

enum EventSeverity : uint8_t {
    EVT_INFO  = 0,   // Normal operations (login, settings change)
    EVT_WARN  = 1,   // Attention needed (vibration, wifi drop)
    EVT_ALERT = 2    // Immediate concern (motion alert, intrusion)
};

struct EventEntry {
    unsigned long timestamp;       // millis() when event was logged
    EventSeverity severity;
    char          message[EVENT_MSG_LEN];
};

// ---------------------------------------------------------------------------
//  Ring buffer
// ---------------------------------------------------------------------------
namespace _eventlog {
    static EventEntry evtBuf[MAX_EVENTS];
    static int head  = 0;   // next write position
    static int count = 0;   // total events stored (max MAX_EVENTS)
}

// Add a new event to the log.
inline void eventLogAdd(EventSeverity sev, const char *msg) {
    using namespace _eventlog;
    EventEntry &e = evtBuf[head];
    e.timestamp = millis();
    e.severity  = sev;
    strncpy(e.message, msg, EVENT_MSG_LEN - 1);
    e.message[EVENT_MSG_LEN - 1] = '\0';
    head = (head + 1) % MAX_EVENTS;
    if (count < MAX_EVENTS) count++;
}

// Convenience wrappers
inline void eventInfo(const char *msg)  { eventLogAdd(EVT_INFO, msg); }
inline void eventWarn(const char *msg)  { eventLogAdd(EVT_WARN, msg); }
inline void eventAlert(const char *msg) { eventLogAdd(EVT_ALERT, msg); }

// Get total number of stored events.
inline int eventLogCount() {
    return _eventlog::count;
}

// Get event by index (0 = oldest).  Returns nullptr if out of range.
inline const EventEntry *eventLogGet(int idx) {
    using namespace _eventlog;
    if (idx < 0 || idx >= count) return nullptr;
    // oldest event is at (head - count + MAX_EVENTS) % MAX_EVENTS
    int pos = (head - count + idx + MAX_EVENTS) % MAX_EVENTS;
    return &evtBuf[pos];
}

// Get event by index from newest (0 = newest, 1 = second newest, ...).
inline const EventEntry *eventLogGetNewest(int idx) {
    using namespace _eventlog;
    if (idx < 0 || idx >= count) return nullptr;
    int pos = (head - 1 - idx + MAX_EVENTS) % MAX_EVENTS;
    return &evtBuf[pos];
}

// Format timestamp as "HHh MMm SSs"
inline void eventFormatTime(unsigned long ms, char *buf, int bufLen) {
    unsigned long secs = ms / 1000;
    unsigned long h = secs / 3600;
    unsigned long m = (secs % 3600) / 60;
    unsigned long s = secs % 60;
    if (h > 0)
        snprintf(buf, bufLen, "%luh%02lum%02lus", h, m, s);
    else if (m > 0)
        snprintf(buf, bufLen, "%lum%02lus", m, s);
    else
        snprintf(buf, bufLen, "%lus", s);
}

// Get severity icon string for LVGL labels
inline const char *eventSeverityIcon(EventSeverity sev) {
    switch (sev) {
        case EVT_INFO:  return LV_SYMBOL_OK;
        case EVT_WARN:  return LV_SYMBOL_WARNING;
        case EVT_ALERT: return LV_SYMBOL_BELL;
        default:        return "?";
    }
}

#endif // EVENTLOG_H
