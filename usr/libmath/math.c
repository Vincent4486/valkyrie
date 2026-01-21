// SPDX-License-Identifier: BSD-3-Clause

/**
 * Dynamic math library for kernel
 * Exports: integer ops, floating-point trig, exponential, power, rounding
 */

#include <float.h>

/* Math constants */
#define M_PI 3.14159265358979323846
#define M_E 2.71828182845904523536
#define M_LN2 0.69314718055994530942
#define M_LN10 2.30258509299404568402
#define M_SQRT2 1.41421356237309504880
#define INFINITY __builtin_inf()
#define NAN __builtin_nanf("")

/* ===== Integer Arithmetic ===== */

int add(int a, int b) { return a + b; }

int subtract(int a, int b) { return a - b; }

int multiply(int a, int b) { return a * b; }

int divide(int a, int b)
{
   if (b == 0) return 0;
   return a / b;
}

int modulo(int a, int b)
{
   if (b == 0) return 0;
   return a % b;
}

int abs_int(int x) { return x < 0 ? -x : x; }

/* ===== Floating-Point Absolute Value ===== */

float fabsf(float x) { return x < 0.0f ? -x : x; }

double fabs(double x) { return x < 0.0 ? -x : x; }

/* ===== Trigonometric Functions ===== */

/* sin(x) using Taylor series: x - x^3/3! + x^5/5! - x^7/7! + ... */
float sinf(float x)
{
   /* Reduce x to [-pi, pi] */
   while (x > M_PI) x -= 2 * M_PI;
   while (x < -M_PI) x += 2 * M_PI;

   float result = x;
   float term = x;
   for (int i = 1; i < 20; i++)
   {
      term *= -x * x / ((2 * i) * (2 * i + 1));
      result += term;
      if (fabsf(term) < 1e-7f) break;
   }
   return result;
}

double sin(double x)
{
   while (x > M_PI) x -= 2 * M_PI;
   while (x < -M_PI) x += 2 * M_PI;

   double result = x;
   double term = x;
   for (int i = 1; i < 40; i++)
   {
      term *= -x * x / ((2 * i) * (2 * i + 1));
      result += term;
      if (fabs(term) < 1e-15) break;
   }
   return result;
}

/* cos(x) using Taylor series: 1 - x^2/2! + x^4/4! - x^6/6! + ... */
float cosf(float x)
{
   while (x > M_PI) x -= 2 * M_PI;
   while (x < -M_PI) x += 2 * M_PI;

   float result = 1.0f;
   float term = 1.0f;
   for (int i = 1; i < 20; i++)
   {
      term *= -x * x / ((2 * i - 1) * (2 * i));
      result += term;
      if (fabsf(term) < 1e-7f) break;
   }
   return result;
}

double cos(double x)
{
   while (x > M_PI) x -= 2 * M_PI;
   while (x < -M_PI) x += 2 * M_PI;

   double result = 1.0;
   double term = 1.0;
   for (int i = 1; i < 40; i++)
   {
      term *= -x * x / ((2 * i - 1) * (2 * i));
      result += term;
      if (fabs(term) < 1e-15) break;
   }
   return result;
}

/* tan(x) = sin(x) / cos(x) */
float tanf(float x) { return sinf(x) / cosf(x); }

double tan(double x) { return sin(x) / cos(x); }

/* ===== Exponential & Logarithm ===== */

/* exp(x) using Taylor series: 1 + x + x^2/2! + x^3/3! + ... */
float expf(float x)
{
   if (x > 100.0f) return INFINITY;
   if (x < -100.0f) return 0.0f;

   float result = 1.0f;
   float term = 1.0f;
   for (int i = 1; i < 50; i++)
   {
      term *= x / i;
      result += term;
      if (fabsf(term) < 1e-7f) break;
   }
   return result;
}

