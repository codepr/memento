#ifndef UTIL_H
#define UTIL_H

unsigned long crc32(const unsigned char *, unsigned int);
long long current_timestamp(void);
void trim(char *);
int is_integer(const char *);
int is_float(const char *);
int to_int(const char *);
double to_double(const char *);

#endif
