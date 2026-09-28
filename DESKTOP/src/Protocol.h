// ---------------------------------------------------------------------------
//  Protocol.h -- desktop side of the wire format described in PROTOCOL.md.
//
//  (The shared contract is sysmon_wire.h, not "protocol.h": on a case-insensitive
//  filesystem that name collides with this header.)
// ---------------------------------------------------------------------------
#pragma once

#include <optional>
#include <string>

#include "SensorData.h"
#include "sysmon_wire.h"

// What the display asked for. Anything unparsable is simply not returned, so a
// corrupt line can never be mistaken for a screen change.
struct Request {
    enum class Kind { Hello, Screen };
    Kind kind = Kind::Hello;
    int screen = SCR_CPU;
};

// Parses one received line, braces included. Returns nothing if the framing or
// the checksum is wrong.
std::optional<Request> parseRequest(const std::string &line);

// Builds the reply line for a screen, checksum and '\n' included.
std::string encodeScreen(int screen, const SensorSnapshot &snap);

// Builds the HELLO reply.
std::string encodeHello(int coreCount);

// Wraps a payload in braces with its checksum and a newline. Exposed for tests.
std::string frame(const std::string &payload);
