#include "boot_control.h"
#include "internal_flash.h"
#include "stm32f4xx_hal.h"
#include <stddef.h>
#include <string.h>

static bool valid_slot(boot_slot_t slot) { return slot == BOOT_SLOT_A || slot == BOOT_SLOT_B; }
uint32_t boot_slot_address(boot_slot_t slot)
{
    return slot == BOOT_SLOT_A ? BOOT_SLOT_A_ADDRESS : slot == BOOT_SLOT_B ? BOOT_SLOT_B_ADDRESS
                                                                           : 0U;
}
uint32_t boot_slot_app_address(boot_slot_t slot)
{
    return valid_slot(slot) ? boot_slot_address(slot) + BOOT_SLOT_HEADER_SIZE : 0U;
}
uint32_t boot_slot_first_sector(boot_slot_t slot)
{
    return slot == BOOT_SLOT_A ? BOOT_SLOT_A_SECTOR : slot == BOOT_SLOT_B ? BOOT_SLOT_B_SECTOR
                                                                          : UINT32_MAX;
}
boot_control_result_t boot_slot_read(boot_slot_t slot, boot_slot_info_t *info)
{
    if (!valid_slot(slot) || info == NULL)
    {
        return BOOT_CONTROL_ARGUMENT;
    }
    return internal_flash_read(boot_slot_address(slot), (uint8_t *)info, sizeof(*info)) ==
                   INTERNAL_FLASH_RESULT_OK
               ? BOOT_CONTROL_OK
               : BOOT_CONTROL_READ_ERROR;
}
static bool initialized(const boot_slot_info_t *info)
{
    return info != NULL && info->magic == BOOT_SLOT_MAGIC && info->generation < 3U;
}
boot_slot_state_t boot_slot_state(const boot_slot_info_t *info)
{
    if (!initialized(info) || info->write_done != BOOT_FLAG_SET)
    {
        return BOOT_SLOT_INVALID;
    }
    if (info->boot_attempted == BOOT_FLAG_ERASED && info->boot_success == BOOT_FLAG_ERASED)
    {
        return BOOT_SLOT_PENDING;
    }
    if (info->boot_attempted == BOOT_FLAG_SET)
    {
        if (info->boot_success == BOOT_FLAG_SET)
        {
            return BOOT_SLOT_CONFIRMED;
        }
        if (info->boot_success == BOOT_FLAG_ERASED)
        {
            return BOOT_SLOT_UNCONFIRMED;
        }
    }
    return BOOT_SLOT_INVALID;
}
static bool candidate(boot_slot_state_t state)
{
    return state == BOOT_SLOT_PENDING || state == BOOT_SLOT_CONFIRMED;
}
static boot_slot_t newer(const boot_slot_info_t info[2])
{
    return info[1].generation == (info[0].generation + 1U) % 3U ? BOOT_SLOT_B : BOOT_SLOT_A;
}
uint32_t boot_slot_boot_order(boot_candidate_t order[2])
{
    boot_slot_info_t info[2];
    boot_slot_state_t state[2] = {BOOT_SLOT_INVALID, BOOT_SLOT_INVALID};
    uint32_t count = 0U;
    if (order == NULL)
    {
        return 0U;
    }
    for (uint32_t i = 0U; i < 2U; ++i)
    {
        if (boot_slot_read((boot_slot_t)i, &info[i]) == BOOT_CONTROL_OK)
        {
            state[i] = boot_slot_state(&info[i]);
        }
        if (candidate(state[i]))
        {
            order[count].slot = (boot_slot_t)i;
            order[count].state = state[i];
            ++count;
        }
    }
    if (count == 2U)
    {
        boot_slot_t first = newer(info);
        if (info[0].generation == info[1].generation && state[1] == BOOT_SLOT_CONFIRMED &&
            state[0] != BOOT_SLOT_CONFIRMED)
        {
            first = BOOT_SLOT_B;
        }
        if (first == BOOT_SLOT_B)
        {
            boot_candidate_t temp = order[0];
            order[0] = order[1];
            order[1] = temp;
        }
    }
    return count;
}
boot_control_result_t boot_slot_upgrade_target(boot_slot_t *target, uint8_t *generation)
{
    boot_slot_info_t info[2];
    boot_slot_state_t state[2];
    boot_slot_t keep = BOOT_SLOT_NONE;
    if (target == NULL || generation == NULL)
    {
        return BOOT_CONTROL_ARGUMENT;
    }
    *target = BOOT_SLOT_NONE;
    for (uint32_t i = 0U; i < 2U; ++i)
    {
        if (boot_slot_read((boot_slot_t)i, &info[i]) != BOOT_CONTROL_OK)
        {
            return BOOT_CONTROL_READ_ERROR;
        }
        state[i] = boot_slot_state(&info[i]);
    }
    if (state[0] == BOOT_SLOT_CONFIRMED || state[1] == BOOT_SLOT_CONFIRMED)
    {
        keep = state[0] == state[1] ? newer(info) : state[0] == BOOT_SLOT_CONFIRMED ? BOOT_SLOT_A
                                                                                    : BOOT_SLOT_B;
    }
    else if (state[0] == BOOT_SLOT_PENDING || state[1] == BOOT_SLOT_PENDING)
    {
        keep = state[0] == state[1] ? newer(info) : state[0] == BOOT_SLOT_PENDING ? BOOT_SLOT_A
                                                                                  : BOOT_SLOT_B;
    }
    *target = keep == BOOT_SLOT_A ? BOOT_SLOT_B : BOOT_SLOT_A;
    *generation = keep == BOOT_SLOT_NONE ? 0U : (uint8_t)((info[keep].generation + 1U) % 3U);
    return BOOT_CONTROL_OK;
}
static boot_control_result_t program_info(boot_slot_t slot, const boot_slot_info_t *expected,
                                          uint32_t offset, uint32_t size)
{
    boot_slot_info_t readback;
    if (internal_flash_program(boot_slot_address(slot) + offset,
                               (const uint8_t *)expected + offset, size) != INTERNAL_FLASH_RESULT_OK)
    {
        return BOOT_CONTROL_PROGRAM_ERROR;
    }
    if (boot_slot_read(slot, &readback) != BOOT_CONTROL_OK)
    {
        return BOOT_CONTROL_READ_ERROR;
    }
    return memcmp(expected, &readback, sizeof(readback)) == 0 ? BOOT_CONTROL_OK : BOOT_CONTROL_VERIFY_ERROR;
}
boot_control_result_t boot_slot_initialize(boot_slot_t slot, uint8_t generation)
{
    bool erased;
    boot_slot_info_t info;
    if (!valid_slot(slot) || generation >= 3U)
    {
        return BOOT_CONTROL_ARGUMENT;
    }
    if (internal_flash_is_erased(boot_slot_address(slot), BOOT_SLOT_HEADER_SIZE, &erased) !=
        INTERNAL_FLASH_RESULT_OK)
    {
        return BOOT_CONTROL_READ_ERROR;
    }
    if (!erased)
    {
        return BOOT_CONTROL_STATE_ERROR;
    }
    memset(&info, BOOT_FLAG_ERASED, sizeof(info));
    info.magic = BOOT_SLOT_MAGIC;
    info.generation = generation;
    /* No completion flag until the authenticated image has been verified. */
    return program_info(slot, &info, 0U, 5U);
}
static boot_control_result_t mark(boot_slot_t slot, uint32_t offset)
{
    boot_slot_info_t info;
    boot_slot_state_t state;
    boot_control_result_t result = boot_slot_read(slot, &info);
    if (result != BOOT_CONTROL_OK)
    {
        return result;
    }
    state = boot_slot_state(&info);
    if (offset == 5U)
    {
        if (state == BOOT_SLOT_PENDING)
        {
            return BOOT_CONTROL_OK;
        }
        if (!initialized(&info) || info.write_done != BOOT_FLAG_ERASED ||
            info.boot_attempted != BOOT_FLAG_ERASED || info.boot_success != BOOT_FLAG_ERASED)
        {
            return BOOT_CONTROL_STATE_ERROR;
        }
    }
    else if (offset == 6U)
    {
        if (state == BOOT_SLOT_UNCONFIRMED)
        {
            return BOOT_CONTROL_OK;
        }
        if (state != BOOT_SLOT_PENDING)
        {
            return BOOT_CONTROL_STATE_ERROR;
        }
    }
    else
    {
        if (state == BOOT_SLOT_CONFIRMED)
        {
            return BOOT_CONTROL_OK;
        }
        if (state != BOOT_SLOT_UNCONFIRMED)
        {
            return BOOT_CONTROL_STATE_ERROR;
        }
    }
    ((uint8_t *)&info)[offset] = BOOT_FLAG_SET;
    return program_info(slot, &info, offset, 1U);
}
boot_control_result_t boot_slot_mark_written(boot_slot_t slot) { return mark(slot, 5U); }
boot_control_result_t boot_slot_mark_attempted(boot_slot_t slot) { return mark(slot, 6U); }
boot_control_result_t boot_slot_confirm(boot_slot_t slot) { return mark(slot, 7U); }

