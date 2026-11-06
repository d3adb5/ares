#include <stdio.h>
#include <time.h>
#include <unistd.h>
int main(void) { char c; while (read(0, &c, 1) == 1) { struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t); char b[32]; int n = snprintf(b, sizeof b, "%lld\n", t.tv_sec * 1000000000LL + t.tv_nsec); write(1, b, n); } return 0; }
