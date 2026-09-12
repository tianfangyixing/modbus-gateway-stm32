#ifndef MANAGEMENT_TEST_MEMORY_SECTIONS_H
#define MANAGEMENT_TEST_MEMORY_SECTIONS_H
/* Host memory has no STM32 CCM/DMA address distinction. Firmware placement is checked by the integrator. */
#define CCM_SRAM
#define CCM_SRAM_ALIGNED(n)
#endif
