// Offscreen GUI test: drives the real editor with synthetic mouse events, checks the pattern and the MIDI it
// produces, and writes a snapshot PNG of the editor (path given as the first argument, optional).
#include "PluginEditor.h"
#include "PluginProcessor.h"

#include <cstdio>

namespace
{
int failures = 0;

#define CHECK(cond)                                                              \
    do {                                                                         \
        if (! (cond)) {                                                          \
            std::printf ("    FAILED %s:%d: %s\n", __FILE__, __LINE__, #cond);   \
            ++failures;                                                          \
        }                                                                        \
    } while (false)

struct FakePlayHead final : juce::AudioPlayHead
{
    double ppq = 0.0;
    bool playing = true;

    juce::Optional<PositionInfo> getPosition() const override
    {
        PositionInfo info;
        info.setIsPlaying (playing);
        info.setPpqPosition (ppq);
        info.setBpm (120.0);
        return info;
    }
};

juce::MouseEvent makeEvent (juce::Component& c, juce::Point<float> pos, juce::Point<float> downPos, juce::ModifierKeys mods)
{
    const auto now = juce::Time::getCurrentTime();
    return { juce::Desktop::getInstance().getMainMouseSource(), pos, mods, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f,
             &c, &c, now, downPos, now, 1, pos != downPos };
}

void drag (juce::Component& c, std::vector<juce::Point<float>> path, juce::ModifierKeys mods = juce::ModifierKeys::leftButtonModifier)
{
    c.mouseDown (makeEvent (c, path.front(), path.front(), mods));
    for (size_t i = 1; i < path.size(); ++i)
        c.mouseDrag (makeEvent (c, path[i], path.front(), mods));
    c.mouseUp (makeEvent (c, path.back(), path.front(), mods));
}

// Pixel position of (beat, pitch) on a canvas of the given size, mirroring PitchCanvas's default view (36..96).
juce::Point<float> at (juce::Component& canvas, double lengthBeats, double beat, double pitch)
{
    const float gutter = 40.0f, w = (float) canvas.getWidth() - gutter, h = (float) canvas.getHeight();
    return { gutter + (float) (beat / lengthBeats) * w, (float) ((96.0 - pitch) / 60.0) * h };
}

struct NoteOn
{
    int channel, note;
};

std::vector<NoteOn> playBeats (PitchPaintProcessor& proc, FakePlayHead& head, double fromBeat, double toBeat)
{
    std::vector<NoteOn> ons;
    juce::AudioBuffer<float> audio (2, 512);
    juce::MidiBuffer midi;
    head.ppq = fromBeat;
    head.playing = true;
    while (head.ppq < toBeat)
    {
        proc.processBlock (audio, midi);
        for (const auto m : midi)
            if (m.getMessage().isNoteOn())
                ons.push_back ({ m.getMessage().getChannel(), m.getMessage().getNoteNumber() });
        head.ppq += 512 * 2.0 / 48000.0;
    }
    return ons;
}
} // namespace

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI juceInit;

    PitchPaintProcessor proc;
    FakePlayHead head;
    proc.setPlayHead (&head);
    proc.prepareToPlay (48000.0, 512);

    std::unique_ptr<juce::AudioProcessorEditor> editor (proc.createEditor());
    editor->setSize (1000, 560);

    PitchCanvas* canvas = nullptr;
    for (auto* child : editor->getChildren())
        if (auto* c = dynamic_cast<PitchCanvas*> (child))
            canvas = c;
    CHECK (canvas != nullptr);
    if (canvas == nullptr)
        return 1;

    // Start from an empty 4-bar loop.
    auto empty = proc.getPattern();
    empty.trajectories.clear();
    empty.lengthBeats = 16.0;
    proc.setPattern (empty);
    const double L = 16.0;

    const auto saveSnapshot = [&] (int argIndex)
    {
        if (argc <= argIndex)
            return;
        juce::MessageManager::getInstance()->runDispatchLoopUntil (50); // let timers refresh labels
        const auto snapshot = editor->createComponentSnapshot (editor->getLocalBounds());
        juce::FileOutputStream stream (juce::File::getCurrentWorkingDirectory().getChildFile (argv[argIndex]));
        stream.setPosition (0);
        stream.truncate();
        juce::PNGImageFormat().writeImageToStream (snapshot, stream);
    };

