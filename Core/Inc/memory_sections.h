#ifndef MEMORY_SECTIONS_H
#define MEMORY_SECTIONS_H

/* Place a variable in CCM SRAM using its natural alignment. */
#define CCM_SRAM __attribute__((section(".ccmdata")))

/* Place a variable in CCM SRAM using the specified byte alignment. */
#define CCM_SRAM_ALIGNED(n) __attribute__((section(".ccmdata"), aligned(n)))

#endif /* MEMORY_SECTIONS_H */
