#include "../client.h"
#include "../../common/database.h"
#include "../../common/seperator.h"

#include <algorithm>
#include <cctype>
#include <cstring>
#include <string>

// ---------- Class shortname <-> id mapping ----------
static const char* kClassShort[17] = {
    "", "WAR","CLR","PAL","RNG","SHD","DRU","MNK","BRD","ROG","SHM","NEC","WIZ","MAG","ENC","BST","BER"
};

static int ci_compare(const char* lhs, const char* rhs)
{
    if (lhs == rhs)
        return 0;
    if (!lhs)
        return -1;
    if (!rhs)
        return 1;

    while (*lhs && *rhs) {
        unsigned char a = static_cast<unsigned char>(*lhs++);
        unsigned char b = static_cast<unsigned char>(*rhs++);
        int diff = std::tolower(a) - std::tolower(b);
        if (diff != 0)
            return diff;
    }
    if (*lhs == *rhs)
        return 0;
    return *lhs ? 1 : -1;
}

static std::string strtoupper(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c){ return std::toupper(c); });
    return s;
}

static bool parse_uint8(const char* s, uint8& out) {
    if (!s || !*s) return false;
    char* end = nullptr;
    long v = strtol(s, &end, 10);
    if (end == s || *end != '\0' || v < 0 || v > 255) return false;
    out = static_cast<uint8>(v);
    return true;
}

static bool lookup_class_token(const char* tok, uint8& out) {
    if (!tok || !*tok) return false;
    if (parse_uint8(tok, out) && out >= 1 && out <= 16) return true;
    std::string u = strtoupper(tok);
    for (uint8 i = 1; i <= 16; ++i) if (u == kClassShort[i]) { out = i; return true; }
    return false;
}

static const char* class_short(uint8 id) {
    if (id >= 1 && id <= 16) return kClassShort[id];
    return "?";
}

// ---------- DB helpers (no forced inserts) ----------
static uint8 profile_primary_class(Client* c) { return c ? c->GetClass() : 0; }

static bool has_primary_row(uint32 charid, uint8 primary_class) {
    auto r = database.QueryDatabase(fmt::format(
        "SELECT 1 FROM character_classes WHERE char_id = {} AND class_id = {} AND is_primary = 1 LIMIT 1",
        charid, primary_class));
    return r.Success() && r.RowCount() > 0;
}

static bool class_exists(uint32 charid, uint8 cls) {
    auto r = database.QueryDatabase(fmt::format(
        "SELECT 1 FROM character_classes WHERE char_id = {} AND class_id = {} LIMIT 1",
        charid, cls));
    return r.Success() && r.RowCount() > 0;
}

static int count_rows_without_implied(uint32 charid) {
    // Count only valid player classes (1..16)
    auto r = database.QueryDatabase(fmt::format(
        "SELECT COUNT(*) FROM character_classes WHERE char_id = {} AND class_id BETWEEN 1 AND 16",
        charid));
    if (!r.Success() || r.RowCount() == 0) return 0;
    return atoi(r.begin()[0]);
}

// Counts including implied primary if there is no primary row.
static int effective_class_count(uint32 charid, uint8 primary_class) {
    int rows = count_rows_without_implied(charid);
    if (!has_primary_row(charid, primary_class)) {
        // Profile has a primary even if the row is missing
        // Only count it if there is any character at all (rows >= 0 always true here)
        return rows + 1;
    }
    return rows;
}

static void list_classes(Client* c) {
    auto charid = c->CharacterID();
    uint8 pc = profile_primary_class(c);

    // Pull rows
    auto r = database.QueryDatabase(fmt::format(
        "SELECT class_id, is_primary FROM character_classes "
        "WHERE char_id = {} AND class_id BETWEEN 1 AND 16 "
        "ORDER BY is_primary DESC, class_id ASC", charid));

    if (!r.Success()) {
        c->Message(Chat::White, "DB error listing classes: %s", r.ErrorMessage().c_str());
        return;
    }

    // If no row exists for primary, show it as implied
    bool primary_row_exists = has_primary_row(charid, pc);

    std::string line = "Classes: ";
    bool first = true;

    if (!primary_row_exists && pc) {
        line += std::string(class_short(pc)) + "(" + std::to_string(pc) + ") (primary)";
        first = false;
    }

    for (auto row = r.begin(); row != r.end(); ++row) {
        uint8 cls = static_cast<uint8>(atoi(row[0]));
        if (cls < 1 || cls > 16) { continue; }
        bool primary = atoi(row[1]) != 0;
        if (!first) line += ", ";
        line += class_short(cls);
        line += "(" + std::to_string(cls) + ")";
        if (primary) line += " (primary)";
        first = false;
    }

    c->Message(Chat::White, "%s", line.c_str());
}

