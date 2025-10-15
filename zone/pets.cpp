/*  EQEMu: Everquest Server Emulator
    Copyright (C) 2001-2004 EQEMu Development Team (http://eqemu.org)
    GNU GPL v2
*/
#include "../common/rulesys.h"   // RuleB / RuleI / RuleR
#include "../common/global_define.h"
#include "../common/spdat.h"
#include "../common/strings.h"
#include "../common/emu_constants.h"

#include <vector>
#include <string>
#include <algorithm>  // std::max
#include <cmath>      // std::round
#include <cstdlib>    // atoi
#include <unordered_map> // baseline cache

#include "../common/repositories/pets_repository.h"
#include "../common/repositories/pets_beastlord_data_repository.h"
#include "../common/repositories/character_pet_name_repository.h"

#include "entity.h"
#include "client.h"
#include "mob.h"
#include "pets.h"
#include "zonedb.h"
#include "bot.h"
#include <sstream>

#ifndef WIN32
#include <stdlib.h>
#include "../common/unix.h"
#endif

// ------------------------------
// helpers
// ------------------------------


#include <sstream> // add this include

namespace {
    // Map class id -> 3-letter key used in the CSV rule
    inline const char* ClassAbbr(uint8 c)
    {
        switch (c) {
            case Class::Magician:     return "MAG";
            case Class::Necromancer:  return "NEC";
            case Class::Beastlord:    return "BST";
            case Class::Enchanter:    return "ENC";
            case Class::ShadowKnight: return "SHD";
            case Class::Shaman:       return "SHM";
            case Class::Bard:         return "BRD";
            default:                  return "";
        }
    }

    // CSV format example:
    // Pets:PetGearBagLoregroupsCSV = MAG:15071,NEC:15072,BST:15073,ENC:15074,SHD:15075,SHM:15076,BRD:15077;FALLBACK=15075;SCAN_BANK=1
    //
    // - Left side (before ';') is comma-separated class:loregroup pairs.
    // - Right side (after ';') takes optional K=V;K=V items (FALLBACK, SCAN_BANK).
    //
    // Output:
    //   out_loregroups         -> 1+ loregroups the current pet is allowed to use
    //   out_scan_bank_override -> true if SCAN_BANK was specified
    //   out_scan_bank_value    -> value of SCAN_BANK when override=true
    static void ResolvePetLoregroups(uint8 summoner_class,
                                     std::vector<int32>& out_loregroups,
                                     bool& out_scan_bank_override,
                                     bool& out_scan_bank_value)
    {
        out_loregroups.clear();
        out_scan_bank_override = false;
        out_scan_bank_value = false;

        const int legacy = RuleI(Pets, PetGearBagLoregroup); // legacy single-loregroup fallback
        const std::string csv = RuleS(Pets, PetGearBagLoregroupsCSV); // may be empty

        int fallback_val = 0;
        const std::string want = ClassAbbr(summoner_class);

        if (!csv.empty()) {
            // Split once on ';' -> left: pairs, right: K=V list (optional)
            std::string left_pairs = csv;
            std::string right_kv;
            if (auto pos = csv.find(';'); pos != std::string::npos) {
                left_pairs = csv.substr(0, pos);
                right_kv   = csv.substr(pos + 1);
            }

            // Parse left side: "MAG:15071,NEC:15072,..."
            {
                std::stringstream ss(left_pairs);
                std::string token;
                while (std::getline(ss, token, ',')) {
                    auto colon = token.find(':');
                    if (colon == std::string::npos) continue;
                    std::string key = token.substr(0, colon);
					Strings::Trim(key);
					Strings::ToUpper(key);
                    std::string val = token.substr(colon + 1);
					Strings::Trim(val);
                    int32 lg = std::atoi(val.c_str());
                    if (!want.empty() && key == want && lg > 0) {
                        out_loregroups.push_back(lg);
                    }
                }
            }

            // Parse right side: "FALLBACK=15075;SCAN_BANK=1"
            if (!right_kv.empty()) {
                std::stringstream ss(right_kv);
                std::string kv;
                while (std::getline(ss, kv, ';')) {
                    auto eq = kv.find('=');
                    if (eq == std::string::npos) continue;
                    std::string k = kv.substr(0, eq);
					Strings::Trim(k);
					Strings::ToUpper(k);
                    std::string v = kv.substr(eq + 1);
					Strings::Trim(v);
                    if (k == "FALLBACK") {
                        fallback_val = std::atoi(v.c_str());
                    } else if (k == "SCAN_BANK") {
                        out_scan_bank_override = true;
                        std::string vl = Strings::ToLower(v);
                        out_scan_bank_value = (vl == "1" || vl == "true" || vl == "yes");
                    }
                }
            }
        }

        // If nothing specific resolved for this class, prefer CSV FALLBACK, else legacy int rule
        if (out_loregroups.empty()) {
            if (fallback_val > 0) {
                out_loregroups.push_back(fallback_val);
            } else if (legacy > 0) {
                out_loregroups.push_back(legacy);
            }
        }
    }
} // anonymous namespace


// need to pass in a char array of 64 chars
void GetRandPetName(char *name)
{
    std::string temp;
    temp.reserve(64);
    // note these orders are used to make the exclusions cheap :P
    static const char *part1[] = {"G", "J", "K", "L", "V", "X", "Z"};
    static const char *part2[] = {nullptr, "ab", "ar", "as", "eb", "en", "ib", "ob", "on"};
    static const char *part3[] = {nullptr, "an", "ar", "ek", "ob"};
    static const char *part4[] = {"er", "ab", "n", "tik"};

    const char *first = part1[zone->random.Int(0, (sizeof(part1) / sizeof(const char *)) - 1)];
    const char *second = part2[zone->random.Int(0, (sizeof(part2) / sizeof(const char *)) - 1)];
    const char *third  = part3[zone->random.Int(0, (sizeof(part3) / sizeof(const char *)) - 1)];
    const char *fourth = part4[zone->random.Int(0, (sizeof(part4) / sizeof(const char *)) - 1)];

    // if both of these are empty, we would get an illegally short name
    if (second == nullptr && third == nullptr)
        fourth = part4[(sizeof(part4) / sizeof(const char *)) - 1];

    // "ektik" isn't allowed either I guess?
    if (third == part3[3] && fourth == part4[3])
        fourth = part4[zone->random.Int(0, (sizeof(part4) / sizeof(const char *)) - 2)];

    // "Laser" isn't allowed either I guess?
    if (first == part1[3] && second == part2[3] && third == nullptr && fourth == part4[0])
        fourth = part4[zone->random.Int(1, (sizeof(part4) / sizeof(const char *)) - 2)];

    temp += first;
    if (second != nullptr) temp += second;
    if (third  != nullptr) temp += third;
    temp += fourth;

    strn0cpy(name, temp.c_str(), 64);
}

// ------------------------------
// baseline (no-creep) cache for virtual weapon scaling
// ------------------------------
static std::unordered_map<const Pet*, std::pair<int,int>> g_pet_base_dmg;

