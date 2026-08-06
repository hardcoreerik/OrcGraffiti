#include "PaintStateMerge.hpp"

#include <cassert>

namespace Slic3r::ImagePaint {

std::vector<SelectorState>
merge_paint_states(const std::vector<SelectorState>& current,
                   const std::vector<SelectorState>& proposed,
                   MergePolicy                        policy)
{
    if (proposed.empty())
        return current;

    assert(current.size() == proposed.size());

    std::vector<SelectorState> result = current;

    switch (policy) {
    case MergePolicy::PreserveExisting:
        for (std::size_t i = 0; i < result.size(); ++i) {
            if (result[i] == kStateNone && proposed[i] != kStateNone)
                result[i] = proposed[i];
        }
        break;

    case MergePolicy::OverwriteInsideMask:
        for (std::size_t i = 0; i < result.size(); ++i) {
            if (proposed[i] != kStateNone)
                result[i] = proposed[i];
        }
        break;
    }

    return result;
}

} // namespace Slic3r::ImagePaint
