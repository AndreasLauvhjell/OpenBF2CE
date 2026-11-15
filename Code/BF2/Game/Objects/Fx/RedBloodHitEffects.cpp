// [CHOSEN, not recovered] `openbf2.effects.redblood 0/1`: a client-only option that tints the
// bullet-hits-soldier impact puffs red. This is an OpenBF2 extension, not recovered Battlefield 2
// 1.50 behaviour; no retail image defines it. An upstream issue that discusses this approach is
// required before it can be proposed upstream (B2BF/OpenBF2, issue not yet filed).
//
// What it changes: the colours of the sprite particle templates that the vanilla material table
// assigns to bullet and knife hits on soldier materials (Human_bodyarmour, Human_body,
// Human_head, Human_limbs), plus one effect choice: the vanilla Knife/Human_bodyarmour cell uses
// e_bhit_s_cloth, which is shared with cloth and helmet hits and so cannot be tinted, and while
// the option is on that cell plays e_bhit_s_sold_bodyarmor instead. Damage, every other cell,
// textures and every other effect are untouched, and nothing is sent to the server, so the option
// works on retail servers. The textures stay untouched because the smoke atlases they use are
// shared by hundreds of emitters.
//
// How it works:
//   - Setting 1 looks up each template below by name and replaces color1/color2 with the red tones
//     below, first saving the colours the loaded content gave it. Setting 0 restores those saved
//     colours, so a mod keeps its own values. A template that does not exist, or is not a sprite
//     particle family template, is skipped.
//   - The renderer reads the template colours for every draw batch, so live particles change too.
//   - The knife cell is found by material name, so a mod with other material ids still matches;
//     its original effect is saved on 1 and put back on 0.
//   - GameLogic::unload deletes every object template and material cell and the next level load
//     creates them again, so the colours and the knife cell are re-applied when the client posts ECCore 3 at the end of GameClient::load
//     (the server's level load posts the same event). The listener is registered the first time
//     the option is set.
//   - Each apply writes one line to the console and to stderr naming the templates it changed.

#include "../../../Core/ConsoleObjectMacro.h"

#if !DEDICATED

#include "../../../Core/EventManager.h"
#include "../../../Core/Ids.h"
#include "../../../Core/StringUtil.h"
#include "../../../IO/Console/Console.h"
#include "../../../Physics/MMCell.h"
#include "../../../Physics/Material.h"
#include "../../../Physics/MaterialManager.h"
#include "../../../Scene/Object/ObjectTypes/Base/ObjectTemplateManager.h"
#include "../../IEventListener.h"
#include "ParticleSystemTemplates.h"

#include <cstddef>
#include <cstdio>
#include <map>
#include <string>

