# Arc angles use the knob convention, not the math convention

## Status

accepted

## Context

The optional-tier `FlCmd_Arc` payload needs an angle convention.
Graphics and shader code conventionally measures angles from the +x axis,
counter-clockwise positive — anyone reading the IR will assume that. But every
consumer of Arc in this codebase is a knob indicator, modulation ring, or
sector, and knobs are universally specified from 12 o'clock sweeping clockwise
(JUCE, Vital).

## Decision

`FlCmdArc.start_angle` is in radians measured from **12 o'clock**, and a
positive `sweep` is **clockwise**. Widget code does no remapping; the backend
does the one conversion to whatever its shader math wants. Chosen because the
IR carries widget intent, and the widget side is where dozens of call sites
live — the backend conversion is written once.

## Consequences

Hard to reverse once knob/ring widgets bake it in. A future reader porting
shader code that assumes +x/CCW must convert at the IR boundary, not "fix" the
convention.
