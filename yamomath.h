#pragma once
#include <cmath>
#include <stdexcept>

// ============================================================
//  yamomath.h — математика ЯМО, вынесена из interpreter.cpp
// ============================================================
namespace yamo_math {
    constexpr double PI = 3.141592653589793;
    constexpr double E  = 2.718281828459045;

    inline double deg_to_rad(double deg) { return deg * PI / 180.0; }

    inline double sinus_rad(double x)   { return std::sin(x); }
    inline double cosinus_rad(double x) { return std::cos(x); }
    inline double tangens_rad(double x) {
        double c = std::cos(x);
        if (c == 0.0) throw std::runtime_error("тангенс не определён для этого угла");
        return std::sin(x) / c;
    }
    inline double cotangens_rad(double x) {
        double s = std::sin(x);
        if (s == 0.0) throw std::runtime_error("котангенс не определён для этого угла");
        return std::cos(x) / s;
    }
    inline double secans_rad(double x) {
        double c = std::cos(x);
        if (c == 0.0) throw std::runtime_error("секанс не определён для этого угла");
        return 1.0 / c;
    }
    inline double cosecans_rad(double x) {
        double s = std::sin(x);
        if (s == 0.0) throw std::runtime_error("косеканс не определён для этого угла");
        return 1.0 / s;
    }
    inline double arcsinus(double x)    { return std::asin(x) * 180.0 / PI; }
    inline double arccosinus(double x)   { return std::acos(x) * 180.0 / PI; }
    inline double arctangens(double x)   { return std::atan(x) * 180.0 / PI; }
    inline double logarithm(double x) {
        if (x <= 0.0) throw std::runtime_error("логарифм неположительного числа");
        return std::log(x);
    }
    inline double exponens(double x)    { return std::exp(x); }
}