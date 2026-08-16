/* math.h -- minimal declarations for Makar apps.  Doom core is fixed-point and
 * calls no libm at runtime; these protos exist only so includes resolve. */
#ifndef _USERSPACE_MATH_H
#define _USERSPACE_MATH_H
double sin(double), cos(double), tan(double), atan2(double, double);
double sqrt(double), pow(double, double), fabs(double), floor(double), ceil(double);
#endif
