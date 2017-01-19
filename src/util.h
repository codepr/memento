#ifndef UTIL_H
#define UTIL_H

#define GETINT(x) to_int(x)
#define GETFLOAT(x) to_float(x)
#define GETDOUBLE(x) to_double(x)
#define ISINT(x) is_integer(x)
#define CRC32(c, x) crc32(c, x)
#define RANDBETWEEN(A,B) A + rand()/(RAND_MAX/(B - A))

unsigned long crc32(const unsigned char *, unsigned int);
long long current_timestamp(void);
void trim(char *);
int is_integer(const char *);
int is_float(const char *);
int to_int(const char *);
double to_double(const char *);

#endif
