#ifndef PETS_H
#define PETS_H

#include "npc.h"
#include "../common/timer.h"
#include <vector>
#include <cstring> // memset

class Mob;
struct NPCType;

namespace EQ {
    class ItemData;
    class ItemInstance;
}

// Structure to hold pet's virtual equipment from gear bag
struct PetVirtualGear {
    uint32 item_id[EQ::invslot::EQUIPMENT_COUNT];  // Track which items are equipped in which slots

    // Accumulated stat bonuses from all equipped items
    int32 ac_bonus;
    int32 hp_bonus;
    int32 mana_bonus;
    int32 endurance_bonus;

    // Primary stats
    int32 str_bonus;
    int32 sta_bonus;
    int32 agi_bonus;
    int32 dex_bonus;
    int32 wis_bonus;
    int32 int_bonus;
    int32 cha_bonus;

    // Combat stats
    int32 attack_bonus;
    int32 haste_bonus;
    int32 accuracy_bonus;
    int32 avoidance_bonus;

    // Resistances
    int32 mr_bonus;
    int32 fr_bonus;
    int32 cr_bonus;
    int32 pr_bonus;
    int32 dr_bonus;
    int32 corruption_bonus;

    // Weapon data for damage calculation
    struct WeaponData {
        uint32 item_id;
        uint8  damage;
        uint8  delay;
        uint8  skill;
        uint32 proc_spell_id;
        int16  proc_rate;

        WeaponData()
            : item_id(0), damage(0), delay(0), skill(0), proc_spell_id(0), proc_rate(0) {}
    };

    WeaponData primary_weapon;
    WeaponData secondary_weapon;
    WeaponData ranged_weapon;

    // Proc tracking
    std::vector<uint32> weapon_proc_spells;
    std::vector<int16>  weapon_proc_rates;

    // --- Change detection / throttling caches ---
    uint64 last_bag_sig;     // signature of current pet bag contents
    uint16 last_prim_model;  // last pushed primary weapon model
    uint16 last_sec_model;   // last pushed secondary weapon model

    Timer rescan_timer;

    PetVirtualGear() {
        std::memset(item_id, 0, sizeof(item_id));
        ac_bonus = 0;
        hp_bonus = 0;
        mana_bonus = 0;
        endurance_bonus = 0;
        str_bonus = sta_bonus = agi_bonus = dex_bonus = 0;
        wis_bonus = int_bonus = cha_bonus = 0;
        attack_bonus = 0;
        haste_bonus = 0;
        accuracy_bonus = 0;
        avoidance_bonus = 0;
        mr_bonus = fr_bonus = cr_bonus = pr_bonus = dr_bonus = corruption_bonus = 0;
        last_bag_sig   = 0;
        last_prim_model = 0;
        last_sec_model  = 0;
        rescan_timer.Disable();
    }

    void Reset() {
        std::memset(item_id, 0, sizeof(item_id));
        ac_bonus = 0;
        hp_bonus = 0;
        mana_bonus = 0;
        endurance_bonus = 0;
        str_bonus = sta_bonus = agi_bonus = dex_bonus = 0;
        wis_bonus = int_bonus = cha_bonus = 0;
        attack_bonus = 0;
        haste_bonus = 0;
        accuracy_bonus = 0;
        avoidance_bonus = 0;
        mr_bonus = fr_bonus = cr_bonus = pr_bonus = dr_bonus = corruption_bonus = 0;
        primary_weapon = WeaponData();
        secondary_weapon = WeaponData();
        ranged_weapon = WeaponData();
        weapon_proc_spells.clear();
        weapon_proc_rates.clear();
        // Intentionally keep last_bag_sig / last_*_model to avoid thrashing visuals on Reset()
    }
};

class Pet : public NPC {
public:
    Pet(NPCType *type_data, Mob *owner, uint8 pet_type, uint16 spell_id, int16 power);

    virtual bool CheckSpellLevelRestriction(Mob *caster, uint16 spell_id);

