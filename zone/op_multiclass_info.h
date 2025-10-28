#pragma once
#include <cstdint>
#include <vector>

namespace THJ {
#pragma pack(push, 1)
struct McClass { uint8_t class_id; uint8_t level; };
struct McAA    { uint32_t aa_id; uint8_t rank; };
struct McSpell { uint32_t spell_id; uint8_t scribable; };
struct McSkill { uint16_t skill_id; uint16_t value; uint16_t cap; };
struct McDisc  { uint32_t disc_id; uint32_t cooldown_ms; };
struct McAbility { uint32_t ability_id; uint32_t flags; uint32_t cooldown_ms; };

struct MulticlassInfo {
    uint32_t char_id;

    uint8_t  class_count;
    // followed by class_count * McClass

    uint16_t aa_count;
    // followed by aa_count * McAA

    uint16_t spell_count;
    // followed by spell_count * McSpell

    uint16_t skill_count;
    // followed by skill_count * McSkill

    uint16_t disc_count;
    // followed by disc_count * McDisc

    uint16_t ability_count;
    // followed by ability_count * McAbility
};
#pragma pack(pop)
} // namespace THJ