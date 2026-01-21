// SPDX-License-Identifier: GPL-3.0-only

/**
 * @file math.h
 * @brief Math library header for ValkyrieOS kernel
 *
 * Provides common mathematical functions for integer and floating-point
 * arithmetic, trigonometry, exponential/logarithmic operations, and rounding.
 */

#ifndef LIBMATH_MATH_H
#define LIBMATH_MATH_H

/* ===== Math Constants ===== */

#define M_PI 3.14159265358979323846    /**< Pi (π) */
#define M_E 2.71828182845904523536     /**< Euler's number (e) */
#define M_LN2 0.69314718055994530942   /**< Natural log of 2 */
#define M_LN10 2.30258509299404568402  /**< Natural log of 10 */
#define M_SQRT2 1.41421356237309504880 /**< Square root of 2 */

#define INFINITY __builtin_inf() /**< Positive infinity */
#define NAN __builtin_nanf("")   /**< Not-a-number */

/* ===== Integer Arithmetic ===== */

/**
 * Add two integers
 * @param a First number
 * @param b Second number
 * @return Sum of a and b
 */
extern int add(int a, int b);

/**
 * Subtract two integers
 * @param a First number (minuend)
 * @param b Second number (subtrahend)
 * @return Difference a - b
 */
extern int subtract(int a, int b);

/**
 * Multiply two integers
 * @param a First number
 * @param b Second number
 * @return Product of a and b
 */
extern int multiply(int a, int b);

/**
 * Divide two integers
 * @param a Dividend
 * @param b Divisor
 * @return Integer quotient a / b (returns 0 if b == 0)
 */
extern int divide(int a, int b);

/**
 * Modulo operation
 * @param a Dividend
 * @param b Divisor
 * @return Remainder a % b (returns 0 if b == 0)
 */
extern int modulo(int a, int b);

/**
 * Absolute value of integer
 * @param x Value
 * @return Absolute value |x|
 */
extern int abs_int(int x);

/* ===== Floating-Point Absolute Value ===== */

/**
 * Absolute value of float
 * @param x Value
 * @return Absolute value |x|
 */
extern float fabsf(float x);

/**
 * Absolute value of double
 * @param x Value
 * @return Absolute value |x|
 */
extern double fabs(double x);

/* ===== Trigonometric Functions ===== */

/**
 * Sine of x (in radians)
 * @param x Angle in radians
 * @return sin(x)
 */
extern float sinf(float x);

extern double sin(double x);

/**
 * Cosine of x (in radians)
 * @param x Angle in radians
 * @return cos(x)
 */
extern float cosf(float x);

extern double cos(double x);

/**
 * Tangent of x (in radians)
 * @param x Angle in radians
 * @return tan(x)
 */
extern float tanf(float x);

extern double tan(double x);

/* ===== Exponential & Logarithm ===== */

/**
 * Exponential function e^x
 * @param x Exponent
 * @return e^x
 */
extern float expf(float x);

extern double exp(double x);

/**
 * Natural logarithm (base e)
 * @param x Value (must be > 0)
 * @return ln(x)
 */
extern float logf(float x);

extern double log(double x);

/**
 * Base-10 logarithm
 * @param x Value (must be > 0)
 * @return log₁₀(x)
 */
extern float log10f(float x);

extern double log10(double x);

/* ===== Power Function ===== */

/**
 * Power function: x raised to power y
 * @param x Base
 * @param y Exponent
 * @return x^y
 */
extern float powf(float x, float y);

extern double pow(double x, double y);

/**
 * Square root
 * @param x Value (must be >= 0)
 * @return √x
 */
extern float sqrtf(float x);

extern double sqrt(double x);

/* ===== Rounding ===== */

/**
 * Floor: largest integer <= x
 * @param x Value
 * @return ⌊x⌋
 */
extern float floorf(float x);

extern double floor(double x);

/**
 * Ceiling: smallest integer >= x
 * @param x Value
 * @return ⌈x⌉
 */
extern float ceilf(float x);

extern double ceil(double x);

/**
 * Round to nearest integer
 * @param x Value
 * @return Rounded value
 */
extern float roundf(float x);

extern double round(double x);

/* ===== Min/Max ===== */

/**
 * Minimum of two floats
 * @param x First value
 * @param y Second value
 * @return min(x, y)
 */
extern float fminf(float x, float y) __attribute__((weak));

extern double fmin(double x, double y);

/**
 * Maximum of two floats
 * @param x First value
 * @param y Second value
 * @return max(x, y)
 */
extern float fmaxf(float x, float y);

extern double fmax(double x, double y);

/* ===== Floating-Point Modulo ===== */

/**
 * Floating-point remainder
 * @param x Dividend
 * @param y Divisor (must be != 0)
 * @return x - trunc(x/y) * y
 */
extern float fmodf(float x, float y);

extern double fmod(double x, double y);

/* ===== Library Entry Point ===== */

/**
 * Library initialization function
 * Called when libmath is loaded by the dynamic linker
 * @return 0 on success
 */
extern int libmath_init(void);

#endif /* LIBMATH_MATH_H */