    // -----------------------------
    // Pet Gear Bag System (public)
    // -----------------------------
    void ScanOwnerForPetGear();
    void ApplyPetGearStats();
    void ClearPetGearStats();
    bool GetItemsFromPetGearBag(std::vector<const EQ::ItemData*>& items);

	// Class tracking for pet bags
	void SetSummonerClass(uint8 c) { m_summoner_class = c; }
    uint8 GetSummonerClass() const { return m_summoner_class; }

    // Rescan controls
    void StartPetGearRescan(int seconds) {
        if (seconds <= 0) { m_virtual_gear.rescan_timer.Disable(); return; }
        m_virtual_gear.rescan_timer.Start(seconds * 1000);
    }
    void StopPetGearRescan() { m_virtual_gear.rescan_timer.Disable(); }
    void TriggerPetGearRescan() {
        if (m_virtual_gear.rescan_timer.Enabled()) m_virtual_gear.rescan_timer.Trigger();
        else ScanOwnerForPetGear();
    }
    // Signature-gated periodic rescan (only heavy-scan when bag changes)
    void ProcessPetGearRescan() {
        if (!m_virtual_gear.rescan_timer.Check())
            return;

        uint64 sig = 0;
        bool have_items = ComputePetBagSignature(sig);
        if (!have_items) sig = 0; // normalize "no items" to deterministic signature

        if (sig != m_virtual_gear.last_bag_sig) {
            m_virtual_gear.last_bag_sig = sig;
            ScanOwnerForPetGear();
        } else {
            // no change -> skip heavy work
        }
    }

    // Actually make the pet display weapons
    void   UpdatePetWeaponAppearance();
    static uint16 WeaponModelFromItem(const EQ::ItemData* it);

    // Pet item update process override
    bool Process() override;

    // ---- Access to the full gear structure
    const PetVirtualGear& GetVirtualGear() const { return m_virtual_gear; }
    PetVirtualGear&       MutableVirtualGear()    { return m_virtual_gear; }

    // -----------------------------
    // Gear/stat accessors for combat
    // -----------------------------
    inline int32 GetPetGearAC()        const { return m_virtual_gear.ac_bonus; }
    inline int32 GetPetGearHP()        const { return m_virtual_gear.hp_bonus; }
    inline int32 GetPetGearMana()      const { return m_virtual_gear.mana_bonus; }
    inline int32 GetPetGearSTR()       const { return m_virtual_gear.str_bonus; }
    inline int32 GetPetGearSTA()       const { return m_virtual_gear.sta_bonus; }
    inline int32 GetPetGearAGI()       const { return m_virtual_gear.agi_bonus; }
    inline int32 GetPetGearDEX()       const { return m_virtual_gear.dex_bonus; }
    inline int32 GetPetGearWIS()       const { return m_virtual_gear.wis_bonus; }
    inline int32 GetPetGearINT()       const { return m_virtual_gear.int_bonus; }
    inline int32 GetPetGearCHA()       const { return m_virtual_gear.cha_bonus; }
    inline int32 GetPetGearATK()       const { return m_virtual_gear.attack_bonus; }
    inline int32 GetPetGearHaste()     const { return m_virtual_gear.haste_bonus; }
    inline int32 GetPetGearAccuracy()  const { return m_virtual_gear.accuracy_bonus; }
    inline int32 GetPetGearAvoidance() const { return m_virtual_gear.avoidance_bonus; }

    // Resistances
    inline int32 GetPetGearMR()         const { return m_virtual_gear.mr_bonus; }
    inline int32 GetPetGearFR()         const { return m_virtual_gear.fr_bonus; }
    inline int32 GetPetGearCR()         const { return m_virtual_gear.cr_bonus; }
    inline int32 GetPetGearPR()         const { return m_virtual_gear.pr_bonus; }
    inline int32 GetPetGearDR()         const { return m_virtual_gear.dr_bonus; }
    inline int32 GetPetGearCorruption() const { return m_virtual_gear.corruption_bonus; }

