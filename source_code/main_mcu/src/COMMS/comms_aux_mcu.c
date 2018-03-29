/*!  \file     comms_aux_mcu.c
*    \brief    Communications with aux MCU
*    Created:  03/03/2018
*    Author:   Mathieu Stephan
*/
#include <string.h>
#include <asf.h>
#include "comms_hid_msgs_debug.h"
#include "platform_defines.h"
#include "comms_hid_msgs.h"
#include "comms_aux_mcu.h"
#include "driver_timer.h"
#include "defines.h"
#include "dma.h"
/* Received and sent MCU messages */
aux_mcu_message_t aux_mcu_receive_message;
aux_mcu_message_t aux_mcu_send_message;
/* Flag set if we have treated a message by only looking at its first bytes */
BOOL aux_mcu_message_answered_using_first_bytes = FALSE;


/*! \fn     comms_aux_init(void)
*   \brief  Init communications with aux MCU
*/
void comms_aux_init(void)
{
    dma_aux_mcu_init_rx_transfer((void*)&AUXMCU_SERCOM->USART.DATA.reg, (void*)&aux_mcu_receive_message, sizeof(aux_mcu_receive_message));
    
    /* For test */
    //aux_mcu_message_buffer2.message_type = 0x5555;
    //dma_aux_mcu_init_tx_transfer((void*)&AUXMCU_SERCOM->USART.DATA.reg, (void*)&aux_mcu_message_buffer2, sizeof(aux_mcu_message_buffer1));
}