    // 0. Design-system checks: WCAG AA contrast for text (4.5:1) and for UI parts and strokes (3:1).
    {
        namespace c = theme::colour;
        for (const auto bg : { c::surface, c::canvas, c::control, c::controlHover, c::elevated })
        {
            CHECK (theme::contrastRatio (c::textPrimary, bg) >= 4.5);
            CHECK (theme::contrastRatio (c::textSecondary, bg) >= 4.5);
        }
        CHECK (theme::contrastRatio (c::warning, c::surface) >= 4.5);
        CHECK (theme::contrastRatio (c::textOnAccent, c::accent) >= 4.5);
        CHECK (theme::contrastRatio (c::borderControl, c::surface) >= 3.0);
        CHECK (theme::contrastRatio (c::focusRing, c::surface) >= 3.0 && theme::contrastRatio (c::focusRing, c::canvas) >= 3.0);
        CHECK (theme::contrastRatio (c::accent, c::surface) >= 3.0);
        CHECK (theme::contrastRatio (c::playhead, c::canvas) >= 3.0);
        for (const auto s : c::strokes)
            CHECK (theme::contrastRatio (s, c::canvas) >= 3.0);
        std::printf ("[info] contrast: secondary text on surface %.2f, control border %.2f, focus ring %.2f\n",
                     theme::contrastRatio (c::textSecondary, c::surface), theme::contrastRatio (c::borderControl, c::surface),
                     theme::contrastRatio (c::focusRing, c::surface));

        // Every interactive control has an accessible name.
        for (auto* child : editor->getChildren())
            if (child->getWantsKeyboardFocus() || dynamic_cast<juce::Button*> (child) != nullptr)
            {
                // Offscreen there are no accessibility handlers, so check the source of the name: an explicit title,
                // or a button's text (which JUCE's button accessibility uses as its name).
                const auto* button = dynamic_cast<juce::Button*> (child);
                const bool named = child->getTitle().isNotEmpty() || (button != nullptr && button->getButtonText().isNotEmpty());
                CHECK (named);
                if (! named)
                    std::printf ("    unnamed control: %s\n", typeid (*child).name());
            }
    }

    // Empty state on first use.
    saveSnapshot (4);

    // 1. Draw a rising glide from beat 1 (C4) to beat 5 (E4 + 30 cents), then a separate, overlapping wiggle.
    std::vector<juce::Point<float>> glide;
    for (int i = 0; i <= 40; ++i)
        glide.push_back (at (*canvas, L, 1.0 + i * 0.1, 60.0 + i * 0.1 * 4.3 / 4.0));
    drag (*canvas, glide);

    std::vector<juce::Point<float>> wiggle;
    for (int i = 0; i <= 60; ++i)
        wiggle.push_back (at (*canvas, L, 3.0 + i * 0.1, 72.0 + 2.0 * std::sin (i * 0.3)));
    drag (*canvas, wiggle);

    // A click without dragging must not leave a trajectory behind.
    drag (*canvas, { at (*canvas, L, 12.0, 80.0) });

    // Shift snaps to semitones.
    drag (*canvas, { at (*canvas, L, 10.0, 64.3), at (*canvas, L, 11.0, 64.4), at (*canvas, L, 12.0, 65.2) },
          juce::ModifierKeys (juce::ModifierKeys::leftButtonModifier | juce::ModifierKeys::shiftModifier));

    const auto& p = proc.getPattern();
    CHECK (p.trajectories.size() == 3);
    if (p.trajectories.size() == 3)
    {
        const auto& g = p.trajectories[0];
        CHECK (std::abs (g.startBeat() - 1.0) < 0.02 && std::abs (g.endBeat() - 5.0) < 0.02);
        CHECK (std::abs (g.pitchAt (3.0) - (60.0 + 2.0 * 4.3 / 4.0)) < 0.15); // continuous, not snapped
        const auto& s = p.trajectories[2];
        CHECK (s.pitchAt (10.5) == 64.0 && s.pitchAt (12.5) == 65.0);
        std::printf ("[info] glide pitch at beat 3 = %.3f (expected %.3f)\n", g.pitchAt (3.0), 60.0 + 2.0 * 4.3 / 4.0);
    }

    // 2. Playing it produces two overlapping MPE notes on different channels, then the snapped one.
    const auto ons = playBeats (proc, head, 0.0, 13.0);
    CHECK (ons.size() == 3);
    if (ons.size() == 3)
    {
        CHECK (ons[0].channel != ons[1].channel);
        CHECK (ons[0].channel >= 2 && ons[1].channel >= 2);
        std::printf ("[info] note-ons: ch%d n%d, ch%d n%d, ch%d n%d\n", ons[0].channel, ons[0].note, ons[1].channel,
                     ons[1].note, ons[2].channel, ons[2].note);
    }

