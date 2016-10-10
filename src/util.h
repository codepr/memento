#ifndef UTIL_H
#define UTIL_H

unsigned long crc32(const unsigned char *, unsigned int);
long long current_timestamp(void);
void trim(char *);
int is_integer(char *);
int is_float(char *);
int to_int(char *);
double to_double(char *);

#endif
