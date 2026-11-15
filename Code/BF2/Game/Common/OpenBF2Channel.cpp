// [CHOSEN, not recovered] The OpenBF2 extension channel (mod/combo: handshake and kill feed messages
// only). See OpenBF2Channel.h.

#include "OpenBF2Channel.h"

#include "GameEvent.h"
#include "../IGame.h"
#include "../GameServer/KillfeedPolicy.h"              // [CHOSEN, not recovered] openbf2.sv.killfeed.*
#include "../../Core/EventManager.h"
#include "../../Core/System.h"                         // the welcome timeout clock

#include <cstdio>
#include <cstring>
#include <map>

namespace dice {
namespace hfe {
namespace openbf2_channel {

// The payloads must fit PostRemoteEvent's 52-byte buffer; the layouts are the wire format.
static_assert(sizeof(Handshake) == 12, "Handshake is a 12-byte wire payload");
static_assert(sizeof(KillDistance) == 8, "KillDistance is an 8-byte wire payload");
static_assert(sizeof(KillVehicle) == 8, "KillVehicle is an 8-byte wire payload");
static_assert(sizeof(KillfeedPolicyMessage) == 16, "KillfeedPolicyMessage is a 16-byte wire payload");

namespace {

// Reads a handshake from a received 52-byte event buffer. False for a foreign layout.
bool readHandshake(const void* payload, Handshake& out)
{
    if (!payload)
        return false;
    std::memcpy(&out, payload, sizeof(out));
    return out.version == kHandshakeVersion && out.magic == kMagic;
}

void sendHandshake(unsigned int connectionId, MessageType type, unsigned int capabilities, bool toServer)
{
    if (!game)
        return;
    Handshake message;
    message.version         = kHandshakeVersion;
    message.protocolVersion = kProtocolVersion;
    message.reserved        = 0;
    message.magic           = kMagic;
    message.capabilities    = capabilities;
    PostRemoteEvent* const event = new PostRemoteEvent(kCategory, static_cast<unsigned int>(type), false,
                                                       &message, sizeof(message), 0.0f);
    if (toServer)
        game->sendGameEvent(0xFFFFFFFFu, event);        // a client has the one server connection
    else
        game->sendGameEventIfConnected(connectionId, event);
}

// ---- Server state ----

struct Peer
{
    unsigned int protocolVersion;
    unsigned int capabilities;           // what the client announced
    unsigned int announcedCapabilities;  // what the last welcome to it said
};

std::map<int, Peer>& peers()
{
    static std::map<int, Peer> value;
    return value;
}

void sendKillfeedPolicy(int connectionId);

void serverHandleHello(int connectionId, const void* payload)
{
    Handshake hello;
    if (connectionId < 0 || !readHandshake(payload, hello))
        return;                                       // malformed or foreign: ignored
    Peer peer;
    peer.protocolVersion       = hello.protocolVersion;
    peer.capabilities          = hello.capabilities;
    peer.announcedCapabilities = serverCapabilities();
    peers()[connectionId] = peer;
    sendHandshake(static_cast<unsigned int>(connectionId), MSG_WELCOME, peer.announcedCapabilities, false);
    sendKillfeedPolicy(connectionId);   // [CHOSEN, not recovered] right after the welcome
}

// Sends the server's kill feed policy to one connection that announced CAP_KILLFEED_POLICY.
void sendKillfeedPolicy(int connectionId)
{
    if (!game || connectionId < 0 || !(serverClientCapabilities(connectionId) & CAP_KILLFEED_POLICY))
        return;
    const killfeed_policy::KillfeedPolicy& policy = killfeed_policy::killfeedPolicy();
    KillfeedPolicyMessage message;
    std::memset(&message, 0, sizeof(message));
    message.version = kKillfeedPolicyVersion;
    message.flags = static_cast<unsigned char>(
        (policy.headshotShow ? kPolicyHeadshotShow : 0) | (policy.vehicleShow ? kPolicyVehicleShow : 0) |
        (policy.distanceShow ? kPolicyDistanceShow : 0) | (policy.allowPersonalKillfeed ? kPolicyAllowPersonal : 0));
    for (int kind = 0; kind < 3; ++kind)
    {
        message.minRange[kind] = static_cast<unsigned short>(policy.minRange[kind]);
        message.maxRange[kind] = static_cast<unsigned short>(policy.maxRange[kind]);
    }
    PostRemoteEvent* const event = new PostRemoteEvent(kCategory, MSG_KILLFEED_POLICY, false, &message,
                                                       sizeof(KillfeedPolicyMessage), 0.0f);
    game->sendGameEventIfConnected(static_cast<unsigned int>(connectionId), event);
}

// True when a kill detail goes to this connection: everyone while the piece is shown; while it is hidden
// but personal lines are allowed (openbf2.sv.killfeed.*), only the killer, plus the victim when
// toVictim. A hidden distance never goes to the victim, so it cannot point back at a sniper.
bool killDetailRecipient(int connectionId, bool shown, unsigned int victimPlayerId, unsigned int killerPlayerId,
                         bool toVictim)
{
    if (shown)
        return true;
    if (!killfeed_policy::killfeedPolicy().allowPersonalKillfeed)
        return false;
    return static_cast<unsigned int>(connectionId) == killerPlayerId
        || (toVictim && static_cast<unsigned int>(connectionId) == victimPlayerId);
}

// The server's category 7 listener. Only MSG_HELLO is meant for it; everything else is ignored.
class ServerListener : public IEventListener
{
public:
    virtual void handleEvent(EventCategory category, unsigned int id, EventNode* node, void* userData)
    {
        if (category != kCategory || id != MSG_HELLO || !node)
            return;
        // PostRemoteEvent::executeServer posts a RemoteEvent carrying the sender's connection id.
        serverHandleHello(static_cast<RemoteEvent*>(node)->m_connectionId, userData);
    }
};

ServerListener& serverListener()
{
    static ServerListener value;
    return value;
}

// ---- Client state ----

struct ClientState
{
    ClientHandshakeState state;
    double       helloTime;
    unsigned int serverProtocol;
    unsigned int serverCaps;
};

ClientState& client()
{
    static ClientState value = { CLIENT_NOT_CONNECTED, 0.0, 0, 0 };
    return value;
}

double clientNow()
{
    return g_system ? g_system->getWallClockTime() : 0.0;
}

// The client's category 7 listener: the welcome and the kill feed messages. Unknown message types (and
// the server-bound hello) are ignored.
class ClientListener : public IEventListener
{
public:
    virtual void handleEvent(EventCategory category, unsigned int id, EventNode*, void* userData)
    {
        if (category != kCategory || !userData)
            return;
        if (id == MSG_WELCOME)
            clientHandleWelcome(userData);
        else if (id == MSG_KILL_DISTANCE)
            clientHandleKillDistance(userData);
        else if (id == MSG_KILL_VEHICLE)
            clientHandleKillVehicle(userData);
        else if (id == MSG_KILLFEED_POLICY)
            clientHandleKillfeedPolicy(userData);
    }
};

ClientListener& clientListener()
{
    static ClientListener value;
    return value;
}

} // namespace

// ---- Server ----

void serverRegisterListener()
{
    peers().clear();
    if (eventManager)
        eventManager->registerEventHandler(kCategory, &serverListener(), 0);
}

void serverUnregisterListener()
{
    if (eventManager)
        eventManager->unregisterEventHandler(kCategory, &serverListener());
    peers().clear();
}

void serverForgetConnection(int connectionId)
{
    peers().erase(connectionId);
}

unsigned int serverClientCapabilities(int connectionId)
{
    std::map<int, Peer>::const_iterator it = peers().find(connectionId);
    return it == peers().end() ? 0u : it->second.capabilities;
}

unsigned int serverCapabilities()
{
    unsigned int caps = 0;
    if (killfeed_policy::killfeedSendsDistance())
        caps |= CAP_KILL_DISTANCE;
    if (killfeed_policy::killfeedSendsVehicle())
        caps |= CAP_KILL_VEHICLE;
    caps |= CAP_KILLFEED_POLICY;
    return caps;
}

void serverCapabilitiesChanged()
{
    const unsigned int caps = serverCapabilities();
    for (std::map<int, Peer>::iterator it = peers().begin(); it != peers().end(); ++it)
    {
        if (it->second.announcedCapabilities == caps)
            continue;
        it->second.announcedCapabilities = caps;
        sendHandshake(static_cast<unsigned int>(it->first), MSG_WELCOME, caps, false);
    }
}

void serverKillfeedPolicyChanged()
{
    for (std::map<int, Peer>::const_iterator it = peers().begin(); it != peers().end(); ++it)
        sendKillfeedPolicy(it->first);
}

void sendKillDistance(const KillDistance& message)
{
    if (!game)
        return;
    KillDistance copy = message;
    copy.version = kKillDistanceVersion;
    int sent = 0;
    for (std::map<int, Peer>::const_iterator it = peers().begin(); it != peers().end(); ++it)
    {
        if (!(it->second.capabilities & CAP_KILL_DISTANCE))
            continue;                                 // an OpenBF2 client too old for this message
        if (!killDetailRecipient(it->first, killfeed_policy::killfeedPolicy().distanceShow, copy.victimPlayerId,
                                 copy.killerPlayerId, false))
            continue;                                 // [CHOSEN, not recovered] hidden: personal lines only
        // One event per connection: _sendGameEvent takes its own reference and drops the caller's.
        PostRemoteEvent* const event = new PostRemoteEvent(kCategory, MSG_KILL_DISTANCE, false, &copy,
                                                           sizeof(KillDistance), 0.0f);
        game->sendGameEventIfConnected(static_cast<unsigned int>(it->first), event);
        ++sent;
    }
}

void sendKillVehicle(const KillVehicle& message)
{
    if (!game)
        return;
    KillVehicle copy = message;
    copy.version = kKillVehicleVersion;
    int sent = 0;
    for (std::map<int, Peer>::const_iterator it = peers().begin(); it != peers().end(); ++it)
    {
        if (!(it->second.capabilities & CAP_KILL_VEHICLE))
            continue;                                 // an OpenBF2 client too old for this message
        if (!killDetailRecipient(it->first, killfeed_policy::killfeedPolicy().vehicleShow, copy.victimPlayerId,
                                 copy.killerPlayerId, true))
            continue;                                 // [CHOSEN, not recovered] hidden: personal lines only
        // One event per connection: _sendGameEvent takes its own reference and drops the caller's.
        PostRemoteEvent* const event = new PostRemoteEvent(kCategory, MSG_KILL_VEHICLE, false, &copy,
                                                           sizeof(KillVehicle), 0.0f);
        game->sendGameEventIfConnected(static_cast<unsigned int>(it->first), event);
        ++sent;
    }
}

// ---- Client ----

void clientRegisterListener()
{
    if (eventManager)
        eventManager->registerEventHandler(kCategory, &clientListener(), 0);
}

void clientUnregisterListener()
{
    if (eventManager)
        eventManager->unregisterEventHandler(kCategory, &clientListener());
    clientReset();
}

void clientSendHello()
{
    ClientState& c = client();
    c.state          = CLIENT_WAITING;
    c.helloTime      = clientNow();
    c.serverProtocol = 0;
    c.serverCaps     = 0;
    // The client can use every message this build knows.
    sendHandshake(0xFFFFFFFFu, MSG_HELLO, CAP_KILL_DISTANCE | CAP_KILL_VEHICLE | CAP_KILLFEED_POLICY, true);
}

void clientHandleWelcome(const void* payload)
{
    Handshake welcome;
    if (!readHandshake(payload, welcome))
        return;
    ClientState& c = client();
    c.state          = CLIENT_OPENBF2;
    c.serverProtocol = welcome.protocolVersion;
    c.serverCaps     = welcome.capabilities;
}

namespace {
// Kill distances waiting for their KilledByEvent, by victim player id. The server sends each one right
// before the kill event in the same queue, so an entry normally lives for one event dispatch.
struct StoredKillDistance
{
    unsigned int killerPlayerId;
    float        metres;
    double       time;
};

std::map<unsigned int, StoredKillDistance>& storedKillDistances()
{
    static std::map<unsigned int, StoredKillDistance> value;
    return value;
}
} // namespace

void clientHandleKillDistance(const void* payload)
{
    if (!payload)
        return;
    KillDistance message;
    std::memcpy(&message, payload, sizeof(message));
    if (message.version != kKillDistanceVersion || !(message.metres >= 0.0f))
        return;                                       // unknown layout or a NaN: ignored
    const double now = clientNow();
    std::map<unsigned int, StoredKillDistance>& store = storedKillDistances();
    for (std::map<unsigned int, StoredKillDistance>::iterator it = store.begin(); it != store.end();)
    {
        if (now - it->second.time > kKillDistanceMaxAgeSeconds)
            store.erase(it++);
        else
            ++it;
    }
    StoredKillDistance entry;
    entry.killerPlayerId = message.killerPlayerId;
    entry.metres         = message.metres;
    entry.time           = now;
    store[message.victimPlayerId] = entry;
}

bool clientTakeKillDistance(unsigned int victimPlayerId, unsigned int killerPlayerId, float& metres)
{
    std::map<unsigned int, StoredKillDistance>& store = storedKillDistances();
    std::map<unsigned int, StoredKillDistance>::iterator it = store.find(victimPlayerId);
    if (it == store.end())
        return false;
    const StoredKillDistance entry = it->second;
    store.erase(it);
    if (entry.killerPlayerId != killerPlayerId || clientNow() - entry.time > kKillDistanceMaxAgeSeconds)
        return false;
    metres = entry.metres;
    return true;
}

namespace {
// Destroyed-vehicle kills waiting for their KilledByEvent, by victim player id (as for the distances).
struct StoredKillVehicle
{
    unsigned int killerPlayerId;
    unsigned int templateId;
    double       time;
};

std::map<unsigned int, StoredKillVehicle>& storedKillVehicles()
{
    static std::map<unsigned int, StoredKillVehicle> value;
    return value;
}
} // namespace

void clientHandleKillVehicle(const void* payload)
{
    if (!payload)
        return;
    KillVehicle message;
    std::memcpy(&message, payload, sizeof(message));
    if (message.version != kKillVehicleVersion || !(message.flags & kKillVehicleDestroyed))
        return;                                       // unknown layout or nothing destroyed: ignored
    const double now = clientNow();
    std::map<unsigned int, StoredKillVehicle>& store = storedKillVehicles();
    for (std::map<unsigned int, StoredKillVehicle>::iterator it = store.begin(); it != store.end();)
    {
        if (now - it->second.time > kKillDistanceMaxAgeSeconds)
            store.erase(it++);
        else
            ++it;
    }
    StoredKillVehicle entry;
    entry.killerPlayerId = message.killerPlayerId;
    entry.templateId     = message.templateId;
    entry.time           = now;
    store[message.victimPlayerId] = entry;
}

bool clientTakeKillVehicle(unsigned int victimPlayerId, unsigned int killerPlayerId, unsigned int& templateId)
{
    std::map<unsigned int, StoredKillVehicle>& store = storedKillVehicles();
    std::map<unsigned int, StoredKillVehicle>::iterator it = store.find(victimPlayerId);
    if (it == store.end())
        return false;
    const StoredKillVehicle entry = it->second;
    store.erase(it);
    if (entry.killerPlayerId != killerPlayerId || clientNow() - entry.time > kKillDistanceMaxAgeSeconds)
        return false;
    templateId = entry.templateId;
    return true;
}

namespace {
// The kill feed policy the server sent (MSG_KILLFEED_POLICY); none from a vanilla server.
struct StoredKillfeedPolicy
{
    bool                  present;
    KillfeedPolicyMessage message;
};

StoredKillfeedPolicy& storedKillfeedPolicy()
{
    static StoredKillfeedPolicy value = {false, KillfeedPolicyMessage()};
    return value;
}
} // namespace

void clientHandleKillfeedPolicy(const void* payload)
{
    if (!payload)
        return;
    KillfeedPolicyMessage message;
    std::memcpy(&message, payload, sizeof(message));
    if (message.version != kKillfeedPolicyVersion)
        return;                                       // unknown layout: ignored
    StoredKillfeedPolicy& stored = storedKillfeedPolicy();
    stored.present = true;
    stored.message = message;
}

bool clientKillfeedPolicy(KillfeedPolicyMessage& out)
{
    const StoredKillfeedPolicy& stored = storedKillfeedPolicy();
    if (!stored.present)
        return false;
    out = stored.message;
    return true;
}

void clientReset()
{
    ClientState& c = client();
    c.state          = CLIENT_NOT_CONNECTED;
    c.helloTime      = 0.0;
    c.serverProtocol = 0;
    c.serverCaps     = 0;
    storedKillDistances().clear();
    storedKillVehicles().clear();
    storedKillfeedPolicy().present = false;
}

ClientHandshakeState clientState()
{
    ClientState& c = client();
    if (c.state == CLIENT_WAITING && clientNow() - c.helloTime > kWelcomeTimeoutSeconds)
    {
        c.state = CLIENT_VANILLA;
    }
    return c.state;
}

unsigned int clientServerProtocolVersion()
{
    return clientState() == CLIENT_OPENBF2 ? client().serverProtocol : 0u;
}

unsigned int clientServerCapabilities()
{
    return clientState() == CLIENT_OPENBF2 ? client().serverCaps : 0u;
}

} // namespace openbf2_channel
} // namespace hfe
} // namespace dice
