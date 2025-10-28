#include "../client.h"
#include "../../common/database.h"
#include "../../common/seperator.h"

// Minimal GM command to add/remove extra classes for testing
// Usage: #multiclass add <class_id> | remove <class_id>
void command_multiclass(Client *c, const Seperator *sep)
{
    if (!c || !sep) return;
    if (sep->argnum < 2) {
        c->Message(Chat::White, "Usage: #multiclass add <class_id> | remove <class_id>");
        return;
    }
    std::string action = sep->arg[1];
    uint8 cls = static_cast<uint8>(atoi(sep->arg[2]));
    if (cls < 1 || cls > 16) {
        c->Message(Chat::White, "Class id must be 1..16");
        return;
    }
    auto charid = c->CharacterID();
    if (action == "add") {
        auto r = database.QueryDatabase(fmt::format(
            "INSERT IGNORE INTO character_classes (char_id, class_id, is_primary) "
            "VALUES ({}, {}, 0)", charid, cls));
        if (!r.Success()) {
            c->Message(Chat::White, "DB error: %s", r.ErrorMessage().c_str());
            return;
        }
        c->Message(Chat::White, "Added class %u", cls);
        // Refresh runtime multiclass state now, will also emit hydration log
        c->HydrateMulticlassFromDB();
    } else if (action == "remove") {
        auto r = database.QueryDatabase(fmt::format(
            "DELETE FROM character_classes WHERE char_id = {} AND class_id = {}", charid, cls));
        if (!r.Success()) {
            c->Message(Chat::White, "DB error: %s", r.ErrorMessage().c_str());
            return;
        }
        c->Message(Chat::White, "Removed class %u", cls);
        // Refresh runtime multiclass state now, will also emit hydration log
        c->HydrateMulticlassFromDB();
    } else {
        c->Message(Chat::White, "Unknown action '%s'", action.c_str());
    }
}
