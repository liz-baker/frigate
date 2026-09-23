// libultra `gu` matrix helpers the front-end screens call directly.
// libultraship declares these (libultra/gu.h) but doesn't implement them.
// Matrices are row-vector, as on the N64: translation lives in row 3.
#pragma once

#include "libultraship/libultra/gbi.h"

namespace Frigate::Gu {

using MatrixF = float[4][4];

// guLookAtReflectF(): fills `lookAt` with the view's right and up vectors
// (reflection-mapping coefficients for G_TEXTURE_GEN models). The look-at
// matrix it also builds is unused by every caller and not returned.
void LookAtReflect(LookAt& lookAt, float xEye, float yEye, float zEye, float xAt, float yAt, float zAt, float xUp,
                   float yUp, float zUp);

// guLookAtF().
void LookAtF(MatrixF& mf, float xEye, float yEye, float zEye, float xAt, float yAt, float zAt, float xUp, float yUp,
             float zUp);

// guPerspectiveF(), for a vertical field of view in degrees.
void PerspectiveF(MatrixF& mf, float fovyDegrees, float aspect, float near, float far, float scale);

// guRotateF(): `angleDegrees` about the axis (x, y, z).
void RotateF(MatrixF& mf, float angleDegrees, float x, float y, float z);

// guTranslateF() / guScaleF().
void TranslateF(MatrixF& mf, float x, float y, float z);
void ScaleF(MatrixF& mf, float x, float y, float z);

// guOrthoF().
void OrthoF(MatrixF& mf, float l, float r, float b, float t, float n, float f, float scale);

} // namespace Frigate::Gu