    // Leave the transport mid-loop so the snapshot shows the playhead and sounding dots,
    // and let the editor's timer pick up the pattern change (the 4-bar loop set above).
    playBeats (proc, head, 3.9, 4.0);
    juce::MessageManager::getInstance()->runDispatchLoopUntil (200);
    for (auto* child : editor->getChildren())
        if (auto* box = dynamic_cast<juce::ComboBox*> (child); box != nullptr && box->getText().endsWith ("bars"))
            CHECK (box->getText() == "4 bars");

    if (argc > 1)
    {
        const auto snapshot = editor->createComponentSnapshot (editor->getLocalBounds());
        const juce::File out { juce::File::getCurrentWorkingDirectory().getChildFile (argv[1]) };
        out.deleteFile();
        juce::FileOutputStream stream (out);
        juce::PNGImageFormat().writeImageToStream (snapshot, stream);
        std::printf ("[info] wrote %s\n", argv[1]);
    }

    // 3. Shift+right-drag across the wiggle deletes the whole stroke, and only it.
    drag (*canvas, { at (*canvas, L, 6.0, 69.0), at (*canvas, L, 6.0, 76.0) },
          juce::ModifierKeys (juce::ModifierKeys::rightButtonModifier | juce::ModifierKeys::shiftModifier));
    CHECK (proc.getPattern().trajectories.size() == 2);

    // 4. Undo brings the erased stroke back in one step (the whole swipe is one gesture); redo removes it again.
    const auto press = [&] (juce::KeyPress k) { return editor->keyPressed (k); };
    const auto ctrl = [] (char c) { return juce::KeyPress (c, juce::ModifierKeys::commandModifier, 0); };
    CHECK (press (ctrl ('z')) && proc.getPattern().trajectories.size() == 3);
    CHECK (press (ctrl ('y')) && proc.getPattern().trajectories.size() == 2);

    // Drawing a stroke (many drag events) is also a single undo step.
    const auto beforeStroke = proc.getPattern();
    drag (*canvas, { at (*canvas, L, 13.0, 50.0), at (*canvas, L, 13.5, 51.0), at (*canvas, L, 14.0, 50.0), at (*canvas, L, 15.0, 52.0) });
    CHECK (proc.getPattern().trajectories.size() == 3);
    proc.undo();
    CHECK (proc.getPattern() == beforeStroke);

    // 5. Select tool: click the glide, Shift-drag it +1 beat and ~+2.3 semitones -> moves exactly +2 semitones.
    CHECK (press (juce::KeyPress ('s')));
    const auto glideBefore = proc.getPattern().trajectories[0];
    const double grabPitch = glideBefore.pitchAt (3.0);
    drag (*canvas, { at (*canvas, L, 3.0, grabPitch), at (*canvas, L, 3.5, grabPitch + 1.0), at (*canvas, L, 4.0, grabPitch + 2.3) },
          juce::ModifierKeys (juce::ModifierKeys::leftButtonModifier | juce::ModifierKeys::shiftModifier));
    {
        const auto& g = proc.getPattern().trajectories[0];
        CHECK (std::abs (g.startBeat() - (glideBefore.startBeat() + 1.0)) < 0.02);
        CHECK (std::abs (g.points[0].pitch - (glideBefore.points[0].pitch + 2.0)) < 1e-9); // whole semitones
        CHECK (canvas->getSelection() == std::vector<std::uint32_t> { g.id });
    }

    // 6. Box-select the snapped stroke (beats 10..12 around E4) from empty space, delete it with the Delete key.
    const auto snappedId = proc.getPattern().trajectories.back().id;
    {
        // Done by hand (instead of drag()) so a snapshot can show the selection box mid-drag.
        const auto start = at (*canvas, L, 9.5, 68.0), end = at (*canvas, L, 12.5, 61.0);
        canvas->mouseDown (makeEvent (*canvas, start, start, juce::ModifierKeys::leftButtonModifier));
        canvas->mouseDrag (makeEvent (*canvas, end, start, juce::ModifierKeys::leftButtonModifier));
        if (argc > 2)
        {
            const auto snapshot = editor->createComponentSnapshot (editor->getLocalBounds());
            juce::FileOutputStream stream (juce::File::getCurrentWorkingDirectory().getChildFile (argv[2]));
            stream.setPosition (0);
            stream.truncate();
            juce::PNGImageFormat().writeImageToStream (snapshot, stream);
        }
        canvas->mouseUp (makeEvent (*canvas, end, start, juce::ModifierKeys::leftButtonModifier));
    }
    CHECK (canvas->getSelection() == std::vector<std::uint32_t> { snappedId });
    CHECK (press (juce::KeyPress (juce::KeyPress::deleteKey)));
    CHECK (proc.getPattern().trajectories.size() == 1 && canvas->getSelection().empty());
    proc.undo();
    CHECK (proc.getPattern().trajectories.size() == 2);

