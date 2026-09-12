#ifndef MEMORY_SECTIONS_H
#define MEMORY_SECTIONS_H

#if defined(__CC_ARM)
/* ARMCC must treat uninitialized CCM objects as zero-initialized data. */
#define CCM_SRAM __attribute__((section(".ccmdata"), zero_init))
#define CCM_SRAM_ALIGNED(n) __attribute__((section(".ccmdata"), zero_init, aligned(n)))
#else
#define CCM_SRAM __attribute__((section(".ccmdata")))
#define CCM_SRAM_ALIGNED(n) __attribute__((section(".ccmdata"), aligned(n)))
#endif

#endif /* MEMORY_SECTIONS_H */
