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


template<typename T>
class vector3_base {
public:

    T x;
    T y;
    T z;

    vector3_base() noexcept
    {
        x = 0;
        y = 0;
        z = 0;
    }

    vector3_base(const T x_, const T y_, const T z_) noexcept
    {
        x = x_;
        y = y_;
        z = z_;
    }

    explicit vector3_base(const float *buffer) noexcept {
        x = buffer[0];
        y = buffer[1];
        z = buffer[2];
    }

    constexpr vector3_base(const vector3_base& copy_) noexcept {
        x = copy_.x;
        y = copy_.y;
        z = copy_.z;
    }

    constexpr vector3_base& operator= (const vector3_base& other) noexcept { x = other.x; y = other.y; z = other.z; return *this; }
    constexpr vector3_base operator * (const T& val) const noexcept { return vector3_base(x * val, y * val, z * val); }
    constexpr vector3_base operator / (const T& val) const noexcept { T invVal = T(1) / val; return vector3_base(x * invVal, y * invVal, z * invVal); }
    constexpr vector3_base operator + (const vector3_base& v) const noexcept { return vector3_base(x + v.x, y + v.y, z + v.z); }
    constexpr vector3_base operator / (const vector3_base& v) const noexcept { return vector3_base(x / v.x, y / v.y, z / v.z); }
    constexpr vector3_base operator * (const vector3_base& v) const noexcept { return vector3_base(x * v.x, y * v.y, z * v.z); }
    constexpr vector3_base operator - (const vector3_base& v) const noexcept { return vector3_base(x - v.x, y - v.y, z - v.z); }
    constexpr vector3_base operator - () const noexcept { return vector3_base(-x, -y, -z); }

    constexpr vector3_base& operator *=(const vector3_base& v) noexcept { x *= v.x; y *= v.y; z *= v.z; return *this; }
    constexpr vector3_base& operator *=(T mag) noexcept { x *= mag; y *= mag; z *= mag; return *this; }
    constexpr vector3_base& operator /=(const vector3_base& v) noexcept { x /= v.x; y /= v.y; z /= v.z; return *this; }
    constexpr vector3_base& operator /=(T mag) noexcept { x /= mag; y /= mag; y /= mag; return *this; }
    constexpr vector3_base& operator +=(const vector3_base& v) noexcept { x += v.x; y += v.y; z += v.z; return *this; }
    constexpr vector3_base& operator -=(const vector3_base& v) noexcept { x -= v.x; y -= v.y; z -= v.z; return *this; }

    constexpr bool operator == (const vector3_base& r) const noexcept { return (x == r.x && y == r.y && z == r.z); }
    constexpr bool operator >  (const vector3_base& r) const noexcept { if (*this == r) return false; return magnitude_squared() > r.magnitude_squared(); }
    constexpr bool operator <  (const vector3_base& r) const noexcept { if (*this == r) return false; return magnitude_squared() < r.magnitude_squared(); }
    constexpr bool operator >= (const vector3_base& r) const noexcept { if (*this == r) return true; return magnitude_squared() > r.magnitude_squared(); }
    constexpr bool operator <= (const vector3_base& r) const noexcept { if (*this == r) return true; return magnitude_squared() < r.magnitude_squared(); }

    constexpr T magnitude() const noexcept { return std::sqrt(x * x + y * y + z * z); }
    constexpr T magnitude_squared() const noexcept { return x * x + y * y + z * z; }
    constexpr T dot(const vector3_base& v) const noexcept { return (x * v.x + y * v.y + z * v.z); }
    constexpr T distance(const vector3_base& v) const noexcept { vector3_base dist = (*this - v); dist = dist * dist; return std::sqrt(dist.x + dist.y + dist.z); }
    constexpr T distance_squared(const vector3_base& v) const noexcept { vector3_base dist = (*this - v); dist = dist * dist; return (dist.x + dist.y + dist.z); }
    constexpr T distance_2d(const vector3_base& v) const noexcept { vector3_base dist = (*this - v); dist = dist * dist; return std::sqrt(dist.x + dist.y); }
    constexpr T distance_2d_squared(const vector3_base& v) const noexcept { vector3_base dist = (*this - v); dist = dist * dist; return (dist.x + dist.y); }
    constexpr vector3_base cross(const vector3_base& v) const noexcept { return vector3(y * v.z - z * v.y, z * v.x - x * v.z, x * v.y - y * v.x); }
    constexpr vector3_base normalize() const noexcept { return (*this / std::abs(magnitude())); }
    constexpr bool zero_distance() const noexcept { return ((x == 0.0f && y == 0.0f && z == 0.0f) ? true : false); }

    static constexpr vector3_base lerp(const vector3_base& A, const vector3_base& B, const T t) noexcept { return A * t + B * (1.f - t); }
    /// @brief linear interpolate
    constexpr vector3_base lerp(const vector3_base& B, const T t) noexcept { return vector3_base::lerp(*this, B, t); }

    /// @brief spherical linear interpolate
    static constexpr vector3_base slerp(vector3_base start, vector3_base end, T percent) noexcept {
        T dot = start.dot(end);
        dot = clamp(dot, -1.0f, 1.0f);

        T theta = std::acos(dot) * percent;
        vector3_base relative = end - start * dot;
        return ((start * std::cos(theta)) + (relative.normalize()*std::sin(theta)));
    }
    /// @brief spherical linear interpolate
    constexpr vector3_base slerp(const vector3_base& B, const T p) const noexcept {
        return vector3_base::slerp(*this, B, p);
    }
};

typedef vector3_base<float> vector;
typedef vector3_base<float> vector3;

static const vector empty_vector = {0, 0, 0};
