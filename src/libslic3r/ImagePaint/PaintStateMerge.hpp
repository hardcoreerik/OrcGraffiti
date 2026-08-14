#pragma once

#include "ImagePaintTypes.hpp"

#include <vector>

namespace Slic3r::ImagePaint {

// How a new projection result is merged with existing per-face paint states.
enum class MergePolicy : std::uint8_t {
    // Only paint faces currently kStateNone; existing colour is left untouched.
    PreserveExisting,
    // Paint every face the projector touches, overwriting whatever was there.
    OverwriteInsideMask,
};

// Merge proposed per-face selector states into current states.
// proposed[i] == kStateNone means "the projection did not touch face i".
// Returns a new vector with the merge result applied.
// If proposed is empty the current states are returned unchanged.
// current and proposed must be the same length when both are non-empty.
std::vector<SelectorState>
merge_paint_states(const std::vector<SelectorState>& current,
                   const std::vector<SelectorState>& proposed,
                   MergePolicy                        policy);

// Build a zero-initialised state vector for a mesh with n faces.
inline std::vector<SelectorState> make_blank_states(std::size_t n)
{
    return std::vector<SelectorState>(n, kStateNone);
}

} // namespace Slic3r::ImagePaint
