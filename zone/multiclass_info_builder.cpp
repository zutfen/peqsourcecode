#include "op_multiclass_info.h"
#include "../common/eq_stream.h"
#include "../common/thj_multiclass.h"
#include "../common/packet_functions.h"
#include "client.h"
#include <algorithm>

using namespace THJ;

// helpers you’ll implement against your server model:
static void GatherClasses(Client* c, std::vector<McClass>& out);
static void GatherAAs(Client* c, std::vector<McAA>& out);
static void GatherSpells(Client* c, std::vector<McSpell>& out);
static void GatherSkills(Client* c, std::vector<McSkill>& out);
static void GatherDiscs(Client* c, std::vector<McDisc>& out);
static void GatherAbilities(Client* c, std::vector<McAbility>& out);

static std::vector<uint8_t> Serialize(const MulticlassInfo& hdr,
                                      const std::vector<McClass>& classes,
                                      const std::vector<McAA>& aas,
                                      const std::vector<McSpell>& spells,
                                      const std::vector<McSkill>& skills,
                                      const std::vector<McDisc>& discs,
                                      const std::vector<McAbility>& abilities)
{
    std::vector<uint8_t> buf;
    buf.reserve(64 * 1024);

    auto push = [&](auto const* p, size_t n){
        const uint8_t* b = reinterpret_cast<const uint8_t*>(p);
        buf.insert(buf.end(), b, b + n);
    };

    MulticlassInfo h = hdr;
    h.class_count  = static_cast<uint8_t>(std::min<size_t>(classes.size(), 255));
    h.aa_count     = static_cast<uint16_t>(std::min<size_t>(aas.size(), 65535));
    h.spell_count  = static_cast<uint16_t>(std::min<size_t>(spells.size(), 65535));
    h.skill_count  = static_cast<uint16_t>(std::min<size_t>(skills.size(), 65535));
    h.disc_count   = static_cast<uint16_t>(std::min<size_t>(discs.size(), 65535));
    h.ability_count= static_cast<uint16_t>(std::min<size_t>(abilities.size(), 65535));

    push(&h, sizeof(h));
    if (!classes.empty())  push(classes.data(),  h.class_count  * sizeof(McClass));
    if (!aas.empty())      push(aas.data(),      h.aa_count     * sizeof(McAA));
    if (!spells.empty())   push(spells.data(),   h.spell_count  * sizeof(McSpell));
    if (!skills.empty())   push(skills.data(),   h.skill_count  * sizeof(McSkill));
    if (!discs.empty())    push(discs.data(),    h.disc_count   * sizeof(McDisc));
    if (!abilities.empty())push(abilities.data(),h.ability_count* sizeof(McAbility));
    return buf;
}

static void BuildAndSendMulticlassInfo(Client* c) {
    if (!c) return;

    std::vector<McClass>  classes;
    std::vector<McAA>     aas;
    std::vector<McSpell>  spells;
    std::vector<McSkill>  skills;
    std::vector<McDisc>   discs;
    std::vector<McAbility>abilities;

    GatherClasses(c, classes);
    GatherAAs(c, aas);
    GatherSpells(c, spells);
    GatherSkills(c, skills);
    GatherDiscs(c, discs);
    GatherAbilities(c, abilities);

    MulticlassInfo hdr{};
    hdr.char_id = c->CharacterID();

    uint32 class_mask = 0;
    for (auto& mc : classes) {
        if (mc.class_id >= 1 && mc.class_id <= 16) {
            class_mask |= (1u << (mc.class_id - 1));
        }
    }
    LogInfo("[THJ] SendMulticlassInfo mask=0x{:08X} classes={} aas={} spells={} skills={} discs={} abilities={}",
            class_mask,
            classes.size(),
            aas.size(),
            spells.size(),
            skills.size(),
            discs.size(),
            abilities.size());

    THJ::SetMulticlassMask(c->CharacterID(), class_mask);
    c->SetBucket("GestaltClasses", std::to_string(class_mask));

    auto bytes = Serialize(hdr, classes, aas, spells, skills, discs, abilities);

    // Queue as a single packet:
    auto out = new EQApplicationPacket(OP_MulticlassInfo, bytes.size());
    memcpy(out->pBuffer, bytes.data(), bytes.size());
    c->FastQueuePacket(&out);
}

void Client::SendMulticlassInfo()
{
    // Send simple uint32_t mask packet for client multiclass handler
    // Client expects: opcode 0x7F01 with just the 4-byte class mask
    uint32_t mask = GetClassesMask();

    if (!mask) {
        // Fallback to current class if no mask is set
        mask = GetPlayerClassBit(GetClass());
    }

    LogInfo("[THJ] SendMulticlassInfo char_id={} mask=0x{:08X}",
            CharacterID(), mask);

    // Create packet with just the uint32_t mask
    auto outapp = new EQApplicationPacket(OP_MulticlassInfo, sizeof(uint32_t));
    *(uint32_t*)outapp->pBuffer = mask;

    FastQueuePacket(&outapp);
}
static void GatherClasses(Client* c, std::vector<McClass>& out)
{
    out.clear();
    if (!c)
        return;

    out.reserve(3);
    c->ForEachClass([&](uint8 cls) {
        if (!cls)
            return;
        McClass entry{};
        entry.class_id = cls;
        entry.level = static_cast<uint8>(std::min<uint8>(c->GetLevel(), 255));
        out.push_back(entry);
    });
}

static void GatherAAs(Client* c, std::vector<McAA>& out)
{
    out.clear();
    (void)c;
}

static void GatherSpells(Client* c, std::vector<McSpell>& out)
{
    out.clear();
    (void)c;
}

static void GatherSkills(Client* c, std::vector<McSkill>& out)
{
    out.clear();
    (void)c;
}

static void GatherDiscs(Client* c, std::vector<McDisc>& out)
{
    out.clear();
    (void)c;
}

static void GatherAbilities(Client* c, std::vector<McAbility>& out)
{
    out.clear();
    (void)c;
}
