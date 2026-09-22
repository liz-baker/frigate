# Animation pose bitstream

Decoded by `src/model/Animation.cpp` (`ReadAnimAngleBits`,
`DecodeJointRotation`, `LerpAngle`). Not yet a packed resource -- this
documents the decomp's runtime algorithm, which #73 packages as a
resource once #70's joint table is available.

## Per-frame layout

Each animation frame is one bit-packed snapshot of every joint's
rotation, back to back with no delimiters. There are no on-disk curves
or keyframes: two adjacent frames are decoded independently, and
interpolation between them happens entirely at runtime (see below).

A joint's rotation is three consecutive `width`-bit quantized angles (X,
Y, Z), where `width` is the clip's angle bit width (the GoldenEye
decomp's `ModelAnimation.unk06`) and the joint's channel index (its
joint table entry's `mtxA`, or `mtxB` when mirrored -- see
`goldeneye-model-tree-resource.md`'s "joint ID table") gives the joint's
X angle's bit offset within the frame as `channel * width`; Y follows at
`+ width`, Z at `+ 2 * width`.

Each `width`-bit value is read most-significant-bit-first and left-
justified into a 16-bit angle (the low `16 - width` bits are zero), then
scaled to radians as `raw * 2*pi / 65536`. Transcribed from the
GoldenEye decomp's `modelAnimReadBitsAsU16Angle`.

## Mirroring

A joint's `mtxB` channel (used when the caller requests the mirrored
side, e.g. reusing a left-arm animation for the right arm) decodes the
same way as `mtxA`, then negates Y and Z through the full circle
(`raw == 0` stays `0`, otherwise `(0x10000 - raw) * 2*pi / 65536`); X is
never negated. Transcribed from the GoldenEye decomp's
`sub_GAME_7F06DEC0`.

## Frame interpolation

The game advances a fractional frame counter and decodes the two
surrounding integer frames independently, then interpolates each axis
with a shortest-path angle lerp (moving through whichever of the two
directions around the circle is shorter, not a direct linear blend,
which would cross the long way around near the 0/2*pi boundary).
Transcribed from the GoldenEye decomp's `sub_GAME_7F06D0CC`.