// ------------------------------
// Mob -> pet creation wrappers
// ------------------------------
void Mob::MakePet(uint16 spell_id, const char* pettype, const char *petname) {
    // petpower of -1 is used to get the petpower based on whichever focus is currently
    // equipped. This should replicate the old functionality for the most part.
    MakePoweredPet(spell_id, pettype, -1, petname);
}

// Ensure our rescan timer ticks each frame
bool Pet::Process()
{
    if (RuleB(Pets, UsePetGearBag)) {
        ProcessPetGearRescan();
    }
    return NPC::Process();
}

// Split from the basic MakePet to allow backward compatiblity with existing code while also
// making it possible for petpower to be retained without the focus item having to
// stay equipped when the character zones. petpower of -1 means that the currently equipped petfocus
// of a client is searched for and used instead.
void Mob::MakePoweredPet(uint16 spell_id, const char* pettype, int16 petpower,
        const char *petname, float in_size) {
    // Sanity and early out checking first.
    if(HasPet() || pettype == nullptr)
        return;

    int16 act_power = 0; // The actual pet power we'll use.
    if (petpower == -1) {
        if (IsClient()) {
            act_power = CastToClient()->GetFocusEffect(focusPetPower, spell_id);//Client only
        }
        else if (IsBot())
            act_power = CastToBot()->GetFocusEffect(focusPetPower, spell_id);
    }
    else if (petpower > 0)
        act_power = petpower;

    //lookup our pets table record for this type
    PetRecord record;
    if(!content_db.GetPoweredPetEntry(pettype, act_power, &record)) {
        Message(Chat::Red, "Unable to find data for pet %s", pettype);
        LogError("Unable to find data for pet [{}], check pets table", pettype);
        return;
    }

    //find the NPC data for the specified NPC type
    const NPCType *base = content_db.LoadNPCTypesData(record.npc_type);
    if(base == nullptr) {
        Message(Chat::Red, "Unable to load NPC data for pet %s", pettype);
        LogError("Unable to load NPC data for pet [{}] (NPC ID [{}]), check pets and npc_types tables", pettype, record.npc_type);
        return;
    }

    //we copy the npc_type data because we need to edit it a bit
    auto npc_type = new NPCType;
    memcpy(npc_type, base, sizeof(NPCType));

    // If pet power is set to -1 in the DB, use stat scaling
    if ((IsClient() || IsBot()) && record.petpower == -1)
    {
        float scale_power = (float)act_power / 100.0f;
        if(scale_power > 0)
        {
            npc_type->max_hp *= (1 + scale_power);
            npc_type->current_hp = npc_type->max_hp;
            npc_type->AC *= (1 + scale_power);
            npc_type->level += 1 + ((int)act_power / 25) > npc_type->level + RuleR(Pets, PetPowerLevelCap) ? RuleR(Pets, PetPowerLevelCap) : 1 + ((int)act_power / 25); // gains an additional level for every 25 pet power
            npc_type->min_dmg = (npc_type->min_dmg * (1 + (scale_power / 2)));
            npc_type->max_dmg = (npc_type->max_dmg * (1 + (scale_power / 2)));
            npc_type->size = npc_type->size * (1 + (scale_power / 2)) > npc_type->size * 3 ? npc_type->size * 3 : npc_type-> size * (1 + (scale_power / 2));
        }
        record.petpower = act_power;
    }

    //Live AA - Elemental Durability
    int64 MaxHP = aabonuses.PetMaxHP + itembonuses.PetMaxHP + spellbonuses.PetMaxHP;

    if (MaxHP){
        npc_type->max_hp += (npc_type->max_hp*MaxHP)/100;
        npc_type->current_hp = npc_type->max_hp;
    }

    // Pet naming rules
    const auto vanity_name = (IsClient() && !petname) ? CharacterPetNameRepository::FindOne(database, CastToClient()->CharacterID()) : CharacterPetNameRepository::CharacterPetName{};

    if (IsClient() && !petname && !vanity_name.name.empty()) {
        petname = vanity_name.name.c_str();
    }

    if (petname != nullptr) {
        // Name was provided, use it.
        strn0cpy(npc_type->name, petname, 64);
        EntityList::RemoveNumbers(npc_type->name);
        entity_list.MakeNameUnique(npc_type->name);
    } else if (record.petnaming == 0) {
        strcpy(npc_type->name, GetCleanName());
        npc_type->name[25] = '\0';
        strcat(npc_type->name, "`s_pet");
    } else if (record.petnaming == 1) {
        strcpy(npc_type->name, GetName());
        npc_type->name[19] = '\0';
        strcat(npc_type->name, "`s_familiar");
    } else if (record.petnaming == 2) {
        strcpy(npc_type->name, GetName());
        npc_type->name[21] = 0;
        strcat(npc_type->name, "`s_Warder");
    } else if (record.petnaming == 4) {
        // Keep the DB name
    } else if (record.petnaming == 3 && IsClient()) {
        GetRandPetName(npc_type->name);
    } else if (record.petnaming == 5 && IsClient()) {
        strcpy(npc_type->name, GetName());
        npc_type->name[24] = '\0';
        strcat(npc_type->name, "`s_ward");
    } else {
        strcpy(npc_type->name, GetCleanName());
        npc_type->name[25] = '\0';
        strcat(npc_type->name, "`s_pet");
    }

    // Beastlord Pets
    if (record.petnaming == 2) {
        uint16 race_id = GetBaseRace();

        auto d = content_db.GetBeastlordPetData(race_id);

        npc_type->race        = d.race_id;
        npc_type->texture     = d.texture;
        npc_type->helmtexture = d.helm_texture;
        npc_type->gender      = d.gender;
        npc_type->luclinface  = d.face;

        npc_type->size *= d.size_modifier;
    }

    // handle monster summoning pet appearance
    if(record.monsterflag) {

        uint32 monsterid = 0;

        // get a random npc id from the spawngroups assigned to this zone
        auto query = StringFormat("SELECT npcID "
                                    "FROM (spawnentry INNER JOIN spawn2 ON spawn2.spawngroupID = spawnentry.spawngroupID) "
                                    "INNER JOIN npc_types ON npc_types.id = spawnentry.npcID "
                                    "WHERE spawn2.zone = '%s' AND npc_types.bodytype NOT IN (11, 33, 66, 67) "
                                    "AND npc_types.race NOT IN (0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 44, "
                                    "55, 67, 71, 72, 73, 77, 78, 81, 90, 92, 93, 94, 106, 112, 114, 127, 128, "
                                    "130, 139, 141, 183, 236, 237, 238, 239, 254, 266, 329, 330, 378, 379, "
                                    "380, 381, 382, 383, 404, 522) "
                                    "ORDER BY RAND() LIMIT 1", zone->GetShortName());
        auto results = content_db.QueryDatabase(query);
        if (!results.Success()) {
            safe_delete(npc_type);
            return;
        }

        if (results.RowCount() != 0) {
            auto row = results.begin();
            monsterid = Strings::ToInt(row[0]);
        }

        // since we don't have any monsters, just make it look like an earth pet for now
        if (monsterid == 0)
            monsterid = 567;

        // give the summoned pet the attributes of the monster we found
        const NPCType* monster = content_db.LoadNPCTypesData(monsterid);
        if(monster) {
            npc_type->race = monster->race;
            npc_type->size = monster->size;
            npc_type->texture = monster->texture;
            npc_type->gender = monster->gender;
            npc_type->luclinface = monster->luclinface;
            npc_type->helmtexture = monster->helmtexture;
            npc_type->herosforgemodel = monster->herosforgemodel;
        } else
            LogError("Error loading NPC data for monster summoning pet (NPC ID [{}])", monsterid);

    }

    //this takes ownership of the npc_type data
    auto npc = new Pet(npc_type, this, record.petcontrol, spell_id, record.petpower);
		npc->SetSummonerClass(
  	  	IsClient() ? CastToClient()->GetClass() :
    	IsBot()    ? CastToBot()->GetClass()    : 0
);

    // Now that we have an actual object to interact with, load
    // the base items for the pet.
    uint32 petinv[EQ::invslot::EQUIPMENT_COUNT];
    memset(petinv, 0, sizeof(petinv));
    const EQ::ItemData *item = nullptr;

    if (content_db.GetBasePetItems(record.equipmentset, petinv)) {
        for (int i = EQ::invslot::EQUIPMENT_BEGIN; i <= EQ::invslot::EQUIPMENT_END; i++)
            if (petinv[i]) {
                item = database.GetItem(petinv[i]);
                npc->AddLootDrop(item, LootdropEntriesRepository::NewNpcEntity(), true);
            }
    }

    npc->UpdateEquipmentLight();

    // finally, override size if one was provided
    if (in_size > 0.0f)
        npc->size = in_size;

    entity_list.AddNPC(npc, true, true);
    SetPetID(npc->GetID());

    // Initialize pet gear bag system after pet is fully created
    if (RuleB(Pets, UsePetGearBag)) {
        npc->ScanOwnerForPetGear();

        int rescan_seconds = RuleI(Pets, PetGearBagRescanSeconds);
        if (rescan_seconds > 0) {
            npc->StartPetGearRescan(rescan_seconds);
        }
    }

    // We need to handle PetType 5 (petHatelist), add the current target to the hatelist of the pet
    if (record.petcontrol == PetType::TargetLock)
    {
        Mob* m_target = GetTarget();

        bool activiate_pet = false;
        if (m_target && m_target->GetID() != GetID()) {

            if (spells[spell_id].target_type == ST_Self) {
                float distance = CalculateDistance(m_target->GetX(), m_target->GetY(), m_target->GetZ());
                if (distance <= 200) { //Live distance on targetlock pets that self cast. No message is given if not in range.
                    activiate_pet = true;
                }
            }
            else {
                activiate_pet = true;
            }
        }

        if (activiate_pet){
            npc->AddToHateList(m_target, 1);
            npc->SetPetTargetLockID(m_target->GetID());
            npc->SetSpecialAbility(SpecialAbility::AggroImmunity, 1);
        }
        else {
            npc->CastSpell(SPELL_UNSUMMON_SELF, npc->GetID()); //Live like behavior, damages self for 20K
            if (!npc->HasDied()) {
                npc->Kill(); //Ensure pet dies if over 20k HP.
            }
        }
    }
}

