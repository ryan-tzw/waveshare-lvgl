#include "node_identity.h"

#include <string.h>

#include "hardware/flash.h"
#include "hardware/regs/addressmap.h"
#include "hardware/sync.h"
#include "pico/assert.h"

// The final flash sector is an append-only journal and is erased only when full
#define BOOT_RECORD_FLASH_OFFSET (PICO_FLASH_SIZE_BYTES - FLASH_SECTOR_SIZE)

typedef struct {
    uint32_t boot_id;
    uint32_t boot_id_inverse;
} BootRecord;

static bool boot_record_is_erased(const BootRecord *record) {
    return record->boot_id == UINT32_MAX &&
           record->boot_id_inverse == UINT32_MAX;
}

static bool boot_record_is_valid(const BootRecord *record) {
    // A complete record's boot ID and inverse XOR to all one bits
    return (record->boot_id ^ record->boot_id_inverse) == UINT32_MAX;
}

static void read_boot_record(size_t record_index, BootRecord *record) {
    const uint8_t *flash_address = (const uint8_t *)(XIP_BASE + BOOT_RECORD_FLASH_OFFSET);

    memcpy(
        record,
        flash_address + record_index * sizeof(BootRecord),
        sizeof(BootRecord)
    );
}

static bool create_boot_id(uint32_t *boot_id) {
    size_t record_count = FLASH_SECTOR_SIZE / sizeof(BootRecord);
    size_t empty_record_index = record_count;
    bool valid_record_found = false;
    uint32_t highest_boot_id = 0;

    for (size_t record_index = 0; record_index < record_count; record_index++) {
        BootRecord record;
        read_boot_record(record_index, &record);

        if (boot_record_is_erased(&record)) {
            if (empty_record_index == record_count) {
                empty_record_index = record_index;
            }
        } else if (
            boot_record_is_valid(&record) &&
            (!valid_record_found || record.boot_id > highest_boot_id)
        ) {
            highest_boot_id = record.boot_id;
            valid_record_found = true;
        }
    }

    if (valid_record_found && highest_boot_id == UINT32_MAX) {
        return false;
    }

    if (valid_record_found) {
        *boot_id = highest_boot_id + 1;
    } else {
        *boot_id = 1;
    }

    bool sector_is_full = empty_record_index == record_count;
    if (sector_is_full) {
        empty_record_index = 0;
    }

    size_t record_offset = empty_record_index * sizeof(BootRecord);
    size_t page_offset = record_offset - (record_offset % FLASH_PAGE_SIZE);
    size_t record_offset_in_page = record_offset % FLASH_PAGE_SIZE;

    uint8_t page[FLASH_PAGE_SIZE];
    // Programming one bits leaves previously written records unchanged
    memset(page, UINT8_MAX, sizeof(page));

    BootRecord record = {
        .boot_id = *boot_id,
        .boot_id_inverse = ~*boot_id,
    };
    memcpy(page + record_offset_in_page, &record, sizeof(record));

    // Prevent interrupt handlers from reading flash while XIP is unavailable
    uint32_t interrupts = save_and_disable_interrupts();

    if (sector_is_full) {
        flash_range_erase(BOOT_RECORD_FLASH_OFFSET, FLASH_SECTOR_SIZE);
    }

    flash_range_program(
        BOOT_RECORD_FLASH_OFFSET + page_offset,
        page,
        sizeof(page)
    );

    restore_interrupts(interrupts);

    BootRecord stored_record;
    read_boot_record(empty_record_index, &stored_record);

    return stored_record.boot_id == record.boot_id &&
           stored_record.boot_id_inverse == record.boot_id_inverse;
}

bool node_identity_init(NodeIdentity *identity) {
    if (identity == NULL || identity->initialized) {
        return false;
    }

    uint32_t boot_id;
    if (!create_boot_id(&boot_id)) {
        return false;
    }

    pico_get_unique_board_id(&identity->node_id);
    identity->boot_id = boot_id;
    identity->next_sequence = 0;
    identity->initialized = true;

    return true;
}

uint32_t node_identity_next_sequence(NodeIdentity *identity) {
    hard_assert(identity != NULL);
    hard_assert(identity->initialized);

    uint32_t sequence = identity->next_sequence;
    identity->next_sequence++;

    return sequence;
}
