#pragma once

#include <cstdint>

namespace THJ {

// Record the current multiclass mask for a character id.
void SetMulticlassMask(uint32_t char_id, uint32_t mask);

// Retrieve the cached mask for a character id (0 if unknown).
uint32_t GetMulticlassMask(uint32_t char_id);

} // namespace THJ