static void print_usage(Client* c) {
    c->Message(Chat::White,
        "Usage:\n"
        "  #multiclass list\n"
        "  #multiclass add <class_id|shortname>\n"
        "  #multiclass remove <class_id|shortname>\n"
        "  #multiclass <class_id|shortname>    (shorthand for add)\n"
        "  #multiclass removeall CONFIRM:<char_id>\n"
        "Class shortnames: 1=WAR, 2=CLR, 3=PAL, 4=RNG, 5=SHD, 6=DRU, 7=MNK, 8=BRD, 9=ROG,\n"
        "                  10=SHM, 11=NEC, 12=WIZ, 13=MAG, 14=ENC, 15=BST, 16=BER");
}

// ---------- Command ----------
void command_multiclass(Client *c, const Seperator *sep)
{
    if (!c || !sep) return;

    const uint32 charid = c->CharacterID();
    const uint8  primary_cls = profile_primary_class(c);

    const char* t1 = sep->arg[1];
    const char* t2 = sep->arg[2];

    // list
    if (t1 && ci_compare(t1, "list") == 0) {
        list_classes(c);
        return;
    }

    // removeall (confirmation required) — remove only non-primary rows
    if (t1 && ci_compare(t1, "removeall") == 0) {
        std::string expected = fmt::format("CONFIRM:{}", charid);
        if (!t2 || ci_compare(t2, expected.c_str()) != 0) {
            c->Message(Chat::White, "Refusing to remove all. Confirm with: #multiclass removeall %s", expected.c_str());
            return;
        }
        auto r = database.QueryDatabase(fmt::format(
            "DELETE FROM character_classes WHERE char_id = {} AND is_primary = 0", charid));
        if (!r.Success()) {
            c->Message(Chat::White, "DB error: %s", r.ErrorMessage().c_str());
            return;
        }
        c->Message(Chat::White, "Removed all non-primary classes.");
        // Refresh runtime state and emit hydration log
        c->HydrateMulticlassFromDB();
        list_classes(c);
        return;
    }

    // add/remove/shorthand
    std::string action;
    uint8 cls = 0;

    if (t1 && *t1) {
        if (ci_compare(t1, "add") == 0) {
            action = "add";
            if (!lookup_class_token(t2, cls)) { print_usage(c); return; }
        } else if (ci_compare(t1, "remove") == 0) {
            action = "remove";
            if (!lookup_class_token(t2, cls)) { print_usage(c); return; }
        } else {
            // shorthand: first token is the class token
            if (!lookup_class_token(t1, cls)) { print_usage(c); return; }
            action = "add";
        }
    } else {
        print_usage(c);
        return;
    }

    if (cls < 1 || cls > 16) { c->Message(Chat::White, "Class id must be 1..16"); return; }

    if (action == "add") {
        if (cls == primary_cls) { c->Message(Chat::White, "%s(%u) is already the primary class.", class_short(cls), cls); return; }
        if (class_exists(charid, cls)) { c->Message(Chat::White, "%s(%u) is already present.", class_short(cls), cls); return; }

        // Cap check uses *effective* count (rows + implied primary if missing)
        int effective_cnt = effective_class_count(charid, primary_cls);
        if (effective_cnt >= 3) {
            c->Message(Chat::White, "You already have %d classes recorded (including primary). Max is 3.", effective_cnt);
            list_classes(c);
            return;
        }

        auto r = database.QueryDatabase(fmt::format(
            "INSERT INTO character_classes (char_id, class_id, is_primary) VALUES ({}, {}, 0)",
            charid, cls));
        if (!r.Success()) { c->Message(Chat::White, "DB error: %s", r.ErrorMessage().c_str()); return; }

        c->Message(Chat::White, "Added %s(%u).", class_short(cls), cls);
        // Refresh runtime state and emit hydration log
        c->HydrateMulticlassFromDB();
        list_classes(c);
        return;
    }

    // action == remove
    if (cls == primary_cls) { c->Message(Chat::White, "Refusing to remove the primary class %s(%u).", class_short(primary_cls), primary_cls); return; }
    if (!class_exists(charid, cls)) { c->Message(Chat::White, "%s(%u) is not present.", class_short(cls), cls); list_classes(c); return; }

    auto r = database.QueryDatabase(fmt::format(
        "DELETE FROM character_classes WHERE char_id = {} AND class_id = {} LIMIT 1",
        charid, cls));
    if (!r.Success()) { c->Message(Chat::White, "DB error: %s", r.ErrorMessage().c_str()); return; }

    c->Message(Chat::White, "Removed %s(%u).", class_short(cls), cls);
    // Refresh runtime state and emit hydration log
    c->HydrateMulticlassFromDB();
    list_classes(c);
}
