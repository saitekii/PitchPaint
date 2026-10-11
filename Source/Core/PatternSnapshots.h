#pragma once

#include "Pattern.h"

#include <atomic>
#include <memory>
#include <vector>

namespace pitchpaint
{

// Hands immutable copies of the pattern from the message thread to the audio thread.
// The audio thread never frees a pattern: every published copy stays referenced from `retained`
// until the message thread sees nobody else holds it.
class PatternSnapshots
{
public:
    explicit PatternSnapshots (Pattern initial = {}) { publish (std::move (initial)); }

    // Message thread.
    void publish (Pattern p)
    {
        auto snapshot = std::make_shared<const Pattern> (std::move (p));
        retained.push_back (snapshot);
        current.store (std::move (snapshot));
        collectGarbage();
    }

    // Message thread: frees snapshots that are no longer current and no longer used by the audio thread.
    void collectGarbage()
    {
        const auto live = current.load();
        std::erase_if (retained, [&] (const auto& s) { return s != live && s.use_count() == 1; });
    }

    // Audio thread. Keep the returned pointer for the duration of the block.
    std::shared_ptr<const Pattern> acquire() const { return current.load(); }

private:
    std::atomic<std::shared_ptr<const Pattern>> current;
    std::vector<std::shared_ptr<const Pattern>> retained;
};

} // namespace pitchpaint
