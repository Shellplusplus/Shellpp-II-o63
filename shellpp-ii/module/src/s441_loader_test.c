#include "shellpp_ii_module.h"

/* First s441 load probe: no firmware calls and no constructor. */
__attribute__((used, section(".rodata")))
static const char s441_loader_test_rodata[] = "s441-loader-test";
__attribute__((used, section(".data")))
static unsigned int s441_loader_test_data = 1u;
__attribute__((used, section(".bss")))
static unsigned int s441_loader_test_bss;