void NPC::TryDepopTargetLockedPets(Mob* current_target) {

    if (!current_target || (current_target && (current_target->GetID() != GetPetTargetLockID()) || current_target->IsCorpse())) {

        //Use when swarmpets are set to auto lock from quest or rule
        if (GetSwarmInfo() && GetSwarmInfo()->target) {
            Mob* owner = entity_list.GetMobID(GetSwarmInfo()->owner_id);
            if (owner) {
                owner->SetTempPetCount(owner->GetTempPetCount() - 1);
            }
            Depop();
            return;
        }
        //Use when pets are given petype 5
        if (IsPet() && GetPetType() == PetType::TargetLock && GetPetTargetLockID()) {
            CastSpell(SPELL_UNSUMMON_SELF, GetID()); //Live like behavior, damages self for 20K
            if (!HasDied()) {
                Kill(); //Ensure pet dies if over 20k HP.
            }
            return;
        }
    }
}

/* This is why the pets ghost - pets were being spawned too far away from its npc owner and some
into walls or objects (+10), this sometimes creates the "ghost" effect. I changed to +2 (as close as I
could get while it still looked good). I also noticed this can happen if an NPC is spawned on the same spot of another or in a related bad spot.*/
Pet::Pet(NPCType *type_data, Mob *owner, uint8 pet_type, uint16 spell_id, int16 power)
: NPC(type_data, 0, owner->GetPosition() + glm::vec4(2.0f, 2.0f, 0.0f, 0.0f), GravityBehavior::Water)
{
    GiveNPCTypeData(type_data);
    SetPetType(pet_type);
    SetPetPower(power);
    SetOwnerID(owner ? owner->GetID() : 0);
    SetPetSpellID(spell_id);

    // All pets start at false on newer clients. The client
    // turns it on and tracks the state.
    SetTaunting(false);

    // Older clients didn't track state, and default taunting is on (per @mackal)
    // Familiar and animation pets don't get taunt until an AA.
    if (owner && owner->IsClient()) {
        if (!(owner->CastToClient()->ClientVersionBit() & EQ::versions::maskUFAndLater)) {
            if (
                (GetPetType() != PetType::Familiar && GetPetType() != PetType::Animation) ||
                aabonuses.PetCommands[PetCommand::Taunt]
            ) {
                SetTaunting(true);
            }
        }
    }

    // Initialize pet gear system
    m_virtual_gear = PetVirtualGear();
}

// ------------------------------
// Pet Gear Bag System
// ------------------------------

