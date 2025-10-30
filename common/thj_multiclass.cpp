#include "thj_multiclass.h"

#include <mutex>
#include <unordered_map>

namespace {
std::mutex g_mutex;
std::unordered_map<uint32_t, uint32_t> g_maskByCharId;
} // namespace

namespace THJ {

void SetMulticlassMask(uint32_t char_id, uint32_t mask)
{
    std::lock_guard<std::mutex> lock(g_mutex);
    if (mask == 0) {
        g_maskByCharId.erase(char_id);
    } else {
        g_maskByCharId[char_id] = mask;
    }
}

uint32_t GetMulticlassMask(uint32_t char_id)
{
    std::lock_guard<std::mutex> lock(g_mutex);
    auto it = g_maskByCharId.find(char_id);
    return (it != g_maskByCharId.end()) ? it->second : 0u;
}

} // namespace THJ

