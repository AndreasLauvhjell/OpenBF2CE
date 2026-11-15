// [INFERRED] split of the attested Menu/BF2HudManager.cpp.
// BF2.exe 753DE0, all 5845 bytes read.
#include "BF2HudManager.h"
#if !DEDICATED
#include "HudColors.h"
#include "HudKitSelection.h"
#include "HudQuestionManager.h"
#include "InlineIcon.h"                             // [CHOSEN, not recovered] kill feed icons
#include "Ref2System.h"
#include "../Core/Debug.h"
#include "../Core/Ids.h"
#include "../Core/StringUtil.h"
#include "../Game/Common/OpenBF2Channel.h"          // [CHOSEN, not recovered] kill distance
#include "../Game/HudInformationLayer.h"
#include "../Game/IGame.h"
#include "../Game/IGameLogic.h"
#include "../Game/Main/BF2ClientSeams.h"
#include "../Game/Main/KillfeedSettings.h"          // [CHOSEN, not recovered] openbf2.hud.killfeed*
#include "../Game/Objects/IObject.h"
#include "../Game/Objects/IObjectTemplate.h"
#include "../Game/Objects/IPlayer.h"
#include "../Game/Objects/VehicleHudCompTemplate.h"
#include "../Game/Objects/WeaponHudCompTemplate.h"
#include "../IO/Network/NetworkManager.h"
#include "../Physics/MaterialManager.h"            // [CHOSEN, not recovered] Human_head lookup
#include "../Scene/Object/PlayerManager.h"
#include "../Scene/Object/ObjectTypes/Base/ObjectTemplateManager.h"
#include "../Sound/BfSound.h"
#include <cstdio>
#include <cstring>
#include <string>
#include <utility>

