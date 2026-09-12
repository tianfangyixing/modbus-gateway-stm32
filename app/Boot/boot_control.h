#ifndef BOOT_CONTROL_H
#define BOOT_CONTROL_H

#include <stdbool.h>
#include <stdint.h>

/* STM32F407ZG */
#define BOOT_FLASH_BASE 0x08000000U
#define BOOT_FLASH_SIZE 0x00010000U
#define BOOT_SLOT_A_ADDRESS 0x08020000U
#define BOOT_SLOT_B_ADDRESS 0x08080000U
#define BOOT_SLOT_SIZE 0x00060000U
#define BOOT_SLOT_HEADER_SIZE 0x00000200U
#define BOOT_SLOT_INFO_SIZE 8U
#define BOOT_SLOT_A_APP (BOOT_SLOT_A_ADDRESS + BOOT_SLOT_HEADER_SIZE)
#define BOOT_SLOT_B_APP (BOOT_SLOT_B_ADDRESS + BOOT_SLOT_HEADER_SIZE)
#define BOOT_SLOT_A_SECTOR 5U
#define BOOT_SLOT_B_SECTOR 8U
#define BOOT_SLOT_SECTORS 3U
#define BOOT_SECTOR_SIZE 0x00020000U
#define APP_MIN_SIZE 1024U
#define APP_MAX_SIZE (BOOT_SLOT_SIZE - BOOT_SLOT_HEADER_SIZE)
#define BOOT_SLOT_MAGIC 0x32544C53U
#define BOOT_FLAG_ERASED 0xFFU
#define BOOT_FLAG_SET 0x01U
#define BOOT_REQUEST_VALUE 1U

typedef enum
{
    BOOT_SLOT_A = 0,
    BOOT_SLOT_B = 1,
    BOOT_SLOT_NONE = 2
} boot_slot_t;
typedef struct
{
    uint32_t magic;
    uint8_t generation;
    uint8_t write_done;
    uint8_t boot_attempted;
    uint8_t boot_success;
} boot_slot_info_t;

typedef enum
{
    BOOT_SLOT_INVALID = 0,
    BOOT_SLOT_PENDING,
    BOOT_SLOT_CONFIRMED,
    BOOT_SLOT_UNCONFIRMED
} boot_slot_state_t;

typedef struct
{
    boot_slot_t slot;
    boot_slot_state_t state;
} boot_candidate_t;
typedef enum
{
    BOOT_CONTROL_OK = 0,
    BOOT_CONTROL_ARGUMENT,
    BOOT_CONTROL_READ_ERROR,
    BOOT_CONTROL_STATE_ERROR,
    BOOT_CONTROL_PROGRAM_ERROR,
    BOOT_CONTROL_VERIFY_ERROR
} boot_control_result_t;

uint32_t boot_slot_address(boot_slot_t slot);
uint32_t boot_slot_app_address(boot_slot_t slot);
uint32_t boot_slot_first_sector(boot_slot_t slot);
boot_control_result_t boot_slot_read(boot_slot_t slot, boot_slot_info_t *info);
boot_slot_state_t boot_slot_state(const boot_slot_info_t *info);
/* Read-only snapshot of PENDING/CONFIRMED slots in boot order.
 * Consume in serialized startup context; unreadable slots are excluded. */
uint32_t boot_slot_boot_order(boot_candidate_t order[2]);
/* Both reads must succeed before the caller may erase anything. */
boot_control_result_t boot_slot_upgrade_target(boot_slot_t *target, uint8_t *generation);
/* Initialize requires an erased entire header. These APIs never erase Flash.
 * Flags are programmed individually, with full info readback, in stage order.
 * Use from serialized thread/startup context, never concurrently or from ISR. */
boot_control_result_t boot_slot_initialize(boot_slot_t slot, uint8_t generation);
boot_control_result_t boot_slot_mark_written(boot_slot_t slot);
boot_control_result_t boot_slot_mark_attempted(boot_slot_t slot);
boot_control_result_t boot_slot_confirm(boot_slot_t slot);
/* APP success acknowledgement: infer the slot from SCB->VTOR, which must
 * equal BOOT_SLOT_A_APP or BOOT_SLOT_B_APP (no RAM vector relocation).
 * Unknown VTOR returns BOOT_CONTROL_STATE_ERROR without writing Flash.
 * Call after APP self-tests, in the serialized context described above. */
boot_control_result_t boot_confirm_running(void);
bool boot_slot_vectors_are_valid(boot_slot_t slot, uint32_t stack, uint32_t reset_handler);
bool boot_slot_read_vectors(boot_slot_t slot, uint32_t *stack, uint32_t *reset_handler);

/* RTC BKP0R ABI only: query, set and clear; BKP1R..19R are untouched.
 * Set/clear return true when readback matches and restore DBP/PRIMASK. */
uint32_t boot_request_read(void);
bool boot_request_set(void);
bool boot_request_clear(void);

#endif