    // 7. State round trip: pattern and parameters come back exactly in a fresh instance.
    proc.params.getParameter ("bendRange")->setValueNotifyingHost (proc.params.getParameter ("bendRange")->convertTo0to1 (24.0f));
    juce::MemoryBlock state;
    proc.getStateInformation (state);
    {
        PitchPaintProcessor restored;
        restored.setStateInformation (state.getData(), (int) state.getSize());
        CHECK (restored.getPattern() == proc.getPattern());
        CHECK ((int) restored.params.getRawParameterValue ("bendRange")->load() == 24);
        CHECK (! restored.canUndo());
        std::printf ("[info] state: %d bytes, %d trajectories\n", (int) state.getSize(), (int) restored.getPattern().trajectories.size());
    }
    {
        PitchPaintProcessor fresh; // new instances start empty
        CHECK (fresh.getPattern().trajectories.empty());
        fresh.setStateInformation ("garbage", 7); // malformed state is ignored
        CHECK (fresh.getPattern().trajectories.empty());
    }

    // 8. Loop length change is one undo step.
    {
        auto changed = proc.getPattern();
        changed.lengthBeats = 4.0;
        proc.applyEdit (changed);
        CHECK (proc.getPattern().lengthBeats == 4.0);
        proc.undo();
        CHECK (proc.getPattern().lengthBeats == 16.0);
    }

    // 9. Split eraser: a vertical right-drag through the middle of the glide cuts out the span under the eraser
    //    circle (8 px each side), leaving two strokes; the left part keeps the id. One undo step restores it.
    {
        const auto before = proc.getPattern();
        const auto original = before.trajectories[0];
        const double cutAt = 4.0, cutPitch = original.pitchAt (cutAt);
        drag (*canvas, { at (*canvas, L, cutAt, cutPitch - 3.0), at (*canvas, L, cutAt, cutPitch + 3.0) },
              juce::ModifierKeys (juce::ModifierKeys::rightButtonModifier));

        const auto& after = proc.getPattern().trajectories;
        CHECK (after.size() == before.trajectories.size() + 1);
        if (after.size() == before.trajectories.size() + 1)
        {
            const double halfGap = 8.0 / (canvas->getWidth() - 40.0) * L; // eraser radius in beats
            const auto& left = after[0];
            const auto& right = after[1];
            CHECK (left.id == original.id && right.id != original.id);
            CHECK (std::abs (left.endBeat() - (cutAt - halfGap)) < 0.02);
            CHECK (std::abs (right.startBeat() - (cutAt + halfGap)) < 0.02);
            CHECK (std::abs (left.points.back().pitch - original.pitchAt (left.endBeat())) < 1e-9); // exact cut, no jump
            std::printf ("[info] split: left ends %.3f, right starts %.3f (expected %.3f / %.3f)\n", left.endBeat(),
                         right.startBeat(), cutAt - halfGap, cutAt + halfGap);
        }
        proc.undo();
        CHECK (proc.getPattern() == before);
    }

    // 10. Horizontal zoom: Ctrl+wheel zooms time around the mouse, Shift+wheel scrolls, drawing lands where it should.
    {
        const auto wheelAt = [&] (double beat, float deltaY, juce::ModifierKeys mods)
        {
            const auto pos = at (*canvas, L, beat, 60.0);
            canvas->mouseWheelMove (makeEvent (*canvas, pos, pos, mods), juce::MouseWheelDetails { 0.0f, deltaY, false, false, false });
        };
        const auto ctrlMods = juce::ModifierKeys (juce::ModifierKeys::ctrlModifier);

        // at() maps with the whole-loop view, which matches the canvas only before the first zoom step;
        // after it, beat 8 (the centre) stays at the same pixel, so zooming twice there stays anchored on beat 8.
        wheelAt (8.0, 0.5, ctrlMods);
        wheelAt (8.0, 0.5, ctrlMods);
        const auto view = canvas->getVisibleBeats();
        CHECK (view.getLength() < 10.0 && view.contains (8.0));
        std::printf ("[info] zoomed view: %.3f .. %.3f\n", view.getStart(), view.getEnd());

        // Draw in the zoomed view: positions must map through the visible range, not the whole loop.
        const auto atZoomed = [&] (double beat, double pitch)
        {
            const float w = (float) canvas->getWidth() - 40.0f, h = (float) canvas->getHeight();
            return juce::Point<float> { 40.0f + (float) ((beat - view.getStart()) / view.getLength()) * w, (float) ((96.0 - pitch) / 60.0) * h };
        };
        CHECK (press (juce::KeyPress ('p')));
        drag (*canvas, { atZoomed (6.0, 48.0), atZoomed (6.5, 49.0), atZoomed (7.0, 48.0) });
        const auto& drawn = proc.getPattern().trajectories.back();
        CHECK (std::abs (drawn.startBeat() - 6.0) < 0.01 && std::abs (drawn.endBeat() - 7.0) < 0.01);
        CHECK (std::abs (drawn.pitchAt (6.5) - 49.0) < 0.1);

        if (argc > 3)
        {
            const auto snapshot = editor->createComponentSnapshot (editor->getLocalBounds());
            juce::FileOutputStream stream (juce::File::getCurrentWorkingDirectory().getChildFile (argv[3]));
            stream.setPosition (0);
            stream.truncate();
            juce::PNGImageFormat().writeImageToStream (snapshot, stream);
        }

        // Shift+wheel scrolls time by a quarter of the visible span; zooming far out shows the whole loop again.
        const double startBefore = view.getStart();
        wheelAt (8.0, -0.5, juce::ModifierKeys (juce::ModifierKeys::shiftModifier));
        CHECK (std::abs (canvas->getVisibleBeats().getStart() - (startBefore + 0.5 * view.getLength() * 0.25)) < 1e-9);
        for (int i = 0; i < 10; ++i)
            wheelAt (8.0, -0.5, ctrlMods);
        CHECK (canvas->getVisibleBeats() == juce::Range<double> (0.0, L));
    }

