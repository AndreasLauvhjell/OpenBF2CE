// [CHOSEN, not recovered] The OpenBF2 extension channel: messages between OpenBF2 clients and OpenBF2
// servers that a vanilla peer drops unread. No retail image has it; own-use change, no upstream issue
// yet. Format and safety: META\notes\server\openbf2-extension-channel.md.
//
// mod/combo carries only the handshake and the kill feed messages (from mod/killfeed and the channel
// part of net-tuning stage 2, 1765020). The hit details message (id 1, CAP_HIT_DETAILS) and everything
// else from net tuning are not here; id 1 and capability bits 0-1 stay reserved so the wire format
// matches the other OpenBF2 builds.
//
// Carrier: a PostRemoteEvent in event category 7, in both directions. No retail 1.50 image (BF2.exe,
// bf2_w32ded.exe, the Linux bf2) registers a category 7 handler, and EventManager::postEvent releases
// an event whose category has no handler without dispatching it, so a vanilla client and a vanilla
// server both drop these messages silently. The event id is the message type; the payload (at most
// PostRemoteEvent's 0x34 bytes) starts with a version byte. Unknown types and unknown versions are
// ignored, never misread. Messages are event-driven only, never per tick.
//
// Handshake: after joining, an OpenBF2 client sends MSG_HELLO (magic, protocol version, its
// capability bits). An OpenBF2 server records the capabilities per connection and answers with
// MSG_WELCOME (its version and capabilities). The server sends an extension message only to a
// connection that announced the matching capability; the client uses a server feature only when the
// welcome announced it. A vanilla server never answers: after kWelcomeTimeoutSeconds the client
// treats the server as vanilla.
#ifndef GAME_COMMON_OPENBF2CHANNEL_H
#define GAME_COMMON_OPENBF2CHANNEL_H

#include "../IEventListener.h"