boot_control_result_t boot_confirm_running(void)
{
    uint32_t vtor = SCB->VTOR;
    if (vtor == BOOT_SLOT_A_APP)
    {
        return boot_slot_confirm(BOOT_SLOT_A);
    }
    if (vtor == BOOT_SLOT_B_APP)
    {
        return boot_slot_confirm(BOOT_SLOT_B);
    }
    return BOOT_CONTROL_STATE_ERROR;
}

bool boot_slot_vectors_are_valid(boot_slot_t slot, uint32_t stack, uint32_t reset_handler)
{
    uint32_t entry = reset_handler & ~1U;
    bool stack_in_ram = (stack > 0x20000000U && stack <= 0x20020000U) ||
                        (stack > 0x10000000U && stack <= 0x10010000U);
    return valid_slot(slot) && stack_in_ram && (stack & 7U) == 0U &&
           (reset_handler & 1U) != 0U && entry >= boot_slot_app_address(slot) + 8U &&
           entry < boot_slot_address(slot) + BOOT_SLOT_SIZE;
}
bool boot_slot_read_vectors(boot_slot_t slot, uint32_t *stack, uint32_t *reset_handler)
{
    uint32_t vectors[2];
    if (!valid_slot(slot) || stack == NULL || reset_handler == NULL ||
        internal_flash_read(boot_slot_app_address(slot), (uint8_t *)vectors, sizeof(vectors)) !=
            INTERNAL_FLASH_RESULT_OK)
    {
        return false;
    }
    *stack = vectors[0];
    *reset_handler = vectors[1];
    return boot_slot_vectors_are_valid(slot, *stack, *reset_handler);
}

uint32_t boot_request_read(void)
{
    __HAL_RCC_PWR_CLK_ENABLE();
    return RTC->BKP0R;
}
static bool boot_request_write(uint32_t value)
{
    uint32_t saved_dbp;
    bool success = false;
    uint32_t primask = __get_PRIMASK();
    __disable_irq();
    __HAL_RCC_PWR_CLK_ENABLE();
    saved_dbp = PWR->CR & PWR_CR_DBP;
    HAL_PWR_EnableBkUpAccess();
    /* Do not reset the backup domain or configure the RTC clock/calendar. */
    for (uint32_t attempt = 0U; attempt < 1024U; ++attempt)
    {
        if ((PWR->CR & PWR_CR_DBP) != 0U)
        {
            RTC->BKP0R = value;
            __DSB();
            success = RTC->BKP0R == value;
            break;
        }
    }
    if (saved_dbp == 0U)
    {
        HAL_PWR_DisableBkUpAccess();
        (void)PWR->CR;
    }
    __DSB();
    __set_PRIMASK(primask);
    return success;
}

bool boot_request_set(void)
{
    return boot_request_write(BOOT_REQUEST_VALUE);
}

bool boot_request_clear(void)
{
    return boot_request_write(0U);
}