    // 11. Feedback and keyboard editing: arrow keys move the selection (one undo step each); deleting reports what
    //     happened in the footer; undo is announced too.
    {
        const auto footerText = [&]
        {
            juce::String text;
            for (auto* child : editor->getChildren())
                if (auto* label = dynamic_cast<juce::Label*> (child); label != nullptr && label->getText().endsWith ("."))
                    text = label->getText();
            return text;
        };

        CHECK (press (juce::KeyPress ('s')));
        CHECK (press (ctrl ('a')));
        const auto before = proc.getPattern();
        CHECK (press (juce::KeyPress (juce::KeyPress::upKey)));
        CHECK (std::abs (proc.getPattern().trajectories[0].points[0].pitch - (before.trajectories[0].points[0].pitch + 1.0)) < 1e-9);
        CHECK (press (juce::KeyPress (juce::KeyPress::rightKey, juce::ModifierKeys::altModifier, 0)));
        CHECK (std::abs (proc.getPattern().trajectories[0].startBeat() - (before.trajectories[0].startBeat() + 1.0 / 32.0)) < 1e-9);
        proc.undo();
        proc.undo();
        CHECK (proc.getPattern() == before);

        CHECK (press (ctrl ('a')));
        CHECK (press (juce::KeyPress (juce::KeyPress::deleteKey)));
        CHECK (proc.getPattern().trajectories.empty());
        CHECK (footerText().startsWith ("Deleted " + juce::String ((int) before.trajectories.size()) + " strokes"));
        std::printf ("[info] footer after delete: \"%s\"\n", footerText().toRawUTF8());
        CHECK (press (ctrl ('z')));
        CHECK (proc.getPattern() == before && footerText() == "Undone.");
    }

    // 12. Narrow window: the toolbar wraps instead of overlapping, everything stays inside, the canvas keeps space.
    {
        editor->setSize (560, 400);
        juce::MessageManager::getInstance()->runDispatchLoopUntil (50);
        std::vector<juce::Rectangle<int>> boxes;
        for (auto* child : editor->getChildren())
            if (child->isVisible() && child != canvas && dynamic_cast<juce::Label*> (child) == nullptr && child->getY() < canvas->getY())
                boxes.push_back (child->getBounds());
        bool overlap = false, outside = false;
        for (size_t i = 0; i < boxes.size(); ++i)
        {
            outside |= boxes[i].getRight() > editor->getWidth();
            for (size_t j = i + 1; j < boxes.size(); ++j)
                overlap |= boxes[i].intersects (boxes[j]);
        }
        CHECK (! overlap && ! outside);
        CHECK (canvas->getHeight() > 200);
        std::printf ("[info] narrow: %d toolbar controls, canvas %dx%d\n", (int) boxes.size(), canvas->getWidth(), canvas->getHeight());
        saveSnapshot (5);
    }

    proc.setPlayHead (nullptr);
    editor.reset();

    std::printf ("\n%s (%d failure%s)\n", failures == 0 ? "ALL PASSED" : "FAILURES", failures, failures == 1 ? "" : "s");
    return failures == 0 ? 0 : 1;
}
