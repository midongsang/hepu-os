#ifndef LUA_MATH_H
#define LUA_MATH_H
#define HUGE_VAL 1e308
double sqrt(double);
double pow(double, double);
double floor(double);
double ceil(double);
double fabs(double);
double fmod(double, double);
double exp(double);
double log(double);
double log10(double);
double log2(double);
double sin(double);
double cos(double);
double tan(double);
double asin(double);
double acos(double);
double atan(double);
double atan2(double, double);
double frexp(double, int *);
double ldexp(double, int);
double modf(double, double *);
#endif