bool Pet::ComputePetBagSignature(uint64 &out_sig)
{
    out_sig = 0;

    Mob* owner = GetOwner();
    if (!owner || !owner->IsClient()) return false;

    Client* cl = owner->CastToClient();
    // old method
	// const int32 target_loregroup = RuleI(Pets, PetGearBagLoregroup);

	//new petbagloregroup method
	const int32 target_loregroup = ResolvePetBagLoregroup();

    if (target_loregroup <= 0) return false;

    auto sig_container = [&](int16 slot_id)
    {
        const EQ::ItemInstance* inst = cl->GetInv().GetItem(slot_id);
        if (!inst || !inst->IsClassBag()) return;

        const EQ::ItemData* bag_item = inst->GetItem();
        if (!bag_item || bag_item->BagSlots <= 0) return;
        if (bag_item->LoreGroup != target_loregroup) return;

        // Mix in bag identity & location
        uint64 h = 0;
        SigMix(h, static_cast<uint64>(bag_item->ID));
        SigMix(h, static_cast<uint64>(slot_id));

        for (uint8 i = 0; i < bag_item->BagSlots; ++i) {
            const EQ::ItemInstance* sub = inst->GetItem(i);
            if (!sub) {
                sub = inst->GetItem(static_cast<int16>(EQ::invbag::SLOT_BEGIN + i));
            }
            if (!sub) continue;
            if (sub->IsClassBag()) continue;

            const EQ::ItemData* si = sub->GetItem();
            if (!si) continue;

            // Only count common/equippable
            if (si->ItemClass == 0 /* ItemClassCommon */) {
                SigMix(h, static_cast<uint64>(si->ID));
                SigMix(h, static_cast<uint64>(si->ItemType));
                SigMix(h, static_cast<uint64>(si->Damage) << 32 | static_cast<uint64>(si->Delay));
                SigMix(h, static_cast<uint64>(si->Slots));
            }
        }
        // Mix the per-bag hash into the overall
        SigMix(out_sig, h);
    };

    // General inventory
    for (int16 s = EQ::invslot::GENERAL_BEGIN; s <= EQ::invslot::GENERAL_END; ++s) {
        sig_container(s);
    }
    // Bank, if allowed
    if (RuleB(Pets, PetGearBagScanBank)) {
        for (int16 s = EQ::invslot::BANK_BEGIN; s <= EQ::invslot::BANK_END; ++s) {
            sig_container(s);
        }
    }

    return (out_sig != 0);
}

void Pet::ScanOwnerForPetGear()
{
    Mob* owner = GetOwner();
    Client* cl = (owner && owner->IsClient()) ? owner->CastToClient() : nullptr;

    if (!RuleB(Pets, UsePetGearBag)) {
        if (cl) cl->Message(Chat::Yellow, "[PetGear] Disabled by rule Pets:UsePetGearBag.");
        return;
    }
    if (!cl) {
        return; // no one to message / no client owner
    }

    const bool virtual_on = RuleB(Pets, PetGearBagVirtualEquip);
    const bool scan_bank  = RuleB(Pets, PetGearBagScanBank);
    const int  loregrp    = ResolvePetBagLoregroup();

    cl->Message(Chat::Yellow, "[PetGear] scan begin (virtual=%s, bank=%s, loregroup=%d)",
               virtual_on ? "on" : "off", scan_bank ? "on" : "off", loregrp);

    // Collect items first (no side effects yet)
    std::vector<const EQ::ItemData*> gear_items;
    const bool found = GetItemsFromPetGearBag(gear_items);

    // Establish/restore baseline min/max once per-pet
    auto it = g_pet_base_dmg.find(this);
    if (it == g_pet_base_dmg.end()) {
        g_pet_base_dmg[this] = {min_dmg, max_dmg};
    } else {
        // restore to baseline before computing new virtual bonus
        min_dmg = it->second.first;
        max_dmg = it->second.second;
    }

    // Reset our own virtual snapshot (does NOT touch player gear)
    ClearPetGearStats();

    const int base_min = min_dmg;
    const int base_max = max_dmg;

    if (found) {
        for (const auto* itx : gear_items) {
            if (itx) ProcessItemForPetGear(itx);
        }
    } else {
        cl->Message(Chat::Yellow, "[PetGear] no matching bag/items found.");
    }

    if (virtual_on) {
        ApplyPetGearStats();
        ApplyVirtualWeaponDamageBonus(base_min, base_max);
    } else {
        min_dmg = base_min;
        max_dmg = base_max;
    }

    // Push/clear weapon visuals every time so the hands match our snapshot
    UpdatePetWeaponAppearance();

    // Pretty names for chat
    const char* p_name = "none";
    const char* s_name = "none";
    if (m_virtual_gear.primary_weapon.item_id) {
        if (const EQ::ItemData* it2 = database.GetItem(m_virtual_gear.primary_weapon.item_id)) p_name = it2->Name;
    }
    if (m_virtual_gear.secondary_weapon.item_id) {
        if (const EQ::ItemData* it3 = database.GetItem(m_virtual_gear.secondary_weapon.item_id)) s_name = it3->Name;
    }

    cl->Message(Chat::Yellow,
        "[PetGear] %s | items=%d  AC+%d HP+%d ATK+%d Haste=%d  min=%d max=%d",
        found ? "applied" : "cleared",
        (int)gear_items.size(),
        m_virtual_gear.ac_bonus, m_virtual_gear.hp_bonus,
        m_virtual_gear.attack_bonus, m_virtual_gear.haste_bonus,
        min_dmg, max_dmg);
    cl->Message(Chat::Yellow, "[PetGear] Using weapons: Primary: %s  Secondary: %s", p_name, s_name);
}

void Pet::ClearPetGearStats()
{
    // Only reset our snapshot. Do NOT call CalcBonuses() here.
    m_virtual_gear.Reset();

    // Visuals will be updated by UpdatePetWeaponAppearance(), but that function now
    // avoids churn by comparing to last pushed models.
    UpdatePetWeaponAppearance();
}
int32 Pet::ResolvePetBagLoregroup()
{
    // Fallback to old single-loregroup rule
    int32 fallback = RuleI(Pets, PetGearBagLoregroup);

    if (!RuleB(Pets, UseClassMappedPetBags))
        return fallback;

    const Mob* owner = GetOwner();
    if (!owner)
        return fallback;

    // Map owner class -> short key used in CSV
    const char* key = "";
    switch (owner->GetClass()) {
        case 13: key = "MAG"; break; // Magician
        case 11: key = "NEC"; break; // Necromancer
        case 15: key = "BST"; break; // Beastlord
        case 14: key = "ENC"; break; // Enchanter
        case 5:  key = "SHD"; break; // Shadow Knight
        case 10: key = "SHM"; break; // Shaman
        case 8:  key = "BRD"; break; // Bard
        default: key = "";           break; // not mapped → fallback
    }
    if (!key[0])
        return fallback;

    std::unordered_map<std::string, int32> map;
    std::string csv = RuleS(Pets, PetGearBagLoregroupsCSV);
    if (csv.empty())
        return fallback;

    // Parse "KEY:NUMBER" pairs separated by commas
    // Example: "MAG:15071,NEC:15072,BST:15073,..."
    auto tokens = Strings::Split(csv, ',');
    for (const auto& t : tokens)
    {
        std::string token = t;                 // make a copy
        Strings::Trim(token);                  // in-place

        if (token.empty())
            continue;

        size_t pos = token.find(':');
        if (pos == std::string::npos)
            continue;

        std::string k = token.substr(0, pos);
        Strings::Trim(k);
        Strings::ToUpper(k);                   // in-place

        std::string v = token.substr(pos + 1);
        Strings::Trim(v);

        if (!k.empty() && Strings::IsNumber(v)) {
            map[k] = Strings::ToInt(v);
        }
    }

    auto it = map.find(key);
    if (it != map.end() && it->second > 0)
        return it->second;

    return fallback;
}


