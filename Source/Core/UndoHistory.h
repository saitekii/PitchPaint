#pragma once

#include "Pattern.h"

#include <deque>
#include <optional>

namespace pitchpaint
{

// Whole-pattern undo/redo. Patterns are small, so storing copies is simpler than recording operations.
class UndoHistory
{
public:
    static constexpr size_t maxSteps = 200;

    // Call after an edit, with the pattern as it was before it. Clears the redo list.
    void record (Pattern before)
    {
        undoStack.push_back (std::move (before));
        if (undoStack.size() > maxSteps)
            undoStack.pop_front();
        redoStack.clear();
    }

    bool canUndo() const { return ! undoStack.empty(); }
    bool canRedo() const { return ! redoStack.empty(); }

    // Returns the pattern to switch to; `current` becomes the redo (or undo) target.
    std::optional<Pattern> undo (Pattern current) { return step (undoStack, redoStack, std::move (current)); }
    std::optional<Pattern> redo (Pattern current) { return step (redoStack, undoStack, std::move (current)); }

    void clear()
    {
        undoStack.clear();
        redoStack.clear();
    }

private:
    static std::optional<Pattern> step (std::deque<Pattern>& from, std::deque<Pattern>& to, Pattern current)
    {
        if (from.empty())
            return std::nullopt;
        auto target = std::move (from.back());
        from.pop_back();
        to.push_back (std::move (current));
        return target;
    }

    std::deque<Pattern> undoStack, redoStack;
};

} // namespace pitchpaint