namespace dice {
namespace hfe {
namespace world {

extern IBase* materialManager;   // Core/Globals.cpp, a MaterialManager

namespace {

// Same list as the other particle template bindings in this folder; ModuleWorldObjTemplFx
// registers it in both images.
const char* const kRedBloodConsoleTag = "world.objTemplFx";

// The sprite emitters of e_bhit_s_sold_bodyarmor, e_bhit_s_sold_limb and e_bhit_s_sold_head.
// Vanilla uses them only for soldier hit cells (see META notes red-blood-hit-effect-analysis).
const char* const kSoldierHitTemplates[] =
{
    "em_bhit_s_sold_bodyarmor_dusbase",
    "em_bhit_s_sold_limb_dustbase",
    "em_bhit_s_sold_limb_dustpuff",
    "em_bhit_s_sold_head_dustbase",
};
const std::size_t kSoldierHitTemplateCount =
    sizeof(kSoldierHitTemplates) / sizeof(kSoldierHitTemplates[0]);

// Red at birth, blending (by each template's own colorBlendGraph) to a darker red at end of life.
// The shader multiplies these with the grey smoke texture and the scene lighting.
const float kRedBirthR = 0.55f, kRedBirthG = 0.02f, kRedBirthB = 0.02f;
const float kRedDeathR = 0.35f, kRedDeathG = 0.0f,  kRedDeathB = 0.0f;

// The knife-to-vest cell: attacker and target material names as materialManagerDefine.con spells
// them (vanilla ids 48 and 23), the effect slot it plays, and the soldier effect it gets instead.
const char* const kKnifeMaterial      = "Knife";
const char* const kBodyArmourMaterial = "Human_bodyarmour";
const int         kKnifeCellEffectSlot = 0;
const char* const kKnifeCellRedEffect = "e_bhit_s_sold_bodyarmor";

struct SavedColours
{
    Vec3 color1;
    Vec3 color2;
};

// [CHOSEN, not recovered] mod/combo default: on (Andreas' choice); openbf2.effects.redblood 0 turns it off.
bool g_redBloodEnabled = true;

// Content colours by template name, captured before the first tint of the current level.
std::map<std::string, SavedColours>& savedColours()
{
    static std::map<std::string, SavedColours> saved;
    return saved;
}

// The knife-to-vest cell's own effect, captured before the first swap of the current level.
bool g_knifeCellSaved = false;
IObjectTemplate* g_knifeCellOriginalEffect = 0;

// Finds a material id by name, case-insensitively like MaterialManager::getMaterialByName.
bool findMaterialId(MaterialManager* materials, const char* name, unsigned int& id)
{
    const std::map<unsigned int, Material*>& all = materials->getMaterialMap();
    for (std::map<unsigned int, Material*>::const_iterator it = all.begin(); it != all.end(); ++it)
    {
        if (it->second && compareIgnoreCase(it->second->getName(), name) == 0)
        {
            id = it->first;
            return true;
        }
    }
    return false;
}

// Swaps or restores the knife-to-vest cell's effect. Returns a short status for the report.
std::string applyKnifeCell()
{
    if (!materialManager)
        return "knife cell: no material manager";
    MaterialManager* materials = static_cast<MaterialManager*>(materialManager);
    unsigned int knife = 0;
    unsigned int armour = 0;
    if (!findMaterialId(materials, kKnifeMaterial, knife)
        || !findMaterialId(materials, kBodyArmourMaterial, armour))
        return "knife cell: material not found";
    IMMCell* cell = materials->getCell(knife, armour);
    if (!cell)
        return "knife cell: not found";

    if (g_redBloodEnabled)
    {
        IObjectTemplate* red = objectTemplateManager
            ? objectTemplateManager->getTemplate(std::string(kKnifeCellRedEffect)) : 0;
        if (!red)
            return std::string("knife cell: ") + kKnifeCellRedEffect + " not found";
        if (!g_knifeCellSaved)
        {
            g_knifeCellOriginalEffect = cell->getEffectTemplate(kKnifeCellEffectSlot, false);
            g_knifeCellSaved = true;
        }
        cell->setEffectTemplate(kKnifeCellEffectSlot, red);
        return std::string("knife cell: ") + kKnifeCellRedEffect;
    }
    if (!g_knifeCellSaved)
        return "knife cell: unchanged";
    cell->setEffectTemplate(kKnifeCellEffectSlot, g_knifeCellOriginalEffect);
    g_knifeCellSaved = false;
    g_knifeCellOriginalEffect = 0;
    return "knife cell: restored";
}

Vec3 makeColour(float r, float g, float b)
{
    Vec3 colour;
    colour.x = r;
    colour.y = g;
    colour.z = b;
    return colour;
}

// Returns the named template when it belongs to a sprite particle family that has colours and a
// renderer descriptor, otherwise null.
SpriteParticleSystemTemplateBase* findColourTemplate(const std::string& name)
{
    if (!objectTemplateManager)
        return 0;
    IObjectTemplate* found = objectTemplateManager->getTemplate(name);
    if (!found)
        return 0;

    SpriteParticleSystemTemplateBase* particle = 0;
    switch (found->getClassID())
    {
    case CID_SpriteParticleSystemTemplate:
        particle = static_cast<SpriteParticleSystemTemplate*>(found);
        break;
    case CID_MeshParticleSystemTemplate:
        particle = static_cast<MeshParticleSystemTemplate*>(found);
        break;
    case CID_NonScreenAlignedParticleSystemTemplate:
        particle = static_cast<NonScreenAlignedParticleSystemTemplate*>(found);
        break;
    default:
        return 0;
    }
    // The colour getters read the renderer descriptor without a null check.
    return particle->hasRenderTemplate() ? particle : 0;
}

void report(const std::string& line)
{
    if (io::mainConsole)
        io::mainConsole->output(line);
}

// Applies the current setting to every listed template that exists now and reports the result.
void applyRedBlood(const char* reason)
{
    std::map<std::string, SavedColours>& saved = savedColours();
    std::string changed;
    std::string missing;
    std::size_t changedCount = 0;

    for (std::size_t i = 0; i < kSoldierHitTemplateCount; ++i)
    {
        const std::string name(kSoldierHitTemplates[i]);
        SpriteParticleSystemTemplateBase* particle = findColourTemplate(name);
        if (!particle)
        {
            missing += missing.empty() ? name : ", " + name;
            continue;
        }

        std::map<std::string, SavedColours>::iterator entry = saved.find(name);
        if (g_redBloodEnabled)
        {
            if (entry == saved.end())
            {
                SavedColours original;
                original.color1 = particle->getColor1();
                original.color2 = particle->getColor2();
                saved[name] = original;
            }
            particle->setColor1(makeColour(kRedBirthR, kRedBirthG, kRedBirthB));
            particle->setColor2(makeColour(kRedDeathR, kRedDeathG, kRedDeathB));
        }
        else
        {
            if (entry == saved.end())
                continue;                                   // never tinted: content colours stand
            particle->setColor1(entry->second.color1);
            particle->setColor2(entry->second.color2);
            saved.erase(entry);
        }
        changed += changed.empty() ? name : ", " + name;
        ++changedCount;
    }

    const std::string knifeStatus = applyKnifeCell();

    char head[160];
    std::sprintf(head, "openbf2.effects.redblood %d (%s): %u of %u templates %s", g_redBloodEnabled ? 1 : 0,
                 reason, static_cast<unsigned int>(changedCount),
                 static_cast<unsigned int>(kSoldierHitTemplateCount),
                 g_redBloodEnabled ? "tinted" : "restored");
    std::string line(head);
    if (!changed.empty())
        line += " [" + changed + "]";
    if (!missing.empty())
        line += "; not found: " + missing;
    line += "; " + knifeStatus;
    report(line);
}

// Re-applies the tint once a level has loaded. Object templates and material cells are recreated
// by every level load, so what was saved for the previous level no longer belongs to them.
class RedBloodLevelListener : public IEventListener
{
public:
    virtual void handleEvent(EventCategory category, unsigned int id, EventNode*, void*)
    {
        if (category != ECCore || id != kLevelLoadedEventId)
            return;
        savedColours().clear();
        g_knifeCellSaved = false;
        g_knifeCellOriginalEffect = 0;
        if (g_redBloodEnabled)
            applyRedBlood("level loaded");
    }

private:
    // GameClient::load and GameServer::loadPath post ECCore 3 after a successful level load.
    static const unsigned int kLevelLoadedEventId = 3;
};

RedBloodLevelListener g_redBloodLevelListener;
bool g_redBloodListenerRegistered = false;

bool getRedBlood()
{
    return g_redBloodEnabled;
}

void setRedBlood(bool enabled)
{
    if (!g_redBloodListenerRegistered && eventManager)
        g_redBloodListenerRegistered =
            eventManager->registerEventHandler(ECCore, &g_redBloodLevelListener, 0);
    g_redBloodEnabled = enabled;
    applyRedBlood("console");
}

} // anonymous namespace

// [CHOSEN, not recovered] mod/combo: registers the level-load listener once, so the default-on
// setting is applied at the first level load without a console call (GameClient::load).
void redBloodEnsureLevelListener()
{
    if (!g_redBloodListenerRegistered && eventManager)
        g_redBloodListenerRegistered =
            eventManager->registerEventHandler(ECCore, &g_redBloodLevelListener, 0);
}

BF2_CONSOLE_OBJECT("openbf2", 1, 0)
BF2_CONSOLE_PROPERTY_FN_NAMED("effects.redblood", bool, true, getRedBlood(), setRedBlood,
                              kRedBloodConsoleTag)

} // namespace world
} // namespace hfe
} // namespace dice

#endif // !DEDICATED