bool Pet::GetItemsFromPetGearBag(std::vector<const EQ::ItemData*>& items)
{
    Mob* owner = GetOwner();
    if (!owner || !owner->IsClient()) return false;

    Client* client_owner = owner->CastToClient();

    // Resolve allowed loregroups for THIS pet based on the summoner class
    std::vector<int32> allowed_loregroups;
    bool scan_bank_override = false;
    bool scan_bank_value = false;
    ResolvePetLoregroups(GetSummonerClass(), allowed_loregroups, scan_bank_override, scan_bank_value);
    if (allowed_loregroups.empty()) return false;

    auto lore_ok = [&](int32 lg) -> bool {
        for (auto x : allowed_loregroups) if (x == lg) return true;
        return false;
    };

    const bool scan_bank = scan_bank_override ? scan_bank_value : RuleB(Pets, PetGearBagScanBank);

    auto scan_container = [&](int16 slot_id)
    {
        const EQ::ItemInstance* inst = client_owner->GetInv().GetItem(slot_id);
        if (!inst || !inst->IsClassBag()) return;

        const EQ::ItemData* bag_item = inst->GetItem();
        if (!bag_item || bag_item->BagSlots <= 0) return;
        if (!lore_ok(bag_item->LoreGroup)) return; // **class-specific bag filter**

        // Optional breadcrumb
        if (owner->IsClient()) {
            owner->CastToClient()->Message(Chat::Yellow,
                "[PetGear] Using bag '%s' (id %u) for class '%s'",
                bag_item->Name, bag_item->ID, ClassAbbr(GetSummonerClass()));
        }

        // Iterate contents (support both addressing schemes)
        for (uint8 i = 0; i < bag_item->BagSlots; ++i) {
            const EQ::ItemInstance* sub_inst = inst->GetItem(i);
            if (!sub_inst) sub_inst = inst->GetItem(static_cast<int16>(EQ::invbag::SLOT_BEGIN + i));
            if (!sub_inst) continue;
            if (sub_inst->IsClassBag()) continue; // skip nested containers

            const EQ::ItemData* sub_item = sub_inst->GetItem();
            if (!sub_item) continue;

            // Only equippable/common items (ItemClass == 0 on EQEmu)
            if (sub_item->ItemClass == 0) {
                items.push_back(sub_item);
            }
        }
    };

    // General inventory
    for (int16 slot = EQ::invslot::GENERAL_BEGIN; slot <= EQ::invslot::GENERAL_END; ++slot)
        scan_container(slot);

    // Bank (optional)
    if (scan_bank) {
        for (int16 slot = EQ::invslot::BANK_BEGIN; slot <= EQ::invslot::BANK_END; ++slot)
            scan_container(slot);
    }

    return !items.empty();
}


void Pet::ProcessItemForPetGear(const EQ::ItemData* item) {
    if (!item) return;
    using namespace EQ::invslot;

    // Helper: identify 2H weapon types
    auto is_two_hand_type = [](uint8 t) -> bool {
        // 1=2HS, 4=2HB, 35=2HP
        return (t == 1 || t == 4 || t == 35);
    };

    // Current primary state (for blocking secondary when primary is 2H)
    const bool primary_has_weapon = (m_virtual_gear.primary_weapon.item_id != 0);
    const bool primary_is_twohand = primary_has_weapon && is_two_hand_type(m_virtual_gear.primary_weapon.skill);

    // Initial slot decision
    int virtual_slot = DetermineVirtualSlot(item);

    // If DetermineVirtualSlot chose secondary while primary is a 2H, reject this item
    if (virtual_slot == slotSecondary && primary_is_twohand) {
        return;
    }

    // If DetermineVirtualSlot chose primary but it's already occupied,
    // and the item can go secondary, and primary isn't a 2H, try secondary.
    if (virtual_slot == slotPrimary &&
        m_virtual_gear.item_id[slotPrimary] != 0)
    {
        const bool can_secondary = (item->Slots & (1u << slotSecondary)) != 0;
        if (can_secondary && !primary_is_twohand &&
            m_virtual_gear.item_id[slotSecondary] == 0)
        {
            virtual_slot = slotSecondary;
        }
    }

    // If we still don't have a valid/free destination, bail
    if (virtual_slot < 0) return;
    if (m_virtual_gear.item_id[virtual_slot] != 0) return;

    // Place it
    m_virtual_gear.item_id[virtual_slot] = item->ID;

    // Accumulate stats
    AccumulateItemStats(item);

    // Weapon bookkeeping
    if (virtual_slot == slotPrimary ||
        virtual_slot == slotSecondary ||
        virtual_slot == slotRange)
    {
        ProcessWeaponForPet(item, virtual_slot);
    }
}

void Pet::ApplyVirtualWeaponDamageBonus(int base_min, int base_max)
{
    const auto* P = GetPetPrimaryWeapon();
    const auto* S = GetPetSecondaryWeapon();

    const float rP = (P && P->delay > 0) ? float(P->damage) / float(P->delay) : 0.f;
    const float rS = (S && S->delay > 0) ? float(S->damage) / float(S->delay) : 0.f;

    if (rP <= 0.f && rS <= 0.f) {
        // restore baseline if no virtual weapons
        min_dmg = base_min;
        max_dmg = base_max;
        return;
    }

    const float offhand  = RuleR(Pets, PetGearBagVirtualOffhandScalar); // e.g. 0.66
    const float scale    = RuleR(Pets, PetGearBagVirtualDmgScale);      // e.g. 180.0
    const float combined = rP + (rS * offhand);

    const int add_max = (int)std::round(combined * scale);
    const int add_min = (int)std::round(add_max * 0.25f);

    // apply from baseline so rescans do not stack
    min_dmg = std::max<int>(1, base_min + add_min);
    max_dmg = std::max<int>(min_dmg + 1, base_max + add_max);
}
static inline bool IsTwoHandType(uint8 item_type)
{
    // Detect if 2h weapon to prevent offhand weapon as well - EQEmu item->ItemType Mapping
    // 1=2HS, 4=2HB, 35=2HP
    return (item_type == 1 || item_type == 4 || item_type == 35);
}