namespace dice { namespace hfe {
extern IGameLogic* gameLogic;
namespace world { extern IBase* materialManager; }   // Core/Globals.cpp
namespace hud {
namespace {
// Scalar payload reads retain the shipped unaligned accesses. In particular,
// event 14 falls through to 3 without acquiring a KilledByData object lifetime.
template<class T> T eventValue(const void* data, unsigned offset = 0)
{
    T value;
    std::memcpy(&value, static_cast<const unsigned char*>(data) + offset, sizeof(value));
    return value;
}
const char* const hudSource = "C:\\dice\\Projects\\BF2Branches\\Patch_1_50\\Code\\BF2\\Menu\\BF2HudManager.cpp";

// [CHOSEN, not recovered] Team colour tokens for kill messages. #PLAYERCOLOR#, #PLAYERCOLOR1# and
// #PLAYERCOLOR2# expand to the four RGBA digits of a section-sign colour escape (Menu/ColorEscape.h).
// A player who would get HudChat's commander or squad row tint gets that palette colour, so the
// retail squad highlight survives; otherwise the retail chat blue for the local player's team, the
// chat red for any other team, white without a local player. Retail strings contain no such token, so they are unchanged.
// Local experiment, no issue yet.
std::string colorDigits(const meme::Color& color)
{
    const float channels[4] = {color.r, color.g, color.b, color.a};
    std::string digits;
    for (float channel : channels) {
        int value = static_cast<int>(channel * 15.0f + 0.5f);
        if (value < 0) value = 0;
        if (value > 15) value = 15;
        digits += "0123456789ABCDEF"[value];
    }
    return digits;
}

std::string teamColorDigits(world::IPlayer* local, int team)
{
    if (!local) return "FFFF";
    // The retail chat palette: entry 2 is the friendly blue, entry 1 the enemy red.
    return colorDigits(defaultChatColors[team == local->getTeam() ? 2 : 1]);
}

// Mirrors the tint rule in HudChat::selectPlayerColors for one player.
std::string playerColorDigits(world::IPlayer* local, world::IPlayer* player, int team)
{
    if (player && player->getIsCommander()) return colorDigits(commanderChatColor);
    if (local && player && player->getSquadId() != 0 && local->getTeam() == player->getTeam()
        && (local->getSquadId() == player->getSquadId()
            || (local->getIsCommander() && player->getIsSquadLeader())))
        return colorDigits(squadChatColors[1]); // entry 0 is white; 1..3 hold the green
    return teamColorDigits(local, team);
}

// [CHOSEN, not recovered] OpenBF2 kill detail: " HEADSHOT", " 123 m" or both, appended to the weapon of a
// kill feed line and to the "you killed" line (openbf2.hud.killfeed.headshot.show / killfeedDistance,
// Game/Main/KillfeedSettings.h). Not in retail; own-use change, no upstream issue.
// - HEADSHOT: KilledByData +4 holds the surface material of the killing hit, which retail sends to
//   every client but never reads. Soldier head capsules use the material named Human_head; it is looked
//   up by name, so a mod that renumbers its materials still matches. Explosions carry 0xFFFF.
// - Distance: an OpenBF2 server sends it over the extension channel right before the kill event
//   (openbf2_channel::MSG_KILL_DISTANCE); whole metres, truncated like the range finder.
bool isHeadMaterial(unsigned int material)
{
    world::MaterialManager* materials = static_cast<world::MaterialManager*>(world::materialManager);
    if (!materials || material == 0xFFFF) return false;
    const unsigned int head = materials->getMaterialId(std::string("Human_head"));
    return head != 0 && head == material;
}

// What a kill event adds to its lines. Only read for event 3: event 14 falls through with a two-byte
// payload.
struct KillDetail
{
    bool  headshot;      // killing hit on Human_head and openbf2.hud.killfeed.headshot.show on
    bool  headHit;       // killing hit on Human_head, whatever the display setting
    bool  hasDistance;   // the server sent a distance and openbf2.hud.killfeed.distance.show on
    float metres;
    bool  vehicleDestroyed;         // the kill destroyed the victim's vehicle (openbf2.hud.killfeed.destroyedvehicle.show on)
    bool  vehicleHit;               // the server said the kill destroyed the victim's vehicle, whatever the display
    unsigned int vehicleTemplateId; // that vehicle's template
};

// [CHOSEN, not recovered] openbf2.hud.killfeed.destroyedvehicle.show: the lexicon key of a destroyed vehicle's name, the
// same ObjectTemplate.vehicleHud.hudName the kill line's #WEAPON# uses for vehicle kills. A template
// without a vehicle HUD (some emplacements) falls back to its template name.
std::string destroyedVehicleNameKey(unsigned int templateId)
{
    world::IObjectTemplate* vehicle = world::objectTemplateManager->getTemplate(templateId);
    if (!vehicle) return std::string();
    auto* component = vehicle->queryComponent(world::IID_IVehicleHudCompTemplate);
    if (component) {
        const std::string name = static_cast<world::VehicleHudCompTemplate*>(component)->getHudName();
        if (!name.empty()) return name;
    }
    return vehicle->getName();
}

// Always takes the stored distance, so it never waits for a later kill of the same victim.
KillDetail readKillDetail(const void* data)
{
    const unsigned int killerId = eventValue<unsigned char>(data);
    const unsigned int victimId = eventValue<unsigned char>(data, 1);
    const unsigned int material = eventValue<unsigned short>(data, 4);
    KillDetail detail;
    detail.metres = 0.0f;
    detail.vehicleDestroyed = false;
    detail.vehicleHit = false;
    detail.vehicleTemplateId = 0;
    const bool sent = openbf2_channel::clientTakeKillDistance(victimId, killerId, detail.metres);
    const bool head = isHeadMaterial(material);
    detail.headHit = head;
    detail.headshot = head && killfeed_settings::killfeedHeadshot();
    detail.hasDistance = sent && killfeed_settings::killfeedDistance();
    unsigned int vehicleTemplateId = 0;
    const bool destroyed = openbf2_channel::clientTakeKillVehicle(victimId, killerId, vehicleTemplateId);
    detail.vehicleDestroyed = destroyed && killfeed_settings::killfeedVehicle();
    detail.vehicleTemplateId = vehicleTemplateId;
    detail.vehicleHit = destroyed;
    return detail;
}

// [CHOSEN, not recovered] openbf2.hud.killfeedIcons (Game/Main/KillfeedSettings.h): placeholder headshot
// icon until a dedicated one exists, the scoreboard's 15x15 death icon.
const char* const killfeedHeadshotIcon = "Ingame/Scoreboard/Icons/icon_Death.tga";

// The inline icon escape for a texture, or an empty string when icons are off or the texture is missing.
std::wstring inlineIconText(dice::meme::System* system, const std::string& path)
{
    if (!system) return std::wstring();
    const int slot = dice::meme::registerInlineIcon(system, path);
    return slot >= 0 ? dice::meme::inlineIconEscape(slot) : std::wstring();
}

// Replaces every token in text (std::wstring::replace on each hit).
std::wstring replaceAll(std::wstring text, const std::wstring& token, const std::wstring& value)
{
    for (std::wstring::size_type at = text.find(token); at != std::wstring::npos;
         at = text.find(token, at + value.size()))
        text.replace(at, token.size(), value);
    return text;
}

// [CHOSEN, not recovered] An optional lexicon line the player may add; when it is missing (the lexicon
// hands back the key itself) the built-in English text is used. The client never writes the lexicon.
std::wstring localizedOr(BF2HudManager* hud, const std::string& key, const wchar_t* fallback)
{
    std::wstring text = hud->getLocalizedString(key);
    return text.empty() || text == toWideString(key) ? std::wstring(fallback) : text;
}

// The three kill detail pieces, each empty when it does not apply.
//   #HEADSHOT#  the headshot icon (openbf2.hud.killfeedIcons 1|2 and the texture loads), else "HEADSHOT".
//   #VEHICLE#   optional HUD_TEXT_KILLFEED_VEHICLE_DESTROYED with #VEHICLETYPE# (default
//               "#VEHICLETYPE# DESTROYED"); #VEHICLETYPE# is the destroyed vehicle's localized HUD name.
//   #DISTANCE#  "123 m", only when the distance lies within openbf2.hud.killfeed.distance.minrange.<kind>
//               and maxrange.<kind> of the kill's one kind (killKind): judged once per kill.
struct KillTexts
{
    std::wstring headshot;
    std::wstring vehicle;
    std::wstring distance;
};

// A kill's one kind for the distance limits: a headshot first, then a destroyed vehicle, otherwise a long
// kill. Uses what happened, not what is displayed.
killfeed_settings::KillKind killKind(const KillDetail& detail)
{
    if (detail.headHit) return killfeed_settings::KILL_HEADSHOT;
    if (detail.vehicleHit) return killfeed_settings::KILL_VEHICLE;
    return killfeed_settings::KILL_LONG;
}

// True when metres lies within [minimum, maximum]; maximum 0 means no upper limit.
bool withinRange(float metres, int minimum, int maximum)
{
    return metres >= static_cast<float>(minimum) && (maximum <= 0 || metres <= static_cast<float>(maximum));
}

// [CHOSEN, not recovered] The server's kill feed policy (openbf2.sv.killfeed.*, MSG_KILLFEED_POLICY) as
// limits on the client's settings. A vanilla server sends none: the client's settings apply alone.
struct ServerKillfeedLimits
{
    bool present;
    openbf2_channel::KillfeedPolicyMessage policy;
};

ServerKillfeedLimits serverKillfeedLimits()
{
    ServerKillfeedLimits limits;
    limits.present = openbf2_channel::clientKillfeedPolicy(limits.policy);
    return limits;
}

// A piece the server shows, or hides from the public kill feed but allows on the personal lines
// ("You killed", "You were killed by") through allowpersonalkillfeed.
bool serverAllows(const ServerKillfeedLimits& limits, unsigned char showFlag, bool personal)
{
    if (!limits.present) return true;
    if (limits.policy.flags & showFlag) return true;
    return personal && (limits.policy.flags & openbf2_channel::kPolicyAllowPersonal);
}

// The pieces for one line. personal = "You killed" / "You were killed by"; otherwise the public kill feed.
// victim = the "You were killed by" line: a distance the server hides never shows there, so it cannot
// point back at the shooter (headshot and vehicle may).
// Distance limits: the higher minrange and the lower non-zero maxrange of client and server.
KillTexts buildKillTexts(BF2HudManager* hud, const KillDetail& detail, dice::meme::System* system,
                         const ServerKillfeedLimits& limits, bool personal, bool victim)
{
    KillTexts texts;
    if (detail.headshot && serverAllows(limits, openbf2_channel::kPolicyHeadshotShow, personal)) {
        const int mode = killfeed_settings::killfeedIcons();
        texts.headshot = mode == 1 || mode == 2 ? inlineIconText(system, killfeedHeadshotIcon) : std::wstring();
        if (texts.headshot.empty())
            texts.headshot = L"HEADSHOT";
    }
    if (detail.vehicleDestroyed && serverAllows(limits, openbf2_channel::kPolicyVehicleShow, personal)) {
        const std::string nameKey = destroyedVehicleNameKey(detail.vehicleTemplateId);
        const std::wstring name = nameKey.empty() ? std::wstring(L"VEHICLE") : hud->getLocalizedString(nameKey);
        texts.vehicle = replaceAll(localizedOr(hud, "HUD_TEXT_KILLFEED_VEHICLE_DESTROYED",
                                               L"#VEHICLETYPE# DESTROYED"), L"#VEHICLETYPE#", name);
    }
    const killfeed_settings::KillKind kind = killKind(detail);
    int minimum = killfeed_settings::killfeedMinRange(kind);
    int maximum = killfeed_settings::killfeedMaxRange(kind);
    if (limits.present) {
        const int serverMin = limits.policy.minRange[kind];
        const int serverMax = limits.policy.maxRange[kind];
        if (serverMin > minimum) minimum = serverMin;
        if (serverMax > 0 && (maximum <= 0 || serverMax < maximum)) maximum = serverMax;
    }
    if (detail.hasDistance && serverAllows(limits, openbf2_channel::kPolicyDistanceShow, personal && !victim)
        && withinRange(detail.metres, minimum, maximum))
        texts.distance = std::to_wstring(static_cast<unsigned int>(detail.metres)) + L" m";
    return texts;
}

// Fills #HEADSHOT#, #VEHICLE# and #DISTANCE# wherever the player put them in the lexicon line; a line
// without them shows no kill detail at all. An empty piece also takes the one space in front of its
// token, so "NAME #HEADSHOT# #DISTANCE#" never leaves double spaces.
std::wstring applyKillTokens(std::wstring text, const KillTexts& texts)
{
    const std::pair<const wchar_t*, const std::wstring*> pieces[3] = {
        std::make_pair(L"#HEADSHOT#", &texts.headshot),
        std::make_pair(L"#VEHICLE#", &texts.vehicle),
        std::make_pair(L"#DISTANCE#", &texts.distance)};
    for (const auto& piece : pieces) {
        const std::wstring token(piece.first);
        if (piece.second->empty())
            text = replaceAll(replaceAll(std::move(text), L" " + token, std::wstring()), token, std::wstring());
        else
            text = replaceAll(std::move(text), token, *piece.second);
    }
    return text;
}

// [CHOSEN, not recovered] openbf2.hud.killfeed.headshot.soundvolume 1-100: a 2D cue for the local player's
// own headshot on an enemy (the personal #HEADSHOT#); 0 = off. Built like the 2D help voice-overs in
// VoiceMessages (volume group 2); the retail hand grenade explosion stands in until a dedicated sound exists.
void playKillfeedHeadshotSound()
{
    const int volume = killfeed_settings::killfeedHeadshotSoundVolume();
    if (!g_bfSound || volume <= 0) return;
    static const char* const path = "Common/Material/Impact/Handgranade/Handgrenade_01.wav";
    static ISound* sound = nullptr;
    static int createdVolume = -1;
    if (volume != createdVolume) {
        // Built once per volume (openbf2.hud.killfeed.headshot.soundvolume); a change rebuilds it.
        createdVolume = volume;
        if (sound) { sound->release(); sound = nullptr; }
        ISoundEngine* engine = g_bfSound->engine();
        ISoundTemplate* t = engine ? engine->createTemplate() : nullptr;
        if (t) {
            const char* names[1] = {path};
            t->setFilenames(names, 1);
            t->setVolume(static_cast<float>(volume) / 100.0f);
            t->setLoopCount(1);
            t->setIs3dSound(false);
            t->setReverbLevel(0.0f);
            t->setGlobalEffectLevel(0.0f);
            t->setStopType(0);
            t->setVolumeGroup(2);
            t->setIsUsed();
            sound = t->createSound();
            t->release();
        }
    }
    if (sound) sound->play();
}
}

void BF2HudManager::handleGameLogicEvent(unsigned int id, EventNode*, void* data)
{
    using namespace world;
    switch (id) {
    case 8: {
        IPlayer* local = playerManager->getLocalHumanPlayer();
        if (!data || !local) return;
        const int localId = local->getId();
        if (localId != eventValue<int>(data)) return;
        const int pending = m_field11c->m_pendingTeam;
        const int team = eventValue<int>(data, 4);
        if (gameLogic && gameLogic->getGameStatus() == 1 && local->getIsAlive())
            m_field11c->m_field005 = true;
        const int displayed = team == -1 ? local->getTeam() : team;
        m_field11c->setDisplayedTeam(displayed);
        if (pending == -1) return;
        if (team == -1) {
            m_field11c->m_teamChangePending = false;
            return;
        }
        auto text = getLocalizedString(pending != team ? "HUD_HUD_MENU_TEAMS_EQUALIZE" : "HUD_TEXT_MENU_CHANGED_TEAM");
        text = replaceWideInputKeys(std::move(text));
        static_cast<HudInformationLayer*>(g_hudInformationLayer)->setDeathMessage(std::move(text));
        if (pending == team) {
            m_field11c->m_pendingTeam = team;
            m_field11c->suicide();
        }
        if (gameLogic) {
            const char* name = gameLogic->getTeamName(eventValue<int>(data, 4)).c_str();
            g_bfSound->playSound(formatString("%s_TeamSelect", name));
        }
        if (pending != team) {
            if (getMode() == 1) setDone(true);
        } else if (getMode() != 1) {
            setMode(1, true);
            m_field11c->setDisplayedTeam(team);
        }
        if (m_field100) m_field100->clear();
        refreshChatCategory(8);
        return;
    }
    case 14: {
        IPlayer* local = playerManager->getLocalHumanPlayer();
        IObject* object = io::networkManager->getObject(eventValue<unsigned short>(data));
        if (object && local && local->getDefaultVehicle() == object && getMode() == 1)
            setMode(0, true);
        // Retail falls through into kill-feed processing. ArmorGLComp posts only
        // two bytes here; the following larger payload reads are a shipped defect.
    }
    case 3: {
        IPlayer* local = playerManager->getLocalHumanPlayer();
        if (!data) return;
        // [CHOSEN, not recovered] See readKillDetail; event 14 falls through here with two bytes.
        KillDetail detail = {false, false, false, 0.0f, false, false, 0u};
        if (id == 3) detail = readKillDetail(data);
        const ServerKillfeedLimits limits = serverKillfeedLimits();
        const KillTexts killTexts = buildKillTexts(this, detail, ref2System, limits, false, false);     // kill feed
        const KillTexts personalTexts = buildKillTexts(this, detail, ref2System, limits, true, false); // You killed
        const KillTexts victimTexts = buildKillTexts(this, detail, ref2System, limits, true, true);    // You were killed by
        std::string weaponName;
        IPlayer* victim = playerManager->getPlayerFromId(eventValue<unsigned char>(data, 1));
        IPlayer* killer = eventValue<unsigned char>(data, 2)
            ? playerManager->getPlayerFromId(eventValue<unsigned char>(data)) : nullptr;
        bool localKill = false;
        if (local && victim && !local->getIsCommander()) {
            const int localId = local->getId();
            localKill = localId == eventValue<unsigned char>(data) && eventValue<unsigned char>(data, 2);
        }
        if (localKill) {
            const int victimTeam = victim->getTeam();
            if (victimTeam != local->getTeam()) {
                const char* name = victim->getName().c_str();
                const int team = local->getTeam();
                std::string copiedName(name);
                auto text = getLocalizedString("HUD_CHAT_YOUGOTAKILLSCORE");
                // [CHOSEN, not recovered] #HEADSHOT# / #VEHICLE# / #DISTANCE# where the lexicon line has them.
                text = applyKillTokens(std::move(text), personalTexts);
                text = replaceWideTokenFromString(std::move(text), "#PLAYERNAME#", std::move(copiedName));
                text = replaceWideTokenFromString(std::move(text), "#PLAYERCOLOR#",
                                                  playerColorDigits(local, victim, victimTeam));
                text = replaceWideInputKeys(std::move(text));
                addServerChat(std::move(text), team);
            }
        }
        if (killer) {
            bool missingWeapon = false;
            if (victim) {
                const int victimId = victim->getId();
                if (victimId == killer->getId()) {
                    const int idAgain = victim->getId();
                    IPlayer* resolved = playerManager->getPlayerFromId(idAgain);
                    if (resolved) {
                        const char* name = resolved->getName().c_str();
                        const int team = resolved->getTeam();
                        std::string copiedName(name);
                        auto text = getLocalizedString("HUD_HUD_KILLEDBY_MYSELF");
                        text = replaceWideInputKeys(std::move(text));
                        text = replaceWideTokenFromString(std::move(text), "#PLAYERNAME#", std::move(copiedName));
                        addTeamChat(std::move(text), nullptr, team);
                    }
                    return;
                }
            }
            bool localVictim = false;
            if (local) {
                const int localId = local->getId();
                localVictim = localId == eventValue<unsigned char>(data, 1);
            }
            if (localVictim) {
                auto text = getLocalizedString("HUD_TEXT_MENU_YOU_WERE_KILLED");
                text = replaceWideInputKeys(std::move(text));
                static_cast<HudInformationLayer*>(g_hudInformationLayer)->setDeathMessage(std::move(text));
                const char* name = killer->getName().c_str();
                const int team = local->getTeam();
                std::string copiedName(name);
                text = getLocalizedString("HUD_CHAT_YOUWEREKILLEDBY");
                // [CHOSEN, not recovered] the same tokens as "You killed" (applyKillTokens).
                text = applyKillTokens(std::move(text), victimTexts);
                text = replaceWideTokenFromString(std::move(text), "#PLAYERNAME#", std::move(copiedName));
                text = replaceWideTokenFromString(std::move(text), "#PLAYERCOLOR#",
                                                  playerColorDigits(local, killer, killer->getTeam()));
                text = replaceWideInputKeys(std::move(text));
                addServerChat(std::move(text), team);
            }
            IObjectTemplate* vehicleTemplate = objectTemplateManager->getTemplate(eventValue<unsigned int>(data, 8));
            IObjectTemplate* weaponTemplate = nullptr;
            if (vehicleTemplate) {
                weaponTemplate = objectTemplateManager->getTemplate(eventValue<unsigned int>(data, 12));
                if (weaponTemplate) {
                    weaponTemplate->queryImplementation(CID_GenericFireArmTemplate);
                    if (vehicleTemplate->getClassID() == CID_SoldierTemplate ||
                        vehicleTemplate->getClassID() == CID_LadderContainerTemplate ||
                        vehicleTemplate->getClassID() == CID_ParachuteTemplate) {
                        auto* component = weaponTemplate->queryComponent(IID_IWeaponHudCompTemplate);
                        if (component) weaponName = static_cast<WeaponHudCompTemplate*>(component)->getHudName();
                        else missingWeapon = true;
                    }
                }
                if (weaponName.empty() && !missingWeapon) {
                    auto* component = vehicleTemplate->queryComponent(IID_IVehicleHudCompTemplate);
                    if (component) weaponName = static_cast<VehicleHudCompTemplate*>(component)->getHudName();
                    else if (weaponTemplate) {
                        component = weaponTemplate->queryComponent(IID_IWeaponHudCompTemplate);
                        if (component) weaponName = static_cast<WeaponHudCompTemplate*>(component)->getHudName();
                    }
                }
            }
            if (weaponName.empty() && !vehicleTemplate && killer->getVehicle() && killer->getVehicle()->getTemplate()) {
                // The object exposes a const template, but retail dispatches
                // the template's non-const component query on this path.
                auto* component = const_cast<IObjectTemplate*>(killer->getVehicle()->getTemplate())->queryComponent(IID_IVehicleHudCompTemplate);
                if (component) weaponName = static_cast<VehicleHudCompTemplate*>(component)->getHudName();
            }
            if (!victim) return;
            const int victimTeam = victim->getTeam();
            const char* key = victimTeam != killer->getTeam()
                ? (!weaponName.empty() && !missingWeapon ? "HUD_HUD_KILLEDBY_KILLS" : "HUD_HUD_KILLEDBY_KILLED")
                : "HUD_HUD_KILLEDBY_TEAMKILLS";
            const char* victimName = victim->getName().c_str();
            const char* killerName = killer->getName().c_str();
            const int team = killer->getTeam();
            std::string copiedVictimName(victimName);
            auto weaponText = getLocalizedString(weaponName);
            // [CHOSEN, not recovered] #HEADSHOT# / #VEHICLE# / #DISTANCE# go only where the lexicon line
            // has them; nothing is added otherwise. openbf2.hud.killfeedIcons 1 also swaps a hand weapon's name
            // for its small HUD icon (text stays when the icon is missing); vehicle kills keep the name.
            auto text = getLocalizedString(key);
            text = applyKillTokens(std::move(text), killTexts);
            if (!weaponName.empty() && !missingWeapon) {
                const bool onFoot = vehicleTemplate &&
                    (vehicleTemplate->getClassID() == CID_SoldierTemplate ||
                     vehicleTemplate->getClassID() == CID_LadderContainerTemplate ||
                     vehicleTemplate->getClassID() == CID_ParachuteTemplate);
                if (id == 3 && onFoot && weaponTemplate && killfeed_settings::killfeedIcons() == 1) {
                    auto* component = weaponTemplate->queryComponent(IID_IWeaponHudCompTemplate);
                    const std::wstring icon = component ? inlineIconText(ref2System,
                        static_cast<WeaponHudCompTemplate*>(component)->getAltWeaponIcon()) : std::wstring();
                    if (!icon.empty()) weaponText = icon;
                }
                // The local player's own headshot on an enemy (every client sees every kill line).
                if (!personalTexts.headshot.empty() && local && local->getId() == killer->getId()
                    && victim->getTeam() != local->getTeam())
                    playKillfeedHeadshotSound();
            }
            std::string copiedKillerName(killerName);
            text = replaceWideTokenFromString(std::move(text), "#PLAYERNAME1#", std::move(copiedKillerName));
            text = replaceWideToken(std::move(text), "#WEAPON#", std::move(weaponText));
            text = replaceWideTokenFromString(std::move(text), "#PLAYERNAME2#", std::move(copiedVictimName));
            text = replaceWideTokenFromString(std::move(text), "#PLAYERCOLOR1#",
                                              playerColorDigits(local, killer, team));
            text = replaceWideTokenFromString(std::move(text), "#PLAYERCOLOR2#",
                                              playerColorDigits(local, victim, victimTeam));
            addTeamChat(std::move(text), killer, team);
        } else {
            if (!victim) return;
            weaponName = "HUD_TEXT_COMMANDER_ARTILLERY";
            const unsigned char code = eventValue<unsigned char>(data);
            if (code == 0xFD || code == 0xFE) {
                int team;
                const char* name;
                if (code == 0xFD) {
                    team = victim->getTeam() == 1 ? 2 : 1;
                    name = victim->getName().c_str();
                } else {
                    name = victim->getName().c_str();
                    team = victim->getTeam();
                }
                std::string copiedName(name);
                auto weaponText = getLocalizedString(weaponName);
                std::string killerName(" ");
                auto text = getLocalizedString(code == 0xFD ? "HUD_HUD_KILLEDBY_KILLS" : "HUD_HUD_KILLEDBY_TEAMKILLS");
                text = replaceWideTokenFromString(std::move(text), "#PLAYERNAME1#", std::move(killerName));
                text = replaceWideToken(std::move(text), "#WEAPON#", std::move(weaponText));
                text = replaceWideTokenFromString(std::move(text), "#PLAYERNAME2#", std::move(copiedName));
                text = replaceWideTokenFromString(std::move(text), "#PLAYERCOLOR1#",
                                                  teamColorDigits(local, team));
                text = replaceWideTokenFromString(std::move(text), "#PLAYERCOLOR2#",
                                                  playerColorDigits(local, victim, victim->getTeam()));
                addTeamChat(std::move(text), victim, team);
            }
        }
        m_field2ec = victim->getId();
        return;
    }
    case 9: case 10: case 11: case 12: {
        IPlayer* local = playerManager->getLocalHumanPlayer();
        if (!local || (id != 11 && local->getIsCommander())) return;
        const int subtype = eventValue<int>(data);
        const char* key = nullptr;
        if (id == 9) {
            switch (subtype) {
            case 0: return;
            case 1: key = "HUD_HUD_ASSIST_KILL_TARGETING"; break;
            case 2: key = "HUD_HUD_ASSIST_KILL_KILL_DAMAGE"; break;
            case 3: key = "HUD_HUD_ASSIST_KILL_DRIVER_PASSENGER"; break;
            default: Debug(DEBUGTYPE_6, hudSource, 0x151A, "") << "Unknown Assist-type " << subtype; return;
            }
        } else if (id == 11) {
            const int targetId = eventValue<int>(data, 4);
            const int third = eventValue<int>(data, 8);
            if (targetId == local->getId()) {
                const std::string message("AUTO_MOODGP_friendlyfire");
                game->radioMessage(static_cast<TargetChannel>(14), message, local, third, true, true);
                return;
            }
            switch (subtype) {
            case 0: key = "HUD_HUD_TEAM_DAMAGE_PLAYER"; break;
            case 1: key = "HUD_HUD_TEAM_DAMAGE_VEHICLE"; break;
            default: Debug(DEBUGTYPE_6, hudSource, 0x152E, "") << "Unknown Teamdamage-type " << subtype; return;
            }
        } else if (id == 12) {
            switch (subtype) {
            case 0: key = "HUD_HUD_CONQUEST_FULL_CAPTURE"; break;
            case 1: key = "HUD_HUD_CONQUEST_DEFENDED_FLAG"; break;
            case 2: key = "HUD_HUD_CONQUEST_CAPTURE_ASSIST"; break;
            case 3: key = "HUD_HUD_CONQUEST_NEUTRALIZE"; break;
            case 4: key = "HUD_HUD_CONQUEST_NEUTRALIZE_ASSIST"; break;
            default: Debug(DEBUGTYPE_6, hudSource, 0x1548, "") << "Unknown Conquest-type " << subtype; return;
            }
        } else {
            switch (subtype) {
            case 0: key = "HUD_HUD_REPLENISH_GOT_HEALING_POINT"; break;
            case 1: key = "HUD_HUD_REPLENISH_GOT_REPAIR_POINT"; break;
            case 2: key = "HUD_HUD_REPLENISH_GOT_AMMO_POINT"; break;
            case 3: key = "HUD_HUD_REPLENISH_DRIVER"; break;
            case 4: key = "HUD_HUD_REPLENISH_REVIVE_SCORE"; break;
            case 5: key = "HUD_HUD_DESTROY_STRATEGIC"; break;
            default: Debug(DEBUGTYPE_6, hudSource, 0x1564, "") << "Unknown Replenish-type " << subtype; return;
            }
        }
        const int team = local->getTeam();
        auto text = getLocalizedString(key);
        text = replaceWideInputKeys(std::move(text));
        addServerChat(std::move(text), team);
        return;
    }
    case 4: {
        IPlayer* local = playerManager->getLocalHumanPlayer();
        const int team = eventValue<int>(data);
        if (local && local->getTeam() == team) g_bfSound->playSound(std::string("tickets_low"));
        return;
    }
    case 1: {
        const int status = eventValue<int>(data);
        if (status == 4) {
            showConnectionMessage(getLocalizedString("GAME_MESSAGE_GAME_PAUSED"));
            if (isCommanderShown()) setMode(15, true);
            setMode(0, true);
        } else if (status == 2) {
            IPlayer* local = playerManager->getLocalHumanPlayer();
            if (local) {
                if (isCommanderShown()) setMode(15, true);
                setMode(11, true);
                if (gameLogic) gameLogic->selectSpawnGroup(local, 0);
                local->enableVOHelp(false);
                playRoundResultSound();
            }
        } else if (status == 1) {
            if (m_field324 != 4) {
                resetHudState();
                IPlayer* local = playerManager->getLocalHumanPlayer();
                if (!local || !local->getIsAlive()) setMode(1, true);
            }
            clearConnectionMessage();
        }
        m_field324 = status;
        return;
    }
    default: return;
    }
}
} } }
#endif
