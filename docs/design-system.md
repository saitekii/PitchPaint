# PitchPaint design system

All visual values live in [`Source/Theme.h`](../Source/Theme.h). All controls use one look-and-feel,
[`Source/LookAndFeel.cpp`](../Source/LookAndFeel.cpp). Don't hard-code colours or sizes anywhere else.

## Tokens

**Colour roles.** The app has one dark theme, layered canvas < surface < elevated.

| Role | Token | Used for |
|---|---|---|
| Canvas | `canvas` | Drawing area |
| Surface | `surface` | Toolbar, footer, note-name gutter |
| Elevated | `elevated` | Popup menus, tooltips |
| Control | `control`, `controlHover`, `controlPressed` | Button and field fills by state |
| Text | `textPrimary`, `textSecondary`, `textOnAccent` | Content, supporting text, text on accent |
| Border | `border`, `borderControl` | Separators; control outlines |
| Accent | `accent` | The active tool and other primary interactive elements; also text that says a control is editing the selection ("Selected velocity"), so it meets 4.5:1 |
| Focus | `focusRing` | Keyboard focus |
| Selection | `selection` | Outline of selected strokes |
| Warning | `warning` | Synth setup the user must do |
| Strokes | `strokes[]` | Tell strokes apart. They never carry meaning. |

**Spacing:** 4, 8, 12, 16, 24, 32 px (`space::xs` … `space::xxl`).
- 4 px between controls in a group
- 8 px for margins
- 16 px between toolbar groups, with a separator

**Shape:** controls are 28 px tall with a 6 px corner radius. Focus rings are 2 px.

**Type:** one sans-serif face. Sizes: body 14, label 13, caption 12, empty-state title 18 bold. The active tool's
label is bold.

## Layout

- **Toolbar:** groups in reading order: Tools (Pencil, Line, Select, Eraser) | Edit (Undo, Redo, Smooth) | Smoothing |
  Loop + Snap | Key + Scale + Magnet | Velocity | Output + Bend range | Preview. Groups wrap onto
  another row when the window is narrow and never overlap. The default window is 1300 × 640, where everything
  fits in two rows; the minimum is 560 × 460, which keeps the canvas at least ~220 px tall.
- **Canvas:** fills the remaining space. Time zoom/scroll: Ctrl+wheel and Shift+wheel. Pitch zoom/scroll: Alt+wheel and the wheel.
- **Footer:**
  - Left: the persistent synth-setup line, which is never truncated and moves to its own row if needed.
  - Right: feedback after actions for 4 s, otherwise the shortcut reminder.

## States

| Component | States |
|---|---|
| Buttons | Default, hover, pressed, disabled (dimmed and inert), toggled-on (accent fill and bold), keyboard focus (ring) |
| Combo boxes | Default, hover, focus ring |
| Toggle and bend-range field | JUCE defaults coloured by tokens, plus a focus ring |
| Canvas | Empty state; transport chip ("Stopped - press Play in your DAW" / "Playing", with a square or triangle icon); focus ring when focused from the keyboard |
| Strokes | Line thickness shows velocity (1–4 px); selected: white outline, slightly thicker, and a "vel N" tag; eraser circle under the mouse |
| Velocity field | Edits new-stroke velocity, or the selection's (label reads *Selected velocity* in the accent colour). A drag or a ± press is one undo step. |

## Feedback

- **Footer and screen readers:** destructive edits report what happened and how to undo it, e.g.
  "Deleted 2 strokes. Ctrl+Z to undo." Loop changes, undo and redo are confirmed the same way. Each message is
  also announced to screen readers.
- **No confirmation dialogs:** every edit can be undone.

## Accessibility

- **Contrast:** checked against WCAG 2.2 AA in `EditorTests`. Text is at least 4.5:1 on every surface it appears
  on. Control borders, the focus ring, the accent, the playhead and stroke colours are at least 3:1.
- **Keyboard:**
  - Tab reaches every toolbar control in reading order, then the canvas.
  - Clicking a toolbar control doesn't steal focus, so focus rings appear for keyboard use only.
  - Shortcuts: P / L / S / E for tools, G for snap on/off, M to smooth the selection, Enter / Esc to finish / cancel a line, Ctrl+Z / Ctrl+Y, Ctrl+A, Delete, Esc.
  - Arrow keys move the selection (Alt for fine steps).
- **Screen readers:** every control has an accessible name. Drop-downs and the bend-range field are linked to
  their visible labels. The canvas has a title, help text, and a live description such as "3 strokes in a 4-bar
  loop. 1 selected."
- **Tooltips** explain each control. They only add detail; nothing essential is tooltip-only.
- **Motion:** none, so there is nothing to reduce for reduced-motion settings.

## Documented exceptions

- **Canvas grid lines** have low contrast on purpose, so the drawn strokes stay the focal point. Octave lines and
  note labels are higher contrast, and Shift-snapping gives exact semitones without reading the grid.
- **Pitch magnet near midpoints.** The magnet is continuous (no jumps), so a pitch exactly halfway between two
  scale notes stays there and small movements near it are amplified. Use Shift-snap for such notes.
- **Drawing needs a pointer.** Free-hand curves have no sensible keyboard equivalent. Editing existing strokes
  (select all, move, delete, undo) works from the keyboard.
- **Control height is 28 px**, below the ~44 px touch guidance. PitchPaint is a desktop plugin used with a mouse,
  and DAW plugin windows are space-constrained.
- **English only.** No right-to-left layout or localisation yet. Text is never assumed to have a fixed length:
  label widths are measured, and the footer reflows.