int Pet::DetermineVirtualSlot(const EQ::ItemData* item)
{
    if (!item) return -1;
    using namespace EQ::invslot;

    const uint32 slots = item->Slots;

    const bool can_primary   = (slots & (1u << slotPrimary))   != 0;
    const bool can_secondary = (slots & (1u << slotSecondary)) != 0;
    const bool can_range     = (slots & (1u << slotRange))     != 0;

    // If primary is a two-hander, block any attempt to use secondary.
    const bool primary_is_twohand =
        (m_virtual_gear.primary_weapon.item_id != 0) &&
        IsTwoHandType(m_virtual_gear.primary_weapon.skill);

    // Weapons first: prefer Primary, then Secondary (if not blocked), then Range
    if (can_primary || can_secondary || can_range) {
        if (can_primary) {
            if (m_virtual_gear.item_id[slotPrimary] == 0) {
                return slotPrimary;
            }
        }
        if (can_secondary) {
            if (!primary_is_twohand && m_virtual_gear.item_id[slotSecondary] == 0) {
                return slotSecondary;
            }
        }
        if (can_range) {
            if (m_virtual_gear.item_id[slotRange] == 0) {
                return slotRange;
            }
        }
        // Nothing free among weapon slots
        return -1;
    }

    // Singletons
    if (slots & (1u << slotHead))      return (m_virtual_gear.item_id[slotHead]      == 0) ? slotHead      : -1;
    if (slots & (1u << slotFace))      return (m_virtual_gear.item_id[slotFace]      == 0) ? slotFace      : -1;
    if (slots & (1u << slotNeck))      return (m_virtual_gear.item_id[slotNeck]      == 0) ? slotNeck      : -1;
    if (slots & (1u << slotShoulders)) return (m_virtual_gear.item_id[slotShoulders] == 0) ? slotShoulders : -1;
    if (slots & (1u << slotBack))      return (m_virtual_gear.item_id[slotBack]      == 0) ? slotBack      : -1;
    if (slots & (1u << slotChest))     return (m_virtual_gear.item_id[slotChest]     == 0) ? slotChest     : -1;
    if (slots & (1u << slotArms))      return (m_virtual_gear.item_id[slotArms]      == 0) ? slotArms      : -1;
    if (slots & (1u << slotHands))     return (m_virtual_gear.item_id[slotHands]     == 0) ? slotHands     : -1;
    if (slots & (1u << slotWaist))     return (m_virtual_gear.item_id[slotWaist]     == 0) ? slotWaist     : -1;
    if (slots & (1u << slotLegs))      return (m_virtual_gear.item_id[slotLegs]      == 0) ? slotLegs      : -1;
    if (slots & (1u << slotFeet))      return (m_virtual_gear.item_id[slotFeet]      == 0) ? slotFeet      : -1;

    // Pairs: pick-first-free
    if (slots & ((1u << slotEar1) | (1u << slotEar2))) {
        return PickFirstFree(slotEar1,   slotEar2);
    }
    if (slots & ((1u << slotFinger1) | (1u << slotFinger2))) {
        return PickFirstFree(slotFinger1, slotFinger2);
    }
    if (slots & ((1u << slotWrist1) | (1u << slotWrist2))) {
        return PickFirstFree(slotWrist1,  slotWrist2);
    }

    return -1;
}

void Pet::AccumulateItemStats(const EQ::ItemData* item) {
    if (!item) return;

    // Primary stats
    m_virtual_gear.ac_bonus       += item->AC;
    m_virtual_gear.hp_bonus       += item->HP;
    m_virtual_gear.mana_bonus     += item->Mana;
    m_virtual_gear.endurance_bonus+= item->Endur;

    // Attributes
    m_virtual_gear.str_bonus += item->AStr;
    m_virtual_gear.sta_bonus += item->ASta;
    m_virtual_gear.agi_bonus += item->AAgi;
    m_virtual_gear.dex_bonus += item->ADex;
    m_virtual_gear.wis_bonus += item->AWis;
    m_virtual_gear.int_bonus += item->AInt;
    m_virtual_gear.cha_bonus += item->ACha;

    // Combat stats
    m_virtual_gear.attack_bonus   += item->Attack;
    m_virtual_gear.accuracy_bonus += item->Accuracy;        // maps to HitChance
    m_virtual_gear.avoidance_bonus+= item->Avoidance;       // maps to AvoidMeleeChance

    // Haste: keep highest
    if (item->Haste > 0 && item->Haste > m_virtual_gear.haste_bonus) {
        m_virtual_gear.haste_bonus = item->Haste;
    }

    // Resistances
    m_virtual_gear.mr_bonus        += item->MR;
    m_virtual_gear.fr_bonus        += item->FR;
    m_virtual_gear.cr_bonus        += item->CR;
    m_virtual_gear.pr_bonus        += item->PR;
    m_virtual_gear.dr_bonus        += item->DR;
    m_virtual_gear.corruption_bonus+= item->SVCorruption;
}

void Pet::ProcessWeaponForPet(const EQ::ItemData* item, int virtual_slot) {
    if (!item) return;

    PetVirtualGear::WeaponData* weapon_data = nullptr;

    if (virtual_slot == EQ::invslot::slotPrimary) {
        weapon_data = &m_virtual_gear.primary_weapon;
    } else if (virtual_slot == EQ::invslot::slotSecondary) {
        weapon_data = &m_virtual_gear.secondary_weapon;
    } else if (virtual_slot == EQ::invslot::slotRange) {
        weapon_data = &m_virtual_gear.ranged_weapon;
    }

    if (!weapon_data) return;

    // Store weapon stats
    weapon_data->item_id = item->ID;
    weapon_data->damage  = item->Damage;
    weapon_data->delay   = item->Delay;
    weapon_data->skill   = item->ItemType;

    // Handle weapon procs
    if (RuleB(Pets, PetGearBagAllowProcs)) {
        if (item->Proc.Effect != 0 && IsValidSpell(item->Proc.Effect)) {
            weapon_data->proc_spell_id = item->Proc.Effect;
            weapon_data->proc_rate     = item->Proc.Level; // proc rate from item

            // Add to master proc list
            m_virtual_gear.weapon_proc_spells.push_back(item->Proc.Effect);
            m_virtual_gear.weapon_proc_rates.push_back(item->Proc.Level);

            LogDebug("[Pets] {} weapon proc added: Spell [{}] Rate [{}]",
                GetName(), item->Proc.Effect, item->Proc.Level);
        }
    }
}

uint16 Pet::WeaponModelFromItem(const EQ::ItemData* it)
{
    if (!it) return 0;

    // Most EQEmu branches use IDFile like "IT63" for weapon models.
    // Parse the numeric part to get the model id. If IDFile is empty, fall back to 0.
    if (it->IDFile && it->IDFile[0]) {
        // Expect "ITnnn" – skip first two chars if they are letters
        const char* p = it->IDFile;
        if ((p[0] == 'I' || p[0] == 'i') && (p[1] == 'T' || p[1] == 't')) {
            p += 2;
        }
        int v = atoi(p);
        if (v > 0) return static_cast<uint16>(v);
    }
    return 0;
}

void Pet::UpdatePetWeaponAppearance()
{
    // Compute models from our virtual primary/secondary
    uint16 prim_model = 0;
    uint16 sec_model  = 0;

    if (m_virtual_gear.primary_weapon.item_id) {
        if (const EQ::ItemData* it = database.GetItem(m_virtual_gear.primary_weapon.item_id)) {
            prim_model = WeaponModelFromItem(it);
        }
    }
    if (m_virtual_gear.secondary_weapon.item_id) {
        if (const EQ::ItemData* it = database.GetItem(m_virtual_gear.secondary_weapon.item_id)) {
            sec_model = WeaponModelFromItem(it);
        }
    }

    // Avoid churn: only push when changed
    if (prim_model != m_virtual_gear.last_prim_model) {
        WearChange(EQ::textures::weaponPrimary, prim_model, 0);
        m_virtual_gear.last_prim_model = prim_model;
    }
    if (sec_model != m_virtual_gear.last_sec_model) {
        WearChange(EQ::textures::weaponSecondary, sec_model, 0);
        m_virtual_gear.last_sec_model = sec_model;
    }

    // Update light (torches, glow, etc.) if the models imply light
    UpdateEquipmentLight();
}

