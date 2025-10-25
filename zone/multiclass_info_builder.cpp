#include "op_multiclass_info.h"
#include "../common/eq_stream.h"
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

void SendMulticlassInfo(Client* c) {
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

    auto bytes = Serialize(hdr, classes, aas, spells, skills, discs, abilities);

    // Queue as a single packet:
    auto out = new EQApplicationPacket(OP_MULTICLASS_INFO, bytes.size());
    memcpy(out->pBuffer, bytes.data(), bytes.size());
    c->FastQueuePacket(&out);
}