/*! \fn     comms_aux_mcu_routine(void)
*   \brief  Routine dealing with aux mcu comms
*/
void comms_aux_mcu_routine(void)
{	
    /* Ongoing RX transfer received bytes */
    uint16_t nb_received_bytes_for_ongoing_transfer = sizeof(aux_mcu_receive_message) - dma_aux_mcu_get_remaining_bytes_for_rx_transfer();
    
    /* Bool to treat packet */
    BOOL should_deal_with_packet = FALSE;
    
    /* Bool to specify if we should rearm RX DMA transfer */
    BOOL arm_rx_transfer = FALSE;
    
    /* Received message payload length */
    uint16_t payload_length;
    
    /* First part of message */
    if ((nb_received_bytes_for_ongoing_transfer >= sizeof(aux_mcu_receive_message.message_type) + sizeof(aux_mcu_receive_message.payload_length1)) && (aux_mcu_message_answered_using_first_bytes == FALSE))
    {
        /* Check if we were too slow to deal with the message before complete packet transfer */
        if (dma_aux_mcu_check_and_clear_dma_transfer_flag() != FALSE)
        {
            /* Complete packet receive, treat packet if valid flag is set or payload length #1 != 0 */
            aux_mcu_message_answered_using_first_bytes = FALSE;            
            
            if (aux_mcu_receive_message.payload_length1 != 0)
            {
                arm_rx_transfer = TRUE;
                should_deal_with_packet = TRUE;
                payload_length = aux_mcu_receive_message.payload_length1;
            }
            else if (aux_mcu_receive_message.payload_valid_flag != 0)
            {
                arm_rx_transfer = TRUE;
                should_deal_with_packet = TRUE;
                payload_length = aux_mcu_receive_message.payload_length2;                
            }
            else
            {
                /* Arm next RX DMA transfer */
                dma_aux_mcu_init_rx_transfer((void*)&AUXMCU_SERCOM->USART.DATA.reg, (void*)&aux_mcu_receive_message, sizeof(aux_mcu_receive_message));
            }
        } 
        else if ((aux_mcu_receive_message.payload_length1 != 0) && (nb_received_bytes_for_ongoing_transfer >= sizeof(aux_mcu_receive_message.message_type) + sizeof(aux_mcu_receive_message.payload_length1) + aux_mcu_receive_message.payload_length1))
        {
            /* First part receive, payload is small enough so we can answer */
            should_deal_with_packet = TRUE;
            aux_mcu_message_answered_using_first_bytes = TRUE;
            payload_length = aux_mcu_receive_message.payload_length1;
        } 
    }
    else if (dma_aux_mcu_check_and_clear_dma_transfer_flag() != FALSE)
    {
        /* Second part transfer, check if we have already dealt with this packet and if it is valid */
        if ((aux_mcu_message_answered_using_first_bytes == FALSE) && (aux_mcu_receive_message.payload_valid_flag != 0))
        {
            arm_rx_transfer = TRUE;
            should_deal_with_packet = TRUE;
            payload_length = aux_mcu_receive_message.payload_length2;
            
        }
        else
        {
            /* Arm next RX DMA transfer */
            dma_aux_mcu_init_rx_transfer((void*)&AUXMCU_SERCOM->USART.DATA.reg, (void*)&aux_mcu_receive_message, sizeof(aux_mcu_receive_message));
        }
        
        /* Reset bool */
        aux_mcu_message_answered_using_first_bytes = FALSE;
    }
    
    /* Return if we shouldn't deal with packet, or if payload has the incorrect size */
    if ((should_deal_with_packet == FALSE) || (payload_length > AUX_MCU_MSG_PAYLOAD_LENGTH))
    {
        /* Note: there's a case where we don't rearm DMA if the message is valid but payload is too long... was lazy to implement it */
        return;
    }
    
    PORT->Group[DBFLASH_nCS_GROUP].OUTCLR.reg = DBFLASH_nCS_MASK;
    asm("Nop");asm("Nop");asm("Nop");asm("Nop");asm("Nop");
    PORT->Group[DBFLASH_nCS_GROUP].OUTSET.reg = DBFLASH_nCS_MASK;
            
    /* USB / BLE Messages */
    if ((aux_mcu_receive_message.message_type == AUX_MCU_MSG_TYPE_USB) || (aux_mcu_receive_message.message_type == AUX_MCU_MSG_TYPE_BLE))
    {
        /* Cast payloads into correct type */
        int16_t hid_reply_payload_length = -1;
        hid_message_t* hid_message_pt = (hid_message_t*)aux_mcu_receive_message.payload_as_uint32;
        hid_message_t* send_hid_message_pt = (hid_message_t*)aux_mcu_send_message.payload_as_uint32;
                
        /* Clear TX message just in case */
        memset((void*)&aux_mcu_send_message, 0, sizeof(aux_mcu_send_message));
                
        /* Parse message */
        #ifndef DEBUG_USB_COMMANDS_ENABLED
        hid_reply_payload_length = comms_hid_msgs_parse(hid_message_pt, payload_length - sizeof(hid_message_pt->message_type) - sizeof(hid_message_pt->payload_length), send_hid_message_pt);
        #else
        if (hid_message_pt->message_type >= HID_MESSAGE_START_CMD_ID_DBG)
        {
            hid_reply_payload_length = comms_hid_msgs_parse_debug(hid_message_pt, payload_length - sizeof(hid_message_pt->message_type) - sizeof(hid_message_pt->payload_length), send_hid_message_pt);
        }
        else
        {
            hid_reply_payload_length = comms_hid_msgs_parse(hid_message_pt, payload_length - sizeof(hid_message_pt->message_type) - sizeof(hid_message_pt->payload_length), send_hid_message_pt);
        }
        #endif
                
        /* Send reply if needed */
        if (hid_reply_payload_length >= 0)
        {
            /* Set same message type and compute payload size */
            aux_mcu_send_message.payload_valid_flag = 0x0001;
            aux_mcu_send_message.message_type = aux_mcu_receive_message.message_type;
            aux_mcu_send_message.payload_length1 = hid_reply_payload_length + sizeof(hid_message_pt->message_type) + sizeof(hid_message_pt->payload_length);
            aux_mcu_send_message.payload_length2 = hid_reply_payload_length + sizeof(hid_message_pt->message_type) + sizeof(hid_message_pt->payload_length);
                    
            /* Send message */
            dma_aux_mcu_init_tx_transfer((void*)&AUXMCU_SERCOM->USART.DATA.reg, (void*)&aux_mcu_send_message, sizeof(aux_mcu_send_message));
        }
    }     
    
    /* If we need to rearm RX */
    if (arm_rx_transfer != FALSE)
    {
        dma_aux_mcu_init_rx_transfer((void*)&AUXMCU_SERCOM->USART.DATA.reg, (void*)&aux_mcu_receive_message, sizeof(aux_mcu_receive_message));
    }          
}