void Pet::ApplyPetGearStats()
{
    // Snapshot old absolute values (force everything to int to avoid MSVC max/min ambiguity)
    int old_max_hp   = std::max(1, static_cast<int>(GetMaxHP()));
    int old_cur_hp   = std::max(1, static_cast<int>(GetHP()));
    int old_max_mana = std::max(0, static_cast<int>(GetMaxMana()));
    int old_cur_mana = std::max(0, static_cast<int>(GetMana()));

    // 1) Recompute baseline (rebuilds itembonuses from real gear/buffs)
    CalcBonuses();

    // 2) Apply virtual gear deltas on top of baseline
    StatBonuses &ib = itembonuses;

    // Core pools
    ib.AC   += m_virtual_gear.ac_bonus;
    ib.HP   += m_virtual_gear.hp_bonus;
    ib.Mana += m_virtual_gear.mana_bonus;

    // Attributes
    ib.STR += m_virtual_gear.str_bonus;
    ib.STA += m_virtual_gear.sta_bonus;
    ib.AGI += m_virtual_gear.agi_bonus;
    ib.DEX += m_virtual_gear.dex_bonus;
    ib.WIS += m_virtual_gear.wis_bonus;
    ib.INT += m_virtual_gear.int_bonus;
    ib.CHA += m_virtual_gear.cha_bonus;

    // Offense
    ib.ATK += m_virtual_gear.attack_bonus;

    // Accuracy / Avoidance / Haste
    ib.HitChance        += m_virtual_gear.accuracy_bonus;
    ib.AvoidMeleeChance += m_virtual_gear.avoidance_bonus;
    if (m_virtual_gear.haste_bonus > ib.haste) {
        ib.haste = m_virtual_gear.haste_bonus;
    }

    // Resists
    ib.MR += m_virtual_gear.mr_bonus;
    ib.FR += m_virtual_gear.fr_bonus;
    ib.CR += m_virtual_gear.cr_bonus;
    ib.PR += m_virtual_gear.pr_bonus;
    ib.DR += m_virtual_gear.dr_bonus;

    // 3) Preserve absolute values relative to the change in max (no % math → no climb)
    int new_max_hp   = std::max(1, static_cast<int>(GetMaxHP()));
    int new_max_mana = std::max(0, static_cast<int>(GetMaxMana()));

    int hp_delta   = new_max_hp   - old_max_hp;
    int mana_delta = new_max_mana - old_max_mana;

    auto clamp = [](int v, int lo, int hi) {
        if (v < lo) return lo;
        if (v > hi) return hi;
        return v;
    };

    int new_cur_hp   = clamp(old_cur_hp   + hp_delta,   1, new_max_hp);
    int new_cur_mana = clamp(old_cur_mana + mana_delta, 0, new_max_mana);

    SetHP(new_cur_hp);
    SetMana(new_cur_mana);
}

// ========================================================================
// End Pet Gear Bag System Implementation
// ========================================================================

bool ZoneDatabase::GetPetEntry(const std::string& pet_type, PetRecord *p)
{
    return GetPoweredPetEntry(pet_type, 0, p);
}

bool ZoneDatabase::GetPoweredPetEntry(const std::string& pet_type, int16 pet_power, PetRecord* r)
{
    const auto& l = PetsRepository::GetWhere(
        content_db,
        fmt::format(
            "`type` = '{}' AND `petpower` <= {} ORDER BY `petpower` DESC LIMIT 1",
            pet_type,
            pet_power <= 0 ? 0 : pet_power
        )
    );

    if (l.empty()) {
        return false;
    }

    auto &e = l.front();

    r->npc_type     = e.npcID;
    r->temporary    = e.temp;
    r->petpower     = e.petpower;
    r->petcontrol   = e.petcontrol;
    r->petnaming    = e.petnaming;
    r->monsterflag  = e.monsterflag;
    r->equipmentset = e.equipmentset;

    return true;
}

Mob* Mob::GetPet() {
    if (!GetPetID()) {
        return nullptr;
    }

    const auto m = entity_list.GetMob(GetPetID());
    if (!m) {
        SetPetID(0);
        return nullptr;
    }

    if (m->GetOwnerID() != GetID()) {
        SetPetID(0);
        return nullptr;
    }

    return m;
}

bool Mob::HasPet() const {
    if (GetPetID() == 0) {
        return false;
    }

    const auto m = entity_list.GetMob(GetPetID());
    if (!m) {
        return false;
    }

    if (m->GetOwnerID() != GetID()) {
        return false;
    }

    return true;
}

void Mob::SetPet(Mob* newpet) {
    Mob* oldpet = GetPet();
    if (oldpet) {
        oldpet->SetOwnerID(0);
    }
    if (newpet == nullptr) {
        SetPetID(0);
    } else {
        SetPetID(newpet->GetID());
        Mob* oldowner = entity_list.GetMob(newpet->GetOwnerID());
        if (oldowner)
            oldowner->SetPetID(0);
        newpet->SetOwnerID(GetID());
    }
}

void Mob::SetPetID(uint16 NewPetID) {
    if (NewPetID == GetID() && NewPetID != 0)
        return;
    petid = NewPetID;

    if(IsClient())
    {
        Mob* NewPet = entity_list.GetMob(NewPetID);
        CastToClient()->UpdateXTargetType(MyPet, NewPet);
    }
}

void NPC::GetPetState(SpellBuff_Struct *pet_buffs, uint32 *items, char *name) {
    //save the pet name
    strn0cpy(name, GetName(), 64);

    //save their items, we only care about what they are actually wearing
    memcpy(items, equipment, sizeof(uint32) * EQ::invslot::EQUIPMENT_COUNT);

    //save their buffs.
    for (int i=EQ::invslot::EQUIPMENT_BEGIN; i < GetPetMaxTotalSlots(); i++) {
        if (IsValidSpell(buffs[i].spellid)) {
            pet_buffs[i].spellid = buffs[i].spellid;
            pet_buffs[i].effect_type = i+1;
            pet_buffs[i].duration = buffs[i].ticsremaining;
            pet_buffs[i].level = buffs[i].casterlevel;
            pet_buffs[i].bard_modifier = 10;
            pet_buffs[i].counters = buffs[i].counters;
            pet_buffs[i].bard_modifier = buffs[i].instrument_mod;
        }
        else {
            pet_buffs[i].spellid = SPELL_UNKNOWN;
            pet_buffs[i].duration = 0;
            pet_buffs[i].level = 0;
            pet_buffs[i].bard_modifier = 10;
            pet_buffs[i].counters = 0;
        }
    }
}