double exp(double x)
{
   if (x > 700.0) return INFINITY;
   if (x < -700.0) return 0.0;

   double result = 1.0;
   double term = 1.0;
   for (int i = 1; i < 100; i++)
   {
      term *= x / i;
      result += term;
      if (fabs(term) < 1e-15) break;
   }
   return result;
}

/* Natural logarithm using Newton's method */
double log(double x)
{
   if (x <= 0.0) return -INFINITY;
   if (x == 1.0) return 0.0;

   /* First approximation using bit manipulation (fast log) */
   double y = (double)((*(long long *)&x - 0x3fe6a09e667f3bcc) >> 1) *
              1.4426950408889634;

   /* Newton iterations for refinement */
   for (int i = 0; i < 10; i++)
   {
      y = y - (expf(y) - x) / expf(y);
   }
   return y;
}

float logf(float x) { return (float)log((double)x); }

/* log10(x) = log(x) / log(10) */
double log10(double x) { return log(x) / log(10.0); }

float log10f(float x) { return (float)log10((double)x); }

/* ===== Power Function ===== */

/* pow(x, y) = exp(y * log(x)) */
double pow(double x, double y)
{
   if (x == 0.0) return 0.0;
   if (y == 0.0) return 1.0;
   if (x < 0.0 && (y != (int)y))
      return NAN; /* Negative base with non-integer exponent */

   return exp(y * log(fabs(x)));
}

float powf(float x, float y) { return (float)pow((double)x, (double)y); }

/* sqrt(x) using Newton's method: x_{n+1} = (x_n + x/x_n) / 2 */
float sqrtf(float x)
{
   if (x < 0.0f) return NAN;
   if (x == 0.0f) return 0.0f;

   float guess = x * 0.5f;
   for (int i = 0; i < 20; i++)
   {
      float next = (guess + x / guess) * 0.5f;
      if (fabsf(next - guess) < 1e-6f) break;
      guess = next;
   }
   return guess;
}

double sqrt(double x)
{
   if (x < 0.0) return NAN;
   if (x == 0.0) return 0.0;

   double guess = x * 0.5;
   for (int i = 0; i < 40; i++)
   {
      double next = (guess + x / guess) * 0.5;
      if (fabs(next - guess) < 1e-15) break;
      guess = next;
   }
   return guess;
}

/* ===== Rounding ===== */

float floorf(float x)
{
   int truncated = (int)x;
   if (x < 0.0f && x != truncated) return (float)(truncated - 1);
   return (float)truncated;
}

double floor(double x)
{
   int truncated = (int)x;
   if (x < 0.0 && x != truncated) return (double)(truncated - 1);
   return (double)truncated;
}

float ceilf(float x)
{
   int truncated = (int)x;
   if (x > 0.0f && x != truncated) return (float)(truncated + 1);
   return (float)truncated;
}

double ceil(double x)
{
   int truncated = (int)x;
   if (x > 0.0 && x != truncated) return (double)(truncated + 1);
   return (double)truncated;
}

float roundf(float x) { return x >= 0.0f ? floorf(x + 0.5f) : ceilf(x - 0.5f); }

double round(double x) { return x >= 0.0 ? floor(x + 0.5) : ceil(x - 0.5); }

/* ===== Min/Max ===== */

float fminf(float x, float y) { return x < y ? x : y; }

double fmin(double x, double y) { return x < y ? x : y; }

float fmaxf(float x, float y) { return x > y ? x : y; }

double fmax(double x, double y) { return x > y ? x : y; }

/* ===== Floating-Point Modulo ===== */

float fmodf(float x, float y)
{
   if (y == 0.0f) return NAN;
   return x - ((int)(x / y)) * y;
}

double fmod(double x, double y)
{
   if (y == 0.0) return NAN;
   return x - ((int)(x / y)) * y;
}

/* ===== Library Entry Point ===== */

/**
 * Library initialization
 * @return Always returns 0
 */
int libmath_init(void) { return 0; }
