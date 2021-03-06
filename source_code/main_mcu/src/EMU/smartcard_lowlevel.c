#include "smartcard_lowlevel.h"
#include "smartcard_highlevel.h"
#include "emu_smartcard.h"
#include "emulator.h"
#include <string.h>

static det_ret_type_te smartcard_status = RETURN_REL;

void emu_init_smartcard(struct emu_smartcard_storage_t *blank, int type)
{
    if(type == EMU_SMARTCARD_INVALID)
        memset(blank->smc, 0, sizeof(blank->smc));

    else {
        memset(blank->smc, 0xff, sizeof(blank->smc));
        blank->smc[0] = 0x0f; // fabrication zone
        blank->smc[1] = 0x0f;

        blank->smc[10] = 0xf0; // factory pin
        blank->smc[11] = 0xf0;
        memset(blank->fuses, 0, sizeof(blank->fuses));
    }

    blank->type = type;
}

det_ret_type_te smartcard_lowlevel_is_card_plugged(void)
{
    struct emu_smartcard_t *smartcard = emu_open_smartcard();

    if(smartcard != NULL) {
        if(smartcard_status == RETURN_JDETECT)
            smartcard_status = RETURN_DET;
        else if(smartcard_status != RETURN_DET)
            smartcard_status = RETURN_JDETECT;

        emu_close_smartcard(FALSE);

    } else {
        if(smartcard_status == RETURN_JRELEASED)
            smartcard_status = RETURN_REL;
        else if(smartcard_status != RETURN_REL)
            smartcard_status = RETURN_JRELEASED;
    }

    return smartcard_status;
}

RET_TYPE smartcard_lowlevel_check_for_const_val_in_smc_array(uint16_t nb_bytes_total_read, uint16_t start_record_index, uint8_t value){
	uint8_t tmpbuf[nb_bytes_total_read-start_record_index];
	smartcard_lowlevel_read_smc(nb_bytes_total_read, start_record_index, tmpbuf);
	for(unsigned int i=0;i<sizeof(tmpbuf);i++)
		if(tmpbuf[i] != value)
			return RETURN_NOK;

	return RETURN_OK;
}

/* 178-180 - manufacturer zone */
uint8_t* smartcard_lowlevel_read_smc(uint16_t nb_bytes_total_read, uint16_t start_record_index, uint8_t* data_to_receive) 
{
    struct emu_smartcard_t *smartcard = emu_open_smartcard();
    int i;
    if(smartcard == NULL)
        return 0;
    
    /* nb_bytes_total_read name is horribly misleading :( */
    for(i=start_record_index;i < nb_bytes_total_read;i++) {
        BOOL allowed = TRUE;

        /* This is not a full-featured smartcard emulator. We simply assume
         * that the application zones are not readable without the PIN */
        if(i >= (176/8) && i < (1408/8) && !smartcard->unlocked)
            allowed = FALSE;

        if(emu_get_failure_flags() & EMU_FAIL_SMARTCARD_INSECURE)
            allowed = TRUE;

        data_to_receive[i - start_record_index] = allowed ? smartcard->storage.smc[i] : 0xff;
    }
    emu_close_smartcard(FALSE);
    return data_to_receive;
}

void smartcard_lowlevel_write_smc(uint16_t start_index_bit, uint16_t nb_bits, uint8_t* data_to_write)
{
    struct emu_smartcard_t *smartcard = emu_open_smartcard();

    if(smartcard == NULL)
        return;

    /* these tests are not bulletproof, but they work with normally behaved accesses */
    if(start_index_bit >= 1424 && start_index_bit < 1440 && smartcard->storage.fuses[MAN_FUSE]) {
        emu_close_smartcard(FALSE);
        return;
    }

    if(smartcard->storage.type == EMU_SMARTCARD_BROKEN) {
        emu_close_smartcard(FALSE);
        return;
    }

    /* FIXME: doesn't support sub-byte accesses */
    memcpy(smartcard->storage.smc + start_index_bit/8, data_to_write, nb_bits/8);
    emu_close_smartcard(TRUE);
}

pin_check_return_te smartcard_lowlevel_validate_code(volatile uint16_t* code)
{
    struct emu_smartcard_t *smartcard = emu_open_smartcard();

    if(smartcard == NULL)
        return RETURN_PIN_NOK_0;

    if((*code & 0xff) == smartcard->storage.smc[11] && (*code >> 8) == smartcard->storage.smc[10]) {
        smartcard->storage.smc[12] = 0xf0;
        smartcard->unlocked = TRUE; // "The SV flag remains set until power to the card is turned off."
        emu_close_smartcard(TRUE);
        return RETURN_PIN_OK; 

    } else {
        smartcard->storage.smc[12] >>= 1; // remove one attempt bit
        emu_close_smartcard(TRUE);
        return RETURN_PIN_NOK_0; 
    }
}

void smartcard_lowlevel_erase_application_zone1_nzone2(BOOL zone1_nzone2){}

card_detect_return_te smartcard_lowlevel_first_detect_function(void)
{
    /* Code below copied from SMARTCARD/smartcard_lowlevel.c */
    uint16_t data_buffer;
    uint16_t temp_uint;

    smartcard_highlevel_read_fab_zone((uint8_t*)&data_buffer);
    if ((swap16(data_buffer)) != SMARTCARD_FABRICATION_ZONE)
    {
        return RETURN_CARD_NDET;
    }

    /* Perform test write on MTZ */
    smartcard_highlevel_read_mem_test_zone((uint8_t*)&temp_uint);
    temp_uint = temp_uint + 5;
    smartcard_highlevel_write_mem_test_zone((uint8_t*)&temp_uint);
    smartcard_highlevel_read_mem_test_zone((uint8_t*)&data_buffer);
    if (data_buffer != temp_uint)
    {
        return RETURN_CARD_TEST_PB;
    }

    /* Read security code attempts counter */
    switch(smartcard_highlevel_get_nb_sec_tries_left())
    {
        case 4: return RETURN_CARD_4_TRIES_LEFT;
        case 3: return RETURN_CARD_3_TRIES_LEFT;
        case 2: return RETURN_CARD_2_TRIES_LEFT;
        case 1: return RETURN_CARD_1_TRIES_LEFT;
        case 0: return RETURN_CARD_0_TRIES_LEFT;
        default: return RETURN_CARD_0_TRIES_LEFT;
    }
 
    return RETURN_CARD_4_TRIES_LEFT; 
}

void smartcard_lowlevel_blow_fuse(card_fuse_type_te fuse_name)
{
    struct emu_smartcard_t *smartcard = emu_open_smartcard();

    if(smartcard == NULL)
        return;

    smartcard->storage.fuses[fuse_name] = 1;
    emu_close_smartcard(TRUE);
}


RET_TYPE smartcard_low_level_is_smc_absent(void) {
    struct emu_smartcard_t *smartcard = emu_open_smartcard();

    if(smartcard == NULL)
        return RETURN_OK;
    
    emu_close_smartcard(FALSE);
    return RETURN_NOK;
}
