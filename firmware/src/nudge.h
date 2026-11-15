// Nudges: send and receive short messages through the club's nudge hub (a
// Cloudflare Worker, see worker/). A background task checks the inbox once a
// minute, fetches the list of friends, and sends what the UI queues. It takes
// datasync's TLS lock, so it never holds a second HTTPS connection at once.
//
// Signals go through the same hub: quotes broadcast to the whole club, each
// gone 24 hours after it was sent. The task checks them every 10 minutes.
//
// Needs the hub URL (from the club file's "nudges.url") and this unit's token
// (from the setup page). Without both it stays idle.
#pragma once

#include <stdint.h>

#include "bc_nudge.h"
#include "bc_signal.h"

namespace nudge {

void begin();  // after datasync::begin()
// `hubUrl` comes from the club file; "" when it has none.
void tick(uint32_t nowMs, bool wifiUp, const char* hubUrl);

bool configured();  // a hub URL and a token are both set

// Friends this unit can nudge, as the hub knows them (refreshed hourly).
int friendCount();
const char* friendName(int i);

// Queue a nudge. `to` is a friend's name or "*" for everyone.
void send(const char* to, const char* msg);

// Broadcast a signal (a quote) to everyone, replacing this unit's last one.
// It shares the send state below with nudges.
void broadcast(const char* msg);

enum class SendState : uint8_t { Idle, Sending, Sent, Failed };
SendState sendState();
bool sendWasSignal();     // the last send was a signal, not a nudge
const char* sendTo();     // who the last send was for ("*" = everyone)
const char* sendError();  // why it failed
void clearSendState();

// Nudges received, oldest first, one at a time.
bool takeIncoming(bc::Nudge& out);

const char* lastError();  // the last inbox or roster problem, for ABOUT

// This unit's name as the hub knows it ("" until the roster arrives).
const char* me();

// Live signals, newest first, as of the last check. `left` is counted down
// from that check, and a signal past its 24 hours is dropped even offline.
// Returns how many were copied into `out`.
int signals(bc::Signal* out, int cap, uint32_t nowMs);
// Goes up by one after each successful check, so callers can spot new ones.
uint32_t signalsVersion();
const char* signalsError();  // why the last check failed ("" if it didn't)

}  // namespace nudge
