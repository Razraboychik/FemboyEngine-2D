# Editor UX Verification Checklist

This checklist turns editor usability into a repeatable pass. It covers the
authoring surface that unit tests cannot judge: focus, keyboard reachability,
layout persistence, DPI, error feedback, undo boundaries and PIE transitions.

Run it before a release candidate on Windows at 100%, 150% and 200% display
scale, and once with a second monitor. Record every failure with the panel name,
the exact action and the observed result.

## 1. Global shell

| Check | Pass condition |
|---|---|
| Launch with no project | Welcome or project picker is usable; closing it leaves a recoverable window |
| Open a project | Dock layout restores; no panel overlaps or zero-size panel |
| Close and reopen | Window size, dock layout and panel visibility survive |
| Keyboard-only navigation | Every menu and dialog is reachable without the mouse |
| Focus after modal close | Focus returns to the control that opened the modal |
| Second monitor | Panels can move between monitors without losing scale or input |
| Minimise and restore | Rendering resumes, no device loss, no duplicated input |

## 2. Panel pass

Open each panel, then close and reopen it. The panel must keep a useful default
size and must not steal focus from the viewport.

| Area | Checks |
|---|---|
| Hierarchy | Create, rename, reparent, delete; multi-select; search; drag to viewport |
| Inspector | Every component collapses and expands; edits undo as one action; invalid values show a readable message |
| Content Browser | Navigate folders, reimport an asset, rename and move; missing asset shows an error instead of a blank tile |
| Viewport | Orbit/pan/zoom; gizmo modes; selection outline; resize to a very small and a very large size |
| Console | Filter by level and category; clear; copy a line; a long line wraps without hiding controls |
| Profiler and Stats | Start/stop; values update; no allocation spike while the panel is open |
| Material Graph | Create nodes, connect, disconnect, undo, redo, save and reload |
| Animation Graph | Add a state, add a transition, delete a node, compile, inspect the error |
| AI State Tree | Add leaf and selector, connect, delete subtree, compile, save and reload |
| Project Settings | Change a value, cancel, reopen; the old value returns; commit persists |
| Text and font panels | Missing glyph, long text, RTL sample and a large pixel size remain usable |

## 3. Undo and redo

For every mutating action, press undo and redo once.

The check passes when:

- the document returns to the exact pre-action state;
- the redo restores the same identifiers;
- selection is either preserved or restored to a sensible object;
- a cancelled dialog creates no undo entry;
- a multi-step drag creates one undo entry, not one per frame.

## 4. PIE

| Check | Pass condition |
|---|---|
| Start PIE | Editor state is unchanged; the simulation starts from the authored scene |
| Stop PIE | Authored scene returns; no duplicate entities or leaked components |
| Start and stop ten times | Memory and entity count stay flat |
| Edit during PIE | The editor refuses or clearly queues the change; no silent corruption |
| Stop with an error | The error is visible and the editor remains usable |

## 5. Error and empty states

- Every empty list has an explanation and a next action.
- Every import failure names the file, the line or field when available, and the
  reason.
- A failed save leaves the previous file intact.
- A missing asset, font or texture renders a placeholder and logs one error, not
  one error per frame.
- A long path or a non-ASCII path is displayed without clipping controls.

## 6. Visual and accessibility pass

- Text contrast remains readable in every theme.
- Focus rings are visible on every interactive control.
- Tab order follows the visual order.
- Tooltips do not cover the control they describe.
- The layout survives font scaling and a high-DPI monitor.
- No panel relies on colour alone to convey an error.

## 7. Sign-off

Record the build version, display scale, GPU and the list of skipped checks. A
release candidate is accepted only when every skipped check has an explicit
reason and every failure has a tracked owner.
