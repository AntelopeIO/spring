#include <sys/auxv.h>
#include <elf.h>

#ifndef HWCAP2_FSGSBASE
#define HWCAP2_FSGSBASE        (1 << 1)
#endif

int main() {
   return !(getauxval(AT_HWCAP2) & HWCAP2_FSGSBASE);
}