    // Weapon accessors (used by damage scaling)
    const PetVirtualGear::WeaponData* GetPetPrimaryWeapon() const {
        return (m_virtual_gear.primary_weapon.item_id > 0) ? &m_virtual_gear.primary_weapon : nullptr;
    }
    const PetVirtualGear::WeaponData* GetPetSecondaryWeapon() const {
        return (m_virtual_gear.secondary_weapon.item_id > 0) ? &m_virtual_gear.secondary_weapon : nullptr;
    }
    const PetVirtualGear::WeaponData* GetPetRangedWeapon() const {
        return (m_virtual_gear.ranged_weapon.item_id > 0) ? &m_virtual_gear.ranged_weapon : nullptr;
    }

    // Proc system (read-only)
    bool HasPetWeaponProcs() const { return !m_virtual_gear.weapon_proc_spells.empty(); }
    const std::vector<uint32>& GetPetWeaponProcSpells() const { return m_virtual_gear.weapon_proc_spells; }
    const std::vector<int16>&  GetPetWeaponProcRates()  const { return m_virtual_gear.weapon_proc_rates; }

    // High-level checks
    bool HasPetGearEquipped() const {
        for (int i = EQ::invslot::EQUIPMENT_BEGIN; i <= EQ::invslot::EQUIPMENT_END; ++i) {
            if (m_virtual_gear.item_id[i] != 0) return true;
        }
        return false;
    }

    // Equipped item id per slot
    uint32 GetPetGearItemID(int16 slot) const {
        if (slot >= EQ::invslot::EQUIPMENT_BEGIN && slot <= EQ::invslot::EQUIPMENT_END) {
            return m_virtual_gear.item_id[slot];
        }
        return 0;
    }

    // Pick-first-free for paired slots (ears, rings, wrists)
    int16 PickFirstFree(int16 s1, int16 s2) const {
        if (s1 < EQ::invslot::EQUIPMENT_BEGIN || s1 > EQ::invslot::EQUIPMENT_END) return s1;
        if (s2 < EQ::invslot::EQUIPMENT_BEGIN || s2 > EQ::invslot::EQUIPMENT_END) return s1;
        if (m_virtual_gear.item_id[s1] == 0) return s1;
        if (m_virtual_gear.item_id[s2] == 0) return s2;
        return s1;
    }

    // Convenience mutators
    void PetVirtualSetItem(int16 slot, uint32 item_id) {
        if (slot >= EQ::invslot::EQUIPMENT_BEGIN && slot <= EQ::invslot::EQUIPMENT_END)
            m_virtual_gear.item_id[slot] = item_id;
    }
    void PetVirtualClearSlot(int16 slot) {
        if (slot >= EQ::invslot::EQUIPMENT_BEGIN && slot <= EQ::invslot::EQUIPMENT_END)
            m_virtual_gear.item_id[slot] = 0;
    }
    void PetVirtualAddProc(uint32 spell_id, int16 rate) {
        if (spell_id > 0) {
            m_virtual_gear.weapon_proc_spells.push_back(spell_id);
            m_virtual_gear.weapon_proc_rates.push_back(rate);
        }
    }

private:
	uint8 m_summoner_class = 0;

protected:
    // Virtual equipment & derived bonuses live here; mutate via helpers above
    PetVirtualGear m_virtual_gear;

	// Used to resolve pet bag lore groups for differing classes
	int32 ResolvePetBagLoregroup();

	// Helpers used by scan/apply paths
    void ProcessItemForPetGear(const EQ::ItemData* item);
    void AccumulateItemStats(const EQ::ItemData* item);
    void ProcessWeaponForPet(const EQ::ItemData* item, int virtual_slot);
    int  DetermineVirtualSlot(const EQ::ItemData* item);

    // Apply virtual weapon ratios to pet min/max using a non-stacking baseline
    void ApplyVirtualWeaponDamageBonus(int base_min, int base_max);

    // --- Fast bag-change detector (implemented in pets.cpp) ---
    bool ComputePetBagSignature(uint64 &out_sig);
    static inline void SigMix(uint64 &h, uint64 v) {
        // lightweight 64-bit mix; good enough for change detection
        h ^= v + 0x9e3779b97f4a7c15ULL + (h << 6) + (h >> 2);
    }
};

#endif // PETS_H
