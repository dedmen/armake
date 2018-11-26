/*
 * Copyright (C)  2016  Felix "KoffeinFlummi" Wiegand
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */


#pragma once

#include "vector.h"


struct matrix {
    float m00, m01, m02;
    float m10, m11, m12;
    float m20, m21, m22;
};


static const matrix empty_matrix = {
    0.0f, 0.0f, 0.0f,
    0.0f, 0.0f, 0.0f,
    0.0f, 0.0f, 0.0f 
};
static const matrix identity_matrix = {
    1.0f, 0.0f, 0.0f, 
    0.0f, 1.0f, 0.0f,
    0.0f, 0.0f, 1.0f 
};

struct matrix4 {
    float m00, m01, m02;
    float m10, m11, m12;
    float m20, m21, m22;
    float m30, m31, m32;
};


static const matrix4 empty_matrix4 = { 
    0.0f, 0.0f, 0.0f,
    0.0f, 0.0f, 0.0f,
    0.0f, 0.0f, 0.0f, 
    0.0f, 0.0f, 0.0f 
};

static const matrix4 identity_matrix4 = { 
    1.0f, 0.0f, 0.0f, 
    0.0f, 1.0f, 0.0f, 
    0.0f, 0.0f, 1.0f,
    0.0f, 0.0f, 0.0f
};



matrix vector_tilda(const vector3 v);

matrix matrix_sub(const matrix m1, const matrix m2);

matrix matrix_mult(const matrix m1, const matrix m2);

matrix matrix_mult_scalar(const float s, const matrix m);

matrix matrix_inverse(const matrix m);