namespace dice {
namespace hfe {
namespace openbf2_channel {

// The event category the channel uses (unused by retail, inside EventManager's handler array).
const EventCategory kCategory = static_cast<EventCategory>(7);

// Message types (the PostRemoteEvent event id). Id 1 (hit details) is reserved; not in mod/combo.
enum MessageType
{
    MSG_HELLO         = 0,   // client -> server, once after joining: Handshake
    MSG_KILL_DISTANCE = 2,   // server -> every CAP_KILL_DISTANCE client: KillDistance, before each KilledByEvent
    MSG_WELCOME       = 3,   // server -> client, the answer to MSG_HELLO: Handshake
    MSG_KILL_VEHICLE  = 4,   // server -> every CAP_KILL_VEHICLE client: KillVehicle, before a KilledByEvent
    MSG_KILLFEED_POLICY = 5  // server -> CAP_KILLFEED_POLICY client: KillfeedPolicyMessage, after the welcome
};

// Capability bits, announced in MSG_HELLO (what the client can use) and MSG_WELCOME (what the server
// sends now). Bits 0 and 1 are reserved (hit details on the net-tuning and tick-rate branches).
enum Capability
{
    CAP_KILL_DISTANCE = 1u << 2, // MSG_KILL_DISTANCE
    CAP_KILL_VEHICLE  = 1u << 3, // MSG_KILL_VEHICLE
    CAP_KILLFEED_POLICY = 1u << 4 // MSG_KILLFEED_POLICY
};

// "OBF2" read as a little-endian 32-bit value.
const unsigned int  kMagic = 0x3246424Fu;
// The channel protocol version both sides announce.
const unsigned char kProtocolVersion = 1;
const unsigned char kHandshakeVersion = 1;
// A client that got no welcome this long after its hello treats the server as vanilla.
const double kWelcomeTimeoutSeconds = 5.0;
// MSG_KILL_DISTANCE layout version.
const unsigned char kKillDistanceVersion = 1;
// A stored kill distance older than this is dropped unread (its KilledByEvent never came).
const double kKillDistanceMaxAgeSeconds = 2.0;
// MSG_KILL_VEHICLE layout version.
const unsigned char kKillVehicleVersion = 1;
// KillVehicle::flags: the killing damage hit the victim's vehicle and destroyed it.
const unsigned char kKillVehicleDestroyed = 1u << 0;
// MSG_KILLFEED_POLICY layout version and its flag bits.
const unsigned char kKillfeedPolicyVersion = 1;
const unsigned char kPolicyHeadshotShow    = 1u << 0;   // openbf2.sv.killfeed.headshot.show
const unsigned char kPolicyVehicleShow     = 1u << 1;   // openbf2.sv.killfeed.destroyedvehicle.show
const unsigned char kPolicyDistanceShow    = 1u << 2;   // openbf2.sv.killfeed.distance.show
const unsigned char kPolicyAllowPersonal   = 1u << 3;   // openbf2.sv.killfeed.allowpersonalkillfeed

// MSG_HELLO and MSG_WELCOME payload, 12 bytes.
struct Handshake
{
    unsigned char  version;           // kHandshakeVersion
    unsigned char  protocolVersion;   // the sender's kProtocolVersion
    unsigned short reserved;          // 0
    unsigned int   magic;             // kMagic
    unsigned int   capabilities;      // Capability bits
};

// MSG_KILL_DISTANCE payload, 8 bytes. Sent to every client that announced CAP_KILL_DISTANCE, in the same
// per-connection event queue and just before the retail KilledByEvent of the same kill, so the client
// holds the distance when its kill feed line is built. Only kills by a projectile carry a distance:
// from the projectile's start position (muzzle, throw or launch point) to the victim's root.
struct KillDistance
{
    unsigned char version;          // kKillDistanceVersion
    unsigned char victimPlayerId;   // KilledByData::victimPlayerId of the same kill
    unsigned char killerPlayerId;   // KilledByData::killerPlayerId of the same kill
    unsigned char flags;            // 0; reserved
    float         metres;           // 3D shot distance in metres
};

// MSG_KILL_VEHICLE payload, 8 bytes. Sent like KillDistance, right before the KilledByEvent of a player
// who died because the damage destroyed the vehicle they sat in: the killing hit landed on the vehicle
// itself (not on the soldier) and took its hit points to zero. The client derives the vehicle kind from
// the template.
struct KillVehicle
{
    unsigned char version;          // kKillVehicleVersion
    unsigned char victimPlayerId;   // KilledByData::victimPlayerId of the same kill
    unsigned char killerPlayerId;   // KilledByData::killerPlayerId of the same kill
    unsigned char flags;            // kKillVehicleDestroyed
    unsigned int  templateId;       // object template id of the destroyed vehicle (its root object)
};

// MSG_KILLFEED_POLICY payload, 16 bytes: the server's openbf2.sv.killfeed.* settings, which the client
// applies as limits on its own openbf2.hud.killfeed.* settings (Game/GameServer/KillfeedPolicy.h). Ranges
// are whole metres by kind: [0] headshot, [1] vehicle, [2] longkill; a maxRange of 0 means no ceiling.
struct KillfeedPolicyMessage
{
    unsigned char  version;         // kKillfeedPolicyVersion
    unsigned char  flags;           // kPolicy* bits
    unsigned short reserved;        // 0
    unsigned short minRange[3];
    unsigned short maxRange[3];
};

// ---- Server ----

// Registers / unregisters the server's category 7 listener (GameServer constructor / shutdown).
void serverRegisterListener();
void serverUnregisterListener();

// Forgets a closed connection's handshake (GameServer::closeClientConnection).
void serverForgetConnection(int connectionId);

// The capabilities the client on this connection announced; 0 for a vanilla client, a bot or an
// unknown connection. On the server the connection id is the player id.
unsigned int serverClientCapabilities(int connectionId);

// The capabilities the server announces now (from the kill feed policy, Game/GameServer/KillfeedPolicy.h).
unsigned int serverCapabilities();

// Sends a new welcome to every handshaked connection whose announced capabilities no longer match.
void serverCapabilitiesChanged();

// Sends a kill distance to every connection that announced CAP_KILL_DISTANCE (vanilla clients and bots
// get nothing). Called by serverGiveDamage right before it sends the KilledByEvent.
void sendKillDistance(const KillDistance& message);

// Sends a destroyed-vehicle kill to every connection that announced CAP_KILL_VEHICLE. Called by
// serverGiveDamage right before it sends the KilledByEvent.
void sendKillVehicle(const KillVehicle& message);

// Re-sends the kill feed policy (killfeed_policy::killfeedPolicy) to every CAP_KILLFEED_POLICY connection.
// Called by the openbf2.sv.killfeed.* setters; a new connection gets it right after its welcome.
void serverKillfeedPolicyChanged();

// ---- Client ----

enum ClientHandshakeState
{
    CLIENT_NOT_CONNECTED = 0,   // no hello sent on this connection yet
    CLIENT_WAITING       = 1,   // hello sent, no welcome yet
    CLIENT_OPENBF2       = 2,   // welcome received
    CLIENT_VANILLA       = 3    // no welcome within kWelcomeTimeoutSeconds
};

// Registers / unregisters the client's category 7 listener (GameClient constructor / shutdown). The
// listener dispatches the welcome and the kill feed messages to the functions below.
void clientRegisterListener();
void clientUnregisterListener();

// Sends the hello to the server (GameClient, after it told the server it is ready).
void clientSendHello();

// A welcome arrived. payload is the 52-byte event buffer.
void clientHandleWelcome(const void* payload);

// A kill distance arrived. payload is the 52-byte event buffer.
void clientHandleKillDistance(const void* payload);

// The distance the server sent for this kill (matched by victim and killer id), consumed by the HUD's
// kill handler. False when none is stored: vanilla server, no projectile, or the server setting off.
bool clientTakeKillDistance(unsigned int victimPlayerId, unsigned int killerPlayerId, float& metres);

// A destroyed-vehicle kill arrived. payload is the 52-byte buffer.
void clientHandleKillVehicle(const void* payload);

// The vehicle template the server sent for this kill (matched by victim and killer id), consumed by the
// HUD's kill handler. False when none is stored: vanilla server, no vehicle destroyed, or setting off.
bool clientTakeKillVehicle(unsigned int victimPlayerId, unsigned int killerPlayerId, unsigned int& templateId);

// The server's kill feed policy arrived (the client's category 7 listener).
void clientHandleKillfeedPolicy(const void* payload);

// The kill feed policy of this server; false for a vanilla server (the client's own settings apply).
bool clientKillfeedPolicy(KillfeedPolicyMessage& out);

// Forgets the server's handshake (GameClient::disconnect).
void clientReset();

// The handshake state; turns WAITING into VANILLA once the timeout has passed.
ClientHandshakeState clientState();

// The server's protocol version and capabilities from its welcome (0 when not CLIENT_OPENBF2).
unsigned int clientServerProtocolVersion();
unsigned int clientServerCapabilities();

} // namespace openbf2_channel
} // namespace hfe
} // namespace dice

#endif // GAME_COMMON_OPENBF2CHANNEL_H