void NPC::SetPetState(SpellBuff_Struct *pet_buffs, uint32 *items) {
    //restore their buffs...

    int i;
    for (i = 0; i < GetPetMaxTotalSlots(); i++) {
        for(int z = 0; z < GetPetMaxTotalSlots(); z++) {
        // check for duplicates
            if(IsValidSpell(buffs[z].spellid) && buffs[z].spellid == pet_buffs[i].spellid) {
                buffs[z].spellid = SPELL_UNKNOWN;
                pet_buffs[i].spellid = 0xFFFFFFFF;
            }
        }

        if (pet_buffs[i].spellid <= (uint32)SPDAT_RECORDS && pet_buffs[i].spellid != 0 && (pet_buffs[i].duration > 0 || pet_buffs[i].duration == -1)) {
            if(pet_buffs[i].level == 0 || pet_buffs[i].level > 100)
                pet_buffs[i].level = 1;
            buffs[i].spellid            = pet_buffs[i].spellid;
            buffs[i].ticsremaining      = pet_buffs[i].duration;
            buffs[i].casterlevel        = pet_buffs[i].level;
            buffs[i].casterid           = 0;
            buffs[i].counters           = pet_buffs[i].counters;
            buffs[i].hit_number         = spells[pet_buffs[i].spellid].hit_number;
            buffs[i].instrument_mod     = pet_buffs[i].bard_modifier;
        }
        else {
            buffs[i].spellid = SPELL_UNKNOWN;
            pet_buffs[i].spellid = 0xFFFFFFFF;
            pet_buffs[i].effect_type = 0;
            pet_buffs[i].level = 0;
            pet_buffs[i].duration = 0;
            pet_buffs[i].bard_modifier = 0;
        }
    }
    for (int j1=0; j1 < GetPetMaxTotalSlots(); j1++) {
        if (buffs[j1].spellid <= (uint32)SPDAT_RECORDS) {
            for (int x1=0; x1 < EFFECT_COUNT; x1++) {
                switch (spells[buffs[j1].spellid].effect_id[x1]) {
                    case SpellEffect::AddMeleeProc:
                    case SpellEffect::WeaponProc:
                        // We need to reapply buff based procs
                        // We need to do this here so suspended pets also regain their procs.
                        AddProcToWeapon(GetProcID(buffs[j1].spellid,x1), false, 100+spells[buffs[j1].spellid].limit_value[x1], buffs[j1].spellid, buffs[j1].casterlevel, GetSpellProcLimitTimer(buffs[j1].spellid, ProcType::MELEE_PROC));
                        break;
                    case SpellEffect::DefensiveProc:
                        AddDefensiveProc(GetProcID(buffs[j1].spellid, x1), 100 + spells[buffs[j1].spellid].limit_value[x1], buffs[j1].spellid, GetSpellProcLimitTimer(buffs[j1].spellid, ProcType::DEFENSIVE_PROC));
                        break;
                    case SpellEffect::RangedProc:
                        AddRangedProc(GetProcID(buffs[j1].spellid, x1), 100 + spells[buffs[j1].spellid].limit_value[x1], buffs[j1].spellid, GetSpellProcLimitTimer(buffs[j1].spellid, ProcType::RANGED_PROC));
                        break;
                    case SpellEffect::Charm:
                    case SpellEffect::Rune:
                    case SpellEffect::NegateAttacks:
                    case SpellEffect::Illusion:
                        buffs[j1].spellid = SPELL_UNKNOWN;
                        pet_buffs[j1].spellid = SPELLBOOK_UNKNOWN;
                        pet_buffs[j1].effect_type = 0;
                        pet_buffs[j1].level = 0;
                        pet_buffs[j1].duration = 0;
                        pet_buffs[j1].bard_modifier = 0;
                        x1 = EFFECT_COUNT;
                        break;
                    // We can't send appearance packets yet, put down at CompleteConnect
                }
            }
        }
    }

    //restore their equipment...
    for (i = EQ::invslot::EQUIPMENT_BEGIN; i <= EQ::invslot::EQUIPMENT_END; i++) {
        if (items[i] == 0) {
            continue;
        }

        const EQ::ItemData *item2 = database.GetItem(items[i]);

        if (item2) {
            bool noDrop           = (item2->NoDrop == 0); // Field is reverse logic
            bool petCanHaveNoDrop = (RuleB(Pets, CanTakeNoDrop) && _CLIENTPET(this) && GetPetType() <= PetType::Normal);

            if (!noDrop || petCanHaveNoDrop) {
                AddLootDrop(item2, LootdropEntriesRepository::NewNpcEntity(), true);
            }
        }
    }
}

// Load the equipmentset from the DB. Might be worthwhile to load these into
// shared memory at some point due to the number of queries needed to load a
// nested set.
bool ZoneDatabase::GetBasePetItems(int32 equipmentset, uint32 *items) {
    if (equipmentset < 0 || items == nullptr)
        return false;

    int depth = 0;
    int32 curset = equipmentset;
    int32 nextset = -1;
    uint32 slot;

    while (curset >= 0 && depth < 5) {
        std::string  query = StringFormat("SELECT nested_set FROM pets_equipmentset WHERE set_id = '%d'", curset);
        auto results = QueryDatabase(query);
        if (!results.Success()) {
            return false;
        }

        if (results.RowCount() != 1) {
            // invalid set reference, it doesn't exist
            LogError("Error in GetBasePetItems equipment set [{}] does not exist", curset);
            return false;
        }

        auto row = results.begin();
        nextset = Strings::ToInt(row[0]);

        query = StringFormat("SELECT slot, item_id FROM pets_equipmentset_entries WHERE set_id='%d'", curset);
        results = QueryDatabase(query);
        if (results.Success()) {
            for (row = results.begin(); row != results.end(); ++row)
            {
                slot = Strings::ToInt(row[0]);

                if (slot > EQ::invslot::EQUIPMENT_END)
                    continue;

                if (items[slot] == 0)
                    items[slot] = Strings::ToInt(row[1]);
            }
        }

        curset = nextset;
        depth++;
    }

    return true;
}

bool Pet::CheckSpellLevelRestriction(Mob *caster, uint16 spell_id)
{
    auto owner = GetOwner();
    if (owner)
        return owner->CheckSpellLevelRestriction(caster, spell_id);
    return true;
}

BeastlordPetData::PetStruct ZoneDatabase::GetBeastlordPetData(uint16 race_id) {
    BeastlordPetData::PetStruct d;

    const auto& e = PetsBeastlordDataRepository::FindOne(*this, race_id);

    if (!e.player_race) {
        return d;
    }

    d.race_id       = e.pet_race;
    d.texture       = e.texture;
    d.helm_texture  = e.helm_texture;
    d.gender        = e.gender;
    d.size_modifier = e.size_modifier;
    d.face          = e.face;

    return d;
}
