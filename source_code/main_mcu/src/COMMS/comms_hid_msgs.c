/* 
 * This file is part of the Mooltipass Project (https://github.com/mooltipass).
 * Copyright (c) 2019 Stephan Mathieu
 * 
 * This program is free software: you can redistribute it and/or modify  
 * it under the terms of the GNU General Public License as published by  
 * the Free Software Foundation, version 3.
 *
 * This program is distributed in the hope that it will be useful, but 
 * WITHOUT ANY WARRANTY; without even the implied warranty of 
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU 
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License 
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
 */
/*!  \file     comms_hid_msgs.h
*    \brief    HID communications
*    Created:  06/03/2018
*    Author:   Mathieu Stephan
*/
#include <asf.h>
#include <string.h>
#include "smartcard_highlevel.h"
#include "logic_encryption.h"
#include "logic_smartcard.h"
#include "gui_dispatcher.h"
#include "comms_hid_msgs.h"
#include "logic_security.h"
#include "comms_aux_mcu.h"
#include "driver_timer.h"
#include "logic_device.h"
#include "gui_prompts.h"
#include "logic_power.h"
#include "logic_user.h"
#include "custom_fs.h"
#include "text_ids.h"
#include "nodemgmt.h"
#include "dbflash.h"
#include "utils.h"
#include "main.h"
#include "dma.h"
#include "rng.h"


/*! \fn     comms_hid_msgs_fill_get_status_message_answer(uint16_t* msg_array_uint16)
*   \brief  Fill an array with the answer to the HID get_status message
*   \param  msg_array_uint16    4 bytes long array where the answer should be stored
*   \return payload size
*/
uint16_t comms_hid_msgs_fill_get_status_message_answer(uint16_t* msg_array_uint16)
{
    uint8_t* msg_array_uint8 = (uint8_t*)msg_array_uint16;
    msg_array_uint16[1] = 0x0000;
    msg_array_uint16[0] = 0x0000;
    
    // Last bit: is card inserted
    if (smartcard_low_level_is_smc_absent() != RETURN_OK)
    {
        msg_array_uint8[0] |= 0x01;
    }
    // Unlocking screen 0x02: not implemented on this device
    // Smartcard unlocked
    if (logic_security_is_smc_inserted_unlocked() != FALSE)
    {
        msg_array_uint8[0] |= 0x04;
    }
    // Unknown card
    if (gui_dispatcher_get_current_screen() == GUI_SCREEN_INSERTED_UNKNOWN)
    {
        msg_array_uint8[0] |= 0x08;
    }
    // Management mode (useful when dealing with multiple interfaces)
    if (logic_security_is_management_mode_set() != FALSE)
    {
        msg_array_uint8[0] |= 0x10;
    }
    
    /* If user logged in, send user security preferences */
    if (logic_security_is_smc_inserted_unlocked() != FALSE)
    {
        msg_array_uint16[1] = logic_user_get_user_security_flags();
        return 4;
    }
    else
    {
        return 1;
    }
}

/*! \fn     comms_hid_msgs_parse(hid_message_t* rcv_msg, uint16_t supposed_payload_length, hid_message_t* send_msg, msg_restrict_type_te answer_restrict_type, BOOL is_message_from_usb)
*   \brief  Parse an incoming message from USB or BLE
*   \param  rcv_msg                 Received message
*   \param  supposed_payload_length Supposed payload length
*   \param  send_msg                Where to write a possible reply
*   \param  answer_restrict_type    Enum restricting which messages we can answer
*   \param  is_message_from_usb     Boolean set to TRUE if message comes from USB
*   \return something >= 0 if an answer needs to be sent, otherwise -1
*/
int16_t comms_hid_msgs_parse(hid_message_t* rcv_msg, uint16_t supposed_payload_length, hid_message_t* send_msg, msg_restrict_type_te answer_restrict_type, BOOL is_message_from_usb)
{    
    /* Check correct payload length */
    if ((supposed_payload_length != rcv_msg->payload_length) || (supposed_payload_length > sizeof(rcv_msg->payload)))
    {
        /* Silent error */
        return -1;
    }
    
    /* Store received message type in case one of the routines below does some communication */
    uint16_t max_payload_size = MEMBER_ARRAY_SIZE(hid_message_t,payload);
    uint16_t rcv_message_type = rcv_msg->message_type;
    BOOL is_aes_gcm_message = FALSE;
    
    /* Check if it's a AES-GCM message */
    if ((rcv_msg->message_type & HID_MESSAGE_AES_GCM_BITMASK) != 0)
    {
        /* If so, remove the bitmask, reduce max payload size and set the bool */
        rcv_msg->message_type &= ~HID_MESSAGE_AES_GCM_BITMASK;
        max_payload_size-= HID_MESSAGE_GCM_TAG_LGTH;
        is_aes_gcm_message = TRUE;
    }
    
    /* Checks based on restriction type */
    BOOL should_ignore_message = FALSE;
    if ((answer_restrict_type == MSG_RESTRICT_ALL) && (rcv_msg->message_type != HID_CMD_ID_PING))
    {
        should_ignore_message = TRUE;
    }
    if ((answer_restrict_type == MSG_RESTRICT_ALLBUT_CANCEL) && (rcv_msg->message_type != HID_CMD_ID_PING) && (rcv_msg->message_type != HID_CMD_ID_CANCEL_REQ))
    {
        should_ignore_message = TRUE;
    }
    // TODO2: deal with all but bundle
    
    /* Depending on restriction, answer please retry */
    if (should_ignore_message != FALSE)
    {
        /* Send please retry */
        send_msg->message_type = HID_CMD_ID_RETRY;
        send_msg->payload_length = 0;
        return 0;
    }
    
    /* Check for commands for management mode */
    if ((rcv_msg->message_type >= HID_FIRST_CMD_FOR_MMM) && (rcv_msg->message_type <= HID_LAST_CMD_FOR_MMM) && (logic_security_is_management_mode_set() == FALSE))
    {
        /* Set nack, leave same command id */
        send_msg->message_type = rcv_message_type;
        send_msg->payload[0] = HID_1BYTE_NACK;
        send_msg->payload_length = 1;
        return 1;
    }
    
    /* Switch on command id */
    switch (rcv_msg->message_type)
    {
        case HID_CMD_ID_PING:
        {
            /* Simple ping: copy the message contents */
            memcpy((void*)send_msg->payload, (void*)rcv_msg->payload, rcv_msg->payload_length);
            send_msg->payload_length = rcv_msg->payload_length;
            send_msg->message_type = rcv_message_type;
            return send_msg->payload_length;
        }

        case HID_CMD_GET_DEVICE_STATUS:
        {
            /* Get device status: call dedicated function */
            send_msg->payload_length = comms_hid_msgs_fill_get_status_message_answer(send_msg->payload_as_uint16);
            send_msg->message_type = rcv_message_type;
            return send_msg->payload_length;
        }
        
        case HID_CMD_ID_PLAT_INFO:
        {
            aux_mcu_message_t* temp_rx_message;
            aux_mcu_message_t* temp_tx_message_pt;
            
            /* Generate our packet */
            comms_aux_mcu_get_empty_packet_ready_to_be_sent(&temp_tx_message_pt, AUX_MCU_MSG_TYPE_PLAT_DETAILS);
            
            /* Wait for current packet reception and arm reception */
            dma_aux_mcu_wait_for_current_packet_reception_and_clear_flag();
            comms_aux_arm_rx_and_clear_no_comms();
            
            /* Send message */
            comms_aux_mcu_send_message(TRUE);
            
            /* Wait for message from aux MCU */
            while(comms_aux_mcu_active_wait(&temp_rx_message, TRUE, AUX_MCU_MSG_TYPE_PLAT_DETAILS, FALSE, -1) != RETURN_OK){}
            
            /* Copy message contents into send packet */
            send_msg->platform_info.main_mcu_fw_major = FW_MAJOR;
            send_msg->platform_info.main_mcu_fw_minor = FW_MINOR;
            send_msg->platform_info.aux_mcu_fw_major = temp_rx_message->aux_details_message.aux_fw_ver_major;
            send_msg->platform_info.aux_mcu_fw_minor = temp_rx_message->aux_details_message.aux_fw_ver_minor;
            send_msg->platform_info.plat_serial_number = 12345678;
            send_msg->platform_info.memory_size = DBFLASH_CHIP;            
            send_msg->payload_length = sizeof(send_msg->platform_info);
            send_msg->message_type = rcv_message_type;
            
            /* Rearm receive */
            comms_aux_arm_rx_and_clear_no_comms();
            
            return sizeof(send_msg->platform_info);            
        }
        
        case HID_CMD_ID_SET_DATE:
        {
            /* Check address length */
            if (rcv_msg->payload_length == 6*sizeof(uint16_t))
            {
                /* Set Date */
                timer_set_calendar(rcv_msg->payload_as_uint16[0], rcv_msg->payload_as_uint16[1], rcv_msg->payload_as_uint16[2], rcv_msg->payload_as_uint16[3], rcv_msg->payload_as_uint16[4], rcv_msg->payload_as_uint16[5]);
                nodemgmt_set_current_date(nodemgmt_construct_date(rcv_msg->payload_as_uint16[0],rcv_msg->payload_as_uint16[1],rcv_msg->payload_as_uint16[2]));
                
                /* Set success byte */
                send_msg->payload[0] = HID_1BYTE_ACK;
            }
            else
            {
                /* Set failure byte */
                send_msg->payload[0] = HID_1BYTE_NACK;
            }
            
            send_msg->message_type = rcv_message_type;
            send_msg->payload_length = 1;
            return 1;
        }
        
        case HID_CMD_ID_GET_32B_RNG:
        {
            send_msg->message_type = rcv_message_type;
            rng_fill_array(send_msg->payload, 32);
            send_msg->payload_length = 32;
            return 32;
        }

        case HID_CMD_GET_CUR_CARD_CPZ:
        {
            /* Smartcard unlocked or unknown card inserted */
            if ((logic_security_is_smc_inserted_unlocked() != FALSE) || (gui_dispatcher_get_current_screen() == GUI_SCREEN_INSERTED_UNKNOWN))
            {
                smartcard_highlevel_read_code_protected_zone(send_msg->payload); 
                send_msg->payload_length = MEMBER_SIZE(cpz_lut_entry_t,cards_cpz);
            }
            else
            {
                /* Set failure byte */
                send_msg->payload[0] = HID_1BYTE_NACK;
                send_msg->payload_length = 1;
            }
            
            send_msg->message_type = rcv_message_type;
            return send_msg->payload_length;            
        }

        case HID_CMD_GET_DEVICE_SETTINGS:
        {
            /* Get a dump of all device settings */
            send_msg->payload_length = custom_fs_settings_get_dump(send_msg->payload);
            send_msg->message_type = rcv_message_type;
            return send_msg->payload_length;
        }

        case HID_CMD_SET_DEVICE_SETTINGS:
        {
            /* Store all settings */
            custom_fs_settings_store_dump(rcv_msg->payload);
            
            /* Actions run when settings are updated */
            sh1122_set_contrast_current(&plat_oled_descriptor, custom_fs_settings_get_device_setting(SETTINGS_MASTER_CURRENT));
            
            /* Apply possible screen inversion */
            BOOL screen_inverted = logic_power_get_power_source() == BATTERY_POWERED?(BOOL)custom_fs_settings_get_device_setting(SETTINGS_LEFT_HANDED_ON_BATTERY):(BOOL)custom_fs_settings_get_device_setting(SETTINGS_LEFT_HANDED_ON_USB);
            sh1122_set_screen_invert(&plat_oled_descriptor, screen_inverted);
            
            /* Set ack, leave same command id */
            send_msg->message_type = rcv_message_type;
            send_msg->payload[0] = HID_1BYTE_ACK;
            send_msg->payload_length = 1;
            return 1;
        }
        
        case HID_CMD_GET_USER_SETTINGS:
        {
            /* Get user security settings */
            if (logic_security_is_smc_inserted_unlocked() != FALSE)
            {
                send_msg->payload_as_uint16[0] = logic_user_get_user_security_flags();
                send_msg->message_type = rcv_message_type;
                send_msg->payload_length = 2;
                return 2;
            } 
            else
            {
                send_msg->message_type = rcv_message_type;
                send_msg->payload_length = 0;
                return 0;
            }
        }
        
        case HID_CMD_GET_CATEGORIES_STR:
        {            
            _Static_assert(sizeof(nodemgmt_user_category_strings_t) == sizeof(nodemgmt_user_category_strings_t), "get/set categories message and db structure aren't the same");
            
            /* Get user categories strings */
            if (logic_security_is_smc_inserted_unlocked() != FALSE)
            {
                send_msg->message_type = rcv_message_type;
                nodemgmt_get_category_strings((nodemgmt_user_category_strings_t*)&send_msg->get_set_cat_strings);
                send_msg->payload_length = sizeof(nodemgmt_user_category_strings_t);
                return sizeof(nodemgmt_user_category_strings_t);
                
            } 
            else
            {
                /* Set nack, leave same command id */
                send_msg->message_type = rcv_message_type;
                send_msg->payload[0] = HID_1BYTE_NACK;
                send_msg->payload_length = 1;
                return 1;
            }
        }
        
        case HID_CMD_SET_CATEGORIES_STR:
        {
            _Static_assert(sizeof(nodemgmt_user_category_strings_t) == sizeof(nodemgmt_user_category_strings_t), "get/set categories message and db structure aren't the same");
            
            /* Set user categories strings */
            if ((rcv_msg->payload_length == sizeof(nodemgmt_user_category_strings_t)) && (logic_security_is_smc_inserted_unlocked() != FALSE))
            {
                if ((logic_security_is_management_mode_set() != FALSE) || (gui_prompts_ask_for_one_line_confirmation(SET_CAT_STRINGS_TEXT_ID, TRUE, FALSE, TRUE) == MINI_INPUT_RET_YES))
                {
                    /* Store category strings */
                    nodemgmt_set_category_strings((nodemgmt_user_category_strings_t*)&rcv_msg->get_set_cat_strings);
                    
                    /* Set ack, leave same command id */
                    send_msg->payload[0] = HID_1BYTE_ACK;
                } 
                else
                {
                    /* Set nack, leave same command id */
                    send_msg->payload[0] = HID_1BYTE_NACK;
                }                  
                
                /* Update screen */
                gui_dispatcher_get_back_to_current_screen();           
            }
            else
            {
                /* Set nack, leave same command id */
                send_msg->payload[0] = HID_1BYTE_NACK;
            }
            
            send_msg->message_type = rcv_message_type;            
            send_msg->payload_length = 1;
            return 1; 
        }    

        case HID_CMD_RESET_UNKNOWN_CARD:
        {
            /* Resetting card if an unknown one is inserted */
            if (gui_dispatcher_get_current_screen() == GUI_SCREEN_INSERTED_UNKNOWN)
            {
                /* Prompt user to unlock the card */
                logic_device_activity_detected();
                if (logic_smartcard_user_unlock_process() == UNLOCK_OK_RET)
                {
                    /* Erase card! */
                    smartcard_highlevel_erase_smartcard();

                    /* Set next screen */
                    gui_dispatcher_set_current_screen(GUI_SCREEN_INSERTED_INVALID, TRUE, GUI_INTO_MENU_TRANSITION);

                    /* Set success byte */
                    send_msg->payload[0] = HID_1BYTE_ACK;
                }
                else
                {
                    /* Set failure byte */
                    send_msg->payload[0] = HID_1BYTE_NACK;
                }

                /* Update screen */
                gui_dispatcher_get_back_to_current_screen();
            }
            else
            {
                /* Set failure byte */
                send_msg->payload[0] = HID_1BYTE_NACK;
            }
            
            send_msg->message_type = rcv_message_type;
            send_msg->payload_length = 1;
            return 1;
        }

        case HID_CMD_GET_NB_FREE_USERS:
        {
            uint8_t temp_uint8;

            /* Get number of free users */
            send_msg->payload[0] = custom_fs_get_nb_free_cpz_lut_entries(&temp_uint8);
            send_msg->message_type = rcv_message_type;
            send_msg->payload_length = 1;
            return 1;
        }

        case HID_CMD_LOCK_DEVICE:
        {
            /* Lock device */
            if (logic_security_is_smc_inserted_unlocked() != FALSE)
            {
                gui_dispatcher_set_current_screen(GUI_SCREEN_INSERTED_LCK, TRUE, GUI_OUTOF_MENU_TRANSITION);
                gui_dispatcher_get_back_to_current_screen();
                logic_smartcard_handle_removed();
                logic_device_set_state_changed();

                /* Set success byte */
                send_msg->payload[0] = HID_1BYTE_ACK;
            }
            else
            {
                /* Set failure byte */
                send_msg->payload[0] = HID_1BYTE_NACK;
            }
            
            send_msg->message_type = rcv_message_type;
            send_msg->payload_length = 1;
            return 1;
        }
        
        case HID_CMD_GET_START_PARENTS:
        {            
            /* Return correct size & data */
            send_msg->payload_length = nodemgmt_get_start_addresses(send_msg->payload_as_uint16)*sizeof(uint16_t);
            send_msg->message_type = rcv_message_type;
            return send_msg->payload_length;
        }

        case HID_CMD_SET_CRED_ST_PARENT:
        {
            /* Check address length */
            if (rcv_msg->payload_length == 2*sizeof(uint16_t))
            {
                /* Store new address */
                nodemgmt_set_cred_start_address(rcv_msg->payload_as_uint16[1], rcv_msg->payload_as_uint16[0]);

                /* Set success byte */
                send_msg->payload[0] = HID_1BYTE_ACK;
            }
            else
            {
                /* Set failure byte */
                send_msg->payload[0] = HID_1BYTE_NACK;
            }
            
            send_msg->message_type = rcv_message_type;
            send_msg->payload_length = 1;
            return 1;
        }

        case HID_CMD_SET_DATA_ST_PARENT:
        {
            /* Check address length */
            if (rcv_msg->payload_length == 2*sizeof(uint16_t))
            {
                /* Store new address */
                nodemgmt_set_data_start_address(rcv_msg->payload_as_uint16[1], rcv_msg->payload_as_uint16[0]);

                /* Set success byte */
                send_msg->payload[0] = HID_1BYTE_ACK;
            }
            else
            {
                /* Set failure byte */
                send_msg->payload[0] = HID_1BYTE_NACK;
            }
            
            send_msg->message_type = rcv_message_type;
            send_msg->payload_length = 1;
            return 1;
        }

        case HID_CMD_SET_START_PARENTS:
        {
            /* Check address length */
            if (rcv_msg->payload_length == (MEMBER_SIZE(nodemgmt_profile_main_data_t, cred_start_addresses) + MEMBER_SIZE(nodemgmt_profile_main_data_t, data_start_addresses)))
            {
                /* Store new addresses */
                nodemgmt_set_start_addresses(rcv_msg->payload_as_uint16);

                /* Set success byte */
                send_msg->payload[0] = HID_1BYTE_ACK;
            }
            else
            {
                /* Set failure byte */
                send_msg->payload[0] = HID_1BYTE_NACK;
            }
            
            send_msg->message_type = rcv_message_type;
            send_msg->payload_length = 1;
            return 1;
        }

        case HID_CMD_GET_FREE_NODES:
        {
            /* Check for correct number of args and that not too many free slots have been requested */
            if ((rcv_msg->payload_length == 3*sizeof(uint16_t)) && (((uint32_t)(rcv_msg->payload_as_uint16[1]) + (uint32_t)(rcv_msg->payload_as_uint16[2])) <= (max_payload_size/sizeof(uint16_t))))
            {
                uint16_t nb_nodes_found = nodemgmt_find_free_nodes(rcv_msg->payload_as_uint16[1], send_msg->payload_as_uint16, rcv_msg->payload_as_uint16[2], &(send_msg->payload_as_uint16[rcv_msg->payload_as_uint16[1]]), nodemgmt_page_from_address(rcv_msg->payload_as_uint16[0]), nodemgmt_node_from_address(rcv_msg->payload_as_uint16[0]));
                
                /* Send free nodes */
                send_msg->payload_length = nb_nodes_found*sizeof(uint16_t);
                send_msg->message_type = rcv_message_type;
                return nb_nodes_found*sizeof(uint16_t);
            }
            else
            {
                /* Send failure */
                send_msg->message_type = rcv_message_type;
                send_msg->payload[0] = HID_1BYTE_NACK;
                send_msg->payload_length = 1;
                return 1;
            }
        }
        
        case HID_CMD_END_MMM:
        {            
            if (logic_security_is_management_mode_set() != FALSE)
            {
                /* Device state is going to change... */
                logic_device_set_state_changed();
                
                /* Clear bool */
                logic_device_activity_detected();
                logic_security_clear_management_mode();
                
                /* Set next screen */
                gui_dispatcher_set_current_screen(GUI_SCREEN_MAIN_MENU, TRUE, GUI_INTO_MENU_TRANSITION);
                gui_dispatcher_get_back_to_current_screen();
                nodemgmt_scan_node_usage();
            }
            
            /* Set ack, leave same command id */
            send_msg->message_type = rcv_message_type;
            send_msg->payload[0] = HID_1BYTE_ACK;
            send_msg->payload_length = 1;
            return 1;
        }
        
        case HID_CMD_START_MMM:
        {            
            /* Smartcard unlocked? */
            if (logic_security_is_smc_inserted_unlocked() != FALSE)
            {                
                /* Prompt the user */
                if (gui_prompts_ask_for_one_line_confirmation(ID_STRING_ENTER_MMM, TRUE, FALSE, TRUE) == MINI_INPUT_RET_YES)
                {
                    /* Device state is going to change... */
                    logic_device_set_state_changed();
                    
                    if ((logic_user_get_user_security_flags() & USER_SEC_FLG_PIN_FOR_MMM) != 0)
                    {
                        /* As the following call takes a little while, cheat */
                        sh1122_fade_into_darkness(&plat_oled_descriptor, OLED_IN_OUT_TRANS);
                        
                        /* Require user to reauth himself */
                        if (logic_smartcard_remove_card_and_reauth_user(FALSE) == RETURN_OK)
                        {
                            /* Approved, set next screen */
                            gui_dispatcher_set_current_screen(GUI_SCREEN_MEMORY_MGMT, TRUE, GUI_INTO_MENU_TRANSITION);
                            
                            /* Update current state */
                            logic_security_set_management_mode();
                            
                            /* Set success byte */
                            send_msg->payload[0] = HID_1BYTE_ACK;
                        } 
                        else
                        {
                            /* Couldn't reauth, set to inserted lock */
                            gui_dispatcher_set_current_screen(GUI_SCREEN_INSERTED_LCK, TRUE, GUI_OUTOF_MENU_TRANSITION);

                            /* Set failure byte */
                            send_msg->payload[0] = HID_1BYTE_NACK;
                        }
                    } 
                    else
                    {
                        /* Approved, set next screen */
                        gui_dispatcher_set_current_screen(GUI_SCREEN_MEMORY_MGMT, TRUE, GUI_INTO_MENU_TRANSITION);
                        
                        /* Update current state */
                        logic_security_set_management_mode();
                        
                        /* Set success byte */
                        send_msg->payload[0] = HID_1BYTE_ACK;
                    }
                }
                else
                {
                    /* Set failure byte */
                    send_msg->payload[0] = HID_1BYTE_NACK;
                }
            
                /* Go back to old or updated screen */
                gui_dispatcher_get_back_to_current_screen();
            } 
            else
            {
                /* Set failure byte */
                send_msg->payload[0] = HID_1BYTE_NACK;
            }
            
            /* Return success or failure */
            send_msg->message_type = rcv_message_type;
            send_msg->payload_length = 1;
            return 1;
        }
        
        case HID_CMD_READ_NODE:
        {
            /* Check address length */
            if (rcv_msg->payload_length == sizeof(uint16_t))
            {
                node_type_te temp_node_type;
                
                /* Check user permission */
                if (nodemgmt_check_user_permission(rcv_msg->payload_as_uint16[0], &temp_node_type) == RETURN_OK)
                {
                    if ((temp_node_type == NODE_TYPE_PARENT) || (temp_node_type == NODE_TYPE_PARENT_DATA) || (temp_node_type == NODE_TYPE_NULL))
                    {
                        /* Read and send parent node */
                        nodemgmt_read_parent_node_data_block_from_flash(rcv_msg->payload_as_uint16[0], (parent_node_t*)send_msg->payload_as_uint16);
                        send_msg->payload_length = sizeof(parent_node_t);
                        send_msg->message_type = rcv_message_type;
                        return sizeof(parent_node_t);
                    } 
                    else
                    {
                        /* Read and send child node */
                        nodemgmt_read_child_node_data_block_from_flash(rcv_msg->payload_as_uint16[0], (child_node_t*)send_msg->payload_as_uint16);
                        send_msg->payload_length = sizeof(child_node_t);
                        send_msg->message_type = rcv_message_type;
                        return sizeof(child_node_t);
                    }
                } 
                else
                {
                    /* Set nack, leave same command id */
                    send_msg->message_type = rcv_message_type;
                    send_msg->payload[0] = HID_1BYTE_NACK;
                    send_msg->payload_length = 1;
                    return 1;
                }
            } 
            else
            {
                /* Set nack, leave same command id */
                send_msg->message_type = rcv_message_type;
                send_msg->payload[0] = HID_1BYTE_NACK;
                send_msg->payload_length = 1;
                return 1;
            }
        }

        case HID_CMD_WRITE_NODE:
        {
            node_type_te temp_node_type_te;

            /* Check for big or small node size */
            if ((rcv_msg->payload_length == sizeof(uint16_t) + sizeof(child_node_t)) \
                    && (nodemgmt_check_user_permission(rcv_msg->payload_as_uint16[0], &temp_node_type_te) == RETURN_OK) \
                    && (nodemgmt_check_user_permission(nodemgmt_get_incremented_address(rcv_msg->payload_as_uint16[0]), &temp_node_type_te) == RETURN_OK))
            {
                /* big node */
                nodemgmt_write_child_node_block_to_flash(rcv_msg->payload_as_uint16[0], (child_node_t*)&(rcv_msg->payload_as_uint16[1]), FALSE);

                /* Set success byte */
                send_msg->payload[0] = HID_1BYTE_ACK;
            }
            else if ((rcv_msg->payload_length == sizeof(uint16_t) + sizeof(parent_node_t)) \
                    && (nodemgmt_check_user_permission(rcv_msg->payload_as_uint16[0], &temp_node_type_te) == RETURN_OK))
            {
                /* small node */
                nodemgmt_write_parent_node_data_block_to_flash(rcv_msg->payload_as_uint16[0], (parent_node_t*)&(rcv_msg->payload_as_uint16[1]));

                /* Set success byte */
                send_msg->payload[0] = HID_1BYTE_ACK;
            }
            else
            {
                /* Set failure byte */
                send_msg->payload[0] = HID_1BYTE_NACK;
            }

            send_msg->message_type = rcv_message_type;
            send_msg->payload_length = 1;
            return 1;
        }

        case HID_CMD_GET_USER_CHANGE_NB :
        {
            /* Smartcard unlocked? */
            if (logic_security_is_smc_inserted_unlocked() != FALSE)
            {
                send_msg->payload_as_uint32[0] = nodemgmt_get_cred_change_number();
                send_msg->payload_as_uint32[1] = nodemgmt_get_data_change_number();
                send_msg->payload_length = 2*sizeof(uint32_t);
            }
            else
            {
                /* Set failure byte */
                send_msg->payload[0] = HID_1BYTE_NACK;
                send_msg->payload_length = 1;
            }

            send_msg->message_type = rcv_message_type;
            return send_msg->payload_length;
        }

        case HID_CMD_SET_CRED_CHANGE_NB :
        {
            /* Check address length */
            if (rcv_msg->payload_length == sizeof(uint32_t))
            {
                /* Store change number */
                nodemgmt_set_cred_change_number(rcv_msg->payload_as_uint32[0]);

                /* Set success byte */                
                send_msg->payload[0] = HID_1BYTE_ACK;
            }
            else
            {
                /* Set failure byte */
                send_msg->payload[0] = HID_1BYTE_NACK;
            }
            
            send_msg->message_type = rcv_message_type;
            send_msg->payload_length = 1;
            return 1;
        }

        case HID_CMD_SET_DATA_CHANGE_NB :
        {
            /* Check address length */
            if (rcv_msg->payload_length == sizeof(uint32_t))
            {
                /* Store change number */
                nodemgmt_set_data_change_number(rcv_msg->payload_as_uint32[0]);

                /* Set success byte */                
                send_msg->payload[0] = HID_1BYTE_ACK;
            }
            else
            {
                /* Set failure byte */
                send_msg->payload[0] = HID_1BYTE_NACK;
            }
            
            send_msg->message_type = rcv_message_type;
            send_msg->payload_length = 1;
            return 1;
        }

        case HID_CMD_GET_CTR_VALUE:
        {
            /* Read CTR value from flash and send it */
            send_msg->message_type = rcv_message_type;
            nodemgmt_read_profile_ctr(send_msg->payload);            
            send_msg->payload_length = MEMBER_SIZE(nodemgmt_profile_main_data_t, current_ctr);
            return MEMBER_SIZE(nodemgmt_profile_main_data_t, current_ctr);
        }

        case HID_CMD_SET_CTR_VALUE:
        {
            if (rcv_msg->payload_length == MEMBER_SIZE(nodemgmt_profile_main_data_t, current_ctr))
            {
                nodemgmt_set_profile_ctr(rcv_msg->payload);

                /* Set success byte */
                send_msg->payload[0] = HID_1BYTE_ACK;         
            }
            else
            {
                /* Set failure byte */
                send_msg->payload[0] = HID_1BYTE_NACK;
            }
            
            send_msg->message_type = rcv_message_type;
            send_msg->payload_length = 1;
            return 1;
        }

        case HID_CMD_SET_FAVORITE:
        {
            if (rcv_msg->payload_length == 4*sizeof(uint16_t))
            {
                nodemgmt_set_favorite(rcv_msg->payload_as_uint16[0], rcv_msg->payload_as_uint16[1], rcv_msg->payload_as_uint16[2], rcv_msg->payload_as_uint16[3]);
                
                /* Set success byte */
                send_msg->payload[0] = HID_1BYTE_ACK;
            }
            else
            {
                /* Set failure byte */
                send_msg->payload[0] = HID_1BYTE_NACK;
            }
            
            send_msg->message_type = rcv_message_type;
            send_msg->payload_length = 1;
            return 1;            
        }

        case HID_CMD_GET_FAVORITE:
        {
            if (rcv_msg->payload_length == 2*sizeof(uint16_t))
            {
                nodemgmt_read_favorite(rcv_msg->payload_as_uint16[0], rcv_msg->payload_as_uint16[1], &(send_msg->payload_as_uint16[0]), &(send_msg->payload_as_uint16[1]));
                send_msg->payload_length = 2*sizeof(uint16_t);
            }
            else
            {
                /* Set failure byte */
                send_msg->payload[0] = HID_1BYTE_NACK;
                send_msg->payload_length = 1;
            }
            
            send_msg->message_type = rcv_message_type;
            return send_msg->payload_length;            
        }

        case HID_CMD_GET_CPZ_LUT_ENTRY:
        {
            send_msg->payload_length = sizeof(cpz_lut_entry_t);
            logic_encryption_get_cpz_lut_entry(send_msg->payload);
            send_msg->message_type = rcv_message_type;
            return send_msg->payload_length;
        }

        case HID_CMD_GET_FAVORITES:
        {
            /* Return correct size & data */
            send_msg->payload_length = nodemgmt_get_favorites(send_msg->payload_as_uint16)*sizeof(favorite_addr_t);
            send_msg->message_type = rcv_message_type;
            return send_msg->payload_length;
        }
        
        case HID_CMD_ID_GET_CRED:
        {
            /* Default answer: Nope! */
            send_msg->message_type = rcv_message_type;
            send_msg->payload_length = 0;
            
            /* Incorrect service name index */
            if (rcv_msg->get_credential_request.service_name_index != 0)
            {
                return 0;
            }
            
            /* Empty service name */
            if (rcv_msg->get_credential_request.concatenated_strings[0] == 0)
            {
                return 0;
            }
            
            /* Max string length */
            uint16_t max_cust_char_length = (max_payload_size \
                                            - sizeof(rcv_msg->get_credential_request.service_name_index) \
                                            - sizeof(rcv_msg->get_credential_request.login_name_index))/sizeof(cust_char_t);
            
            /* Get service string length */
            uint16_t service_length = utils_strnlen(&(rcv_msg->get_credential_request.concatenated_strings[0]), max_cust_char_length);
            
            /* Too long length */
            if (service_length == max_cust_char_length)
            {
                return 0;
            }
            
            /* Reduce max length */
            max_cust_char_length -= (service_length + 1);
            
            /* Is the login specified? */
            if (rcv_msg->get_credential_request.login_name_index == UINT16_MAX)
            { 
                /* Request user to send credential */
                int16_t payload_length = logic_user_usb_get_credential(&(rcv_msg->get_credential_request.concatenated_strings[0]), 0, send_msg);
                if (payload_length < 0)
                {
                    send_msg->message_type = rcv_message_type;
                    return 0;
                }
                else
                {
                    send_msg->message_type = rcv_message_type;
                    send_msg->payload_length = payload_length;
                    return payload_length;
                }                
            } 
            else
            {
                /* Check correct index */
                if (rcv_msg->get_credential_request.login_name_index != service_length + 1)
                {
                    return 0;
                }
                
                /* Check login format */
                uint16_t login_length = utils_strnlen(&(rcv_msg->get_credential_request.concatenated_strings[rcv_msg->get_credential_request.login_name_index]), max_cust_char_length);
                
                /* Too long length */
                if (login_length == max_cust_char_length)
                {
                    return 0;
                }
                
                /* Request user to send credential */
                int16_t payload_length = logic_user_usb_get_credential(&(rcv_msg->get_credential_request.concatenated_strings[0]), &(rcv_msg->get_credential_request.concatenated_strings[rcv_msg->get_credential_request.login_name_index]), send_msg);
                if (payload_length < 0)
                {
                    send_msg->message_type = rcv_message_type;
                    return 0;
                }
                else
                {
                    send_msg->payload_length = payload_length;
                    send_msg->message_type = rcv_message_type;
                    return payload_length;
                }
            }
        }

        case HID_CMD_CHECK_PASSWORD:
        {
            if (timer_has_timer_expired(TIMER_CHECK_PASSWORD, FALSE) != TIMER_EXPIRED)
            {
                /* Timer hasn't expired... do not allow check */
                send_msg->message_type = rcv_message_type;
                send_msg->payload[0] = HID_1BYTE_NA;
                send_msg->payload_length = 1;
                return 1;
            }
            else
            {
                /* Default answer: Nope! */
                send_msg->message_type = rcv_message_type;
                send_msg->payload[0] = HID_1BYTE_NACK;
                send_msg->payload_length = 1;
            
                /********************************/
                /* Here comes the sanity checks */
                /********************************/
            
                /* Incorrect service name index */
                if (rcv_msg->check_credential.service_name_index != 0)
                {
                    return 1;
                }
            
                /* Empty service name */
                if (rcv_msg->check_credential.concatenated_strings[0] == 0)
                {
                    return 1;
                }
            
                /* Sequential order check */
                uint16_t temp_length = 0;
                uint16_t prev_length = 0;
                uint16_t prev_check_index = 0;
                uint16_t current_check_index = 0;
                uint16_t check_indexes[3] = {   rcv_msg->check_credential.service_name_index, \
                                                rcv_msg->check_credential.login_name_index, \
                                                rcv_msg->check_credential.password_index};
                uint16_t max_cust_char_length = (max_payload_size \
                                                - sizeof(rcv_msg->check_credential.service_name_index) \
                                                - sizeof(rcv_msg->check_credential.login_name_index) \
                                                - sizeof(rcv_msg->check_credential.password_index))/sizeof(cust_char_t);
            
                /* Check all fields */                              
                for (uint16_t i = 0; i < ARRAY_SIZE(check_indexes); i++)
                {
                    current_check_index = check_indexes[i];
                
                    /* If index is correct */
                    if (current_check_index != prev_check_index + prev_length)
                    {
                        return 1;
                    }
                    
                    /* Get string length */
                    temp_length = utils_strnlen(&(rcv_msg->check_credential.concatenated_strings[current_check_index]), max_cust_char_length);
                    
                    /* Too long length */
                    if (temp_length == max_cust_char_length)
                    {
                        return 1;
                    }
                    
                    /* Store previous index & length */
                    prev_length = temp_length + 1;
                    prev_check_index = current_check_index;
                    
                    /* Reduce max length */
                    max_cust_char_length -= (temp_length + 1);
                }    
            
                /* Proceed to other logic */
                if (logic_user_check_credential(    &(rcv_msg->check_credential.concatenated_strings[rcv_msg->check_credential.service_name_index]),\
                                                    &(rcv_msg->check_credential.concatenated_strings[rcv_msg->check_credential.login_name_index]),\
                                                    &(rcv_msg->check_credential.concatenated_strings[rcv_msg->check_credential.password_index])) == RETURN_OK)
                {
                    send_msg->payload[0] = HID_1BYTE_ACK;                
                }
                else
                {
                    /* Not a match, arm timer */
                    timer_start_timer(TIMER_CHECK_PASSWORD, CHECK_PASSWORD_TIMER_VAL);
                    send_msg->payload[0] = HID_1BYTE_NACK;
                }
            
                return 1;
            }
        }
        
        case HID_CMD_ID_STORE_CRED:
        {   
            /* Default answer: Nope! */
            send_msg->message_type = rcv_message_type;
            send_msg->payload[0] = HID_1BYTE_NACK;
            send_msg->payload_length = 1;
            
            /********************************/
            /* Here comes the sanity checks */
            /********************************/
            
            /* Incorrect service name index */
            if (rcv_msg->store_credential.service_name_index != 0)
            {
                return 1;
            }
            
            /* Empty service name */
            if (rcv_msg->store_credential.concatenated_strings[0] == 0)
            {
                return 1;
            }
            
            /* Sequential order check */
            uint16_t temp_length = 0;
            uint16_t prev_length = 0;
            uint16_t prev_check_index = 0;
            uint16_t current_check_index = 0;
            uint16_t check_indexes[5] = {   rcv_msg->store_credential.service_name_index, \
                                            rcv_msg->store_credential.login_name_index, \
                                            rcv_msg->store_credential.description_index, \
                                            rcv_msg->store_credential.third_field_index, \
                                            rcv_msg->store_credential.password_index};
            uint16_t max_cust_char_length = (max_payload_size \
                                            - sizeof(rcv_msg->store_credential.service_name_index) \
                                            - sizeof(rcv_msg->store_credential.login_name_index) \
                                            - sizeof(rcv_msg->store_credential.description_index) \
                                            - sizeof(rcv_msg->store_credential.third_field_index) \
                                            - sizeof(rcv_msg->store_credential.password_index))/sizeof(cust_char_t);
            
            /* Check all fields */                              
            for (uint16_t i = 0; i < ARRAY_SIZE(check_indexes); i++)
            {
                current_check_index = check_indexes[i];
                
                /* If index indicates present field */
                if (current_check_index != UINT16_MAX)
                {
                    /* If index is correct */
                    if (current_check_index != prev_check_index + prev_length)
                    {
                        return 1;
                    }
                    
                    /* Get string length */
                    temp_length = utils_strnlen(&(rcv_msg->store_credential.concatenated_strings[current_check_index]), max_cust_char_length);
                    
                    /* Too long length */
                    if (temp_length == max_cust_char_length)
                    {
                        return 1;
                    }
                    
                    /* Store previous index & length */
                    prev_length = temp_length + 1;
                    prev_check_index = current_check_index;
                    
                    /* Reduce max length */
                    max_cust_char_length -= (temp_length + 1);
                }              
            }    
            
            /* Dirty hack for fields not set */
            uint16_t empty_string_index = utils_strlen(rcv_msg->store_credential.concatenated_strings);
            if (rcv_msg->store_credential.login_name_index == UINT16_MAX)
            {
                rcv_msg->store_credential.login_name_index = empty_string_index;
            }
            
            /* In case we only want to update certain fields */
            cust_char_t* description_field_pt = (cust_char_t*)0;
            cust_char_t* third_field_pt = (cust_char_t*)0;
            cust_char_t* password_field_pt = (cust_char_t*)0;
            if (rcv_msg->store_credential.description_index != UINT16_MAX)
            {
                description_field_pt = &(rcv_msg->store_credential.concatenated_strings[rcv_msg->store_credential.description_index]);
            }
            if (rcv_msg->store_credential.third_field_index != UINT16_MAX)
            {
                third_field_pt = &(rcv_msg->store_credential.concatenated_strings[rcv_msg->store_credential.third_field_index]);
            }
            if (rcv_msg->store_credential.password_index != UINT16_MAX)
            {
                password_field_pt = &(rcv_msg->store_credential.concatenated_strings[rcv_msg->store_credential.password_index]);
            }
            
            /* Proceed to other logic */
            if (logic_user_store_credential(    &(rcv_msg->store_credential.concatenated_strings[rcv_msg->store_credential.service_name_index]),\
                                                &(rcv_msg->store_credential.concatenated_strings[rcv_msg->store_credential.login_name_index]),\
                                                description_field_pt, third_field_pt, password_field_pt) == RETURN_OK)
            {
                send_msg->message_type = rcv_message_type;
                send_msg->payload[0] = HID_1BYTE_ACK;                
            }
            
            return 1;
        }
        
        case HID_CMD_GET_DEVICE_LANG_ID:
        {
            /* Get device default language */
            send_msg->payload[0] = custom_fs_settings_get_device_setting(SETTING_DEVICE_DEFAULT_LANGUAGE);
            send_msg->message_type = rcv_message_type;
            send_msg->payload_length = 1;
            return 1;
        }
        
        case HID_CMD_GET_USER_LANG_ID:
        {
            /* Get user language, device default language returned if no user logged in */
            if (logic_security_is_smc_inserted_unlocked() == FALSE)
            {
                send_msg->payload[0] = custom_fs_settings_get_device_setting(SETTING_DEVICE_DEFAULT_LANGUAGE);
                send_msg->message_type = rcv_message_type;
                send_msg->payload_length = 1;
                return 1;               
            }
            else
            {
                send_msg->payload[0] = custom_fs_get_current_language_id();
                send_msg->message_type = rcv_message_type;
                send_msg->payload_length = 1;
                return 1;                
            }
        }
        
        case HID_CMD_GET_USER_KEYB_ID:
        {
            /* Get user keyboard id */
            if (logic_security_is_smc_inserted_unlocked() == FALSE)
            {
                send_msg->message_type = rcv_message_type;
                send_msg->payload_length = 1;
                send_msg->payload[0] = 0;
                return 1;
            } 
            else
            {
                send_msg->payload[0] = custom_fs_get_current_layout_id(rcv_msg->payload_as_uint16[0]);
                send_msg->message_type = rcv_message_type;
                send_msg->payload_length = 1;
                return 1;
            }
        }
        
        case HID_CMD_GET_NB_LANGUAGES:
        {
            send_msg->payload_as_uint16[0] = (uint16_t)custom_fs_get_number_of_languages();
            send_msg->payload_length = sizeof(uint16_t);
            send_msg->message_type = rcv_message_type;
            return sizeof(uint16_t);            
        }
        
        case HID_CMD_GET_NB_LAYOUTS:
        {
            send_msg->payload_as_uint16[0] = (uint16_t)custom_fs_get_number_of_keyb_layouts();
            send_msg->payload_length = sizeof(uint16_t);
            send_msg->message_type = rcv_message_type;
            return sizeof(uint16_t);            
        }
        
        case HID_CMD_GET_LANGUAGE_DESC:
        {
            /* This function checks for out of boundary conditions... */
            if (custom_fs_get_language_description(rcv_msg->payload[0], (cust_char_t*)send_msg->payload_as_uint16) == RETURN_OK)
            {
                send_msg->payload_length = utils_strlen((cust_char_t*)send_msg->payload_as_uint16)*sizeof(uint16_t) + sizeof(uint16_t);
                send_msg->message_type = rcv_message_type;
                return send_msg->payload_length;
            } 
            else
            {
                send_msg->payload_length = sizeof(uint16_t);
                send_msg->message_type = rcv_message_type;
                send_msg->payload_as_uint16[0] = 0;
                return sizeof(uint16_t);
            }
        }
        
        case HID_CMD_GET_LAYOUT_DESC:
        {
            /* This function checks for out of boundary conditions... */
            if (custom_fs_get_keyboard_descriptor_string(rcv_msg->payload[0], (cust_char_t*)send_msg->payload_as_uint16) == RETURN_OK)
            {
                send_msg->payload_length = utils_strlen((cust_char_t*)send_msg->payload_as_uint16)*sizeof(uint16_t) + sizeof(uint16_t);
                send_msg->message_type = rcv_message_type;
                return send_msg->payload_length;
            }
            else
            {
                send_msg->payload_length = sizeof(uint16_t);
                send_msg->message_type = rcv_message_type;
                send_msg->payload_as_uint16[0] = 0;
                return sizeof(uint16_t);
            }
        }
        
        case HID_CMD_CREATE_FILE_ID:
        {
            /* Input sanitazing */
            uint16_t max_cust_char_length = max_payload_size/sizeof(cust_char_t);
            
            /* Get string length */
            uint16_t string_length = utils_strnlen(rcv_msg->payload_as_cust_char_t, max_cust_char_length);
            
            /* Check for valid length, not exceeding payload size, then prompt user */
            if ((string_length < max_cust_char_length) && ((string_length + 1) == (rcv_msg->payload_length / (uint16_t)sizeof(cust_char_t))) && (logic_security_is_smc_inserted_unlocked() == FALSE) && (logic_user_add_data_service(rcv_msg->payload_as_cust_char_t, is_message_from_usb) == RETURN_OK))
            {
                /* Set success byte */
                send_msg->payload[0] = HID_1BYTE_ACK;
            }
            else
            {
                /* Set failure byte */
                send_msg->payload[0] = HID_1BYTE_NACK;
            }
            
            send_msg->message_type = rcv_message_type;
            send_msg->payload_length = 1;
            return 1;
        }
        
        case HID_CMD_SET_USER_KEYB_ID:
        {
            BOOL is_usb_interface_wanted = (BOOL)rcv_msg->payload[0];
            uint8_t desired_layout_id = rcv_msg->payload[1];
            
            /* user logged in, id valid? */
            if ((logic_security_is_smc_inserted_unlocked() != FALSE) && (desired_layout_id < custom_fs_get_number_of_keyb_layouts()))
            {
                custom_fs_set_current_keyboard_id(desired_layout_id, is_usb_interface_wanted);
                logic_user_set_layout_id(desired_layout_id, is_usb_interface_wanted);
                
                /* Set success byte */
                send_msg->payload[0] = HID_1BYTE_ACK;
            }
            else
            {
                /* Set failure byte */
                send_msg->payload[0] = HID_1BYTE_NACK;
            }

            send_msg->message_type = rcv_message_type;
            send_msg->payload_length = 1;
            return 1;
        }
        
        case HID_CMD_SET_USER_LANG_ID:
        {
            uint8_t desired_lang_id = rcv_msg->payload[0];
            
            /* user logged in, id valid? */
            if ((logic_security_is_smc_inserted_unlocked() != FALSE) && (desired_lang_id < custom_fs_get_number_of_languages()))
            {
                custom_fs_set_current_language(desired_lang_id);
                logic_user_set_language(desired_lang_id);
                
                /* Set success byte */
                send_msg->payload[0] = HID_1BYTE_ACK;
            }
            else
            {
                /* Set failure byte */
                send_msg->payload[0] = HID_1BYTE_NACK;
            }

            send_msg->message_type = rcv_message_type;
            send_msg->payload_length = 1;
            return 1;            
        }
        
        case HID_CMD_SET_DEVICE_LANG_ID:
        {
            uint8_t desired_lang_id = rcv_msg->payload[0];
            
            /* user logged in, id valid? */
            if (desired_lang_id < custom_fs_get_number_of_languages())
            {
                custom_fs_set_device_default_language(desired_lang_id);
                
                /* Set success byte */
                send_msg->payload[0] = HID_1BYTE_ACK;
            }
            else
            {
                /* Set failure byte */
                send_msg->payload[0] = HID_1BYTE_NACK;
            }

            send_msg->message_type = rcv_message_type;
            send_msg->payload_length = 1;
            return 1;            
        }
        
        case HID_CMD_ADD_UNKNOWN_CARD_ID:
        {
            /* Check the args, check we're not authenticated, check that the user could unlock the card */
            if ((rcv_msg->payload_length == sizeof(hid_message_setup_existing_user_req_t)) && (gui_dispatcher_get_current_screen() == GUI_SCREEN_INSERTED_UNKNOWN))
            {
                uint8_t temp_buffer[AES_KEY_LENGTH/8];
                logic_device_activity_detected();
                uint8_t new_user_id;
                
                /* Sanity checks */
                _Static_assert(sizeof(temp_buffer) >= SMARTCARD_CPZ_LENGTH, "Invalid buffer reuse");
                
                /* If feature is enabled */
                #ifndef AES_PROVISIONED_KEY_IMPORT_EXPORT_ALLOWED
                rcv_msg->setup_existing_user_req.cpz_lut_entry.use_provisioned_key_flag = 0;
                memset(rcv_msg->setup_existing_user_req.cpz_lut_entry.provisioned_key, 0, MEMBER_SIZE(cpz_lut_entry_t,provisioned_key));
                #endif
                
                /* Read code protected zone to compare with provided one */
                smartcard_highlevel_read_code_protected_zone(temp_buffer);
                
                /* Check that the provided CPZ is the current one, ask the user to unlock the card and check that we can add the user */
                if (    (memcmp(temp_buffer, rcv_msg->setup_existing_user_req.cpz_lut_entry.cards_cpz, SMARTCARD_CPZ_LENGTH) == 0) && \
                        (smartcard_highlevel_check_hidden_aes_key_contents() == RETURN_OK) && \
                        (logic_smartcard_user_unlock_process() == UNLOCK_OK_RET) && \
                        (logic_user_create_new_user_for_existing_card(&rcv_msg->setup_existing_user_req.cpz_lut_entry, rcv_msg->setup_existing_user_req.security_preferences, rcv_msg->setup_existing_user_req.language_id, rcv_msg->setup_existing_user_req.usb_keyboard_id, rcv_msg->setup_existing_user_req.ble_keyboard_id, &new_user_id) == RETURN_OK))
                {
                    /* Initialize user context, also sets user language */
                    logic_user_init_context(new_user_id);
                    
                    /* Fetch pointer to CPZ MCU stored entry (just created) */
                    cpz_lut_entry_t* cpz_stored_entry;
                    custom_fs_get_cpz_lut_entry(temp_buffer, &cpz_stored_entry);
                    
                    /* Init encryption handling */
                    smartcard_highlevel_read_aes_key(temp_buffer);
                    logic_encryption_init_context(temp_buffer, cpz_stored_entry);
                    
                    /* Set smartcard unlock & MMM flags */
                    logic_security_smartcard_unlocked_actions();
                    logic_security_set_management_mode();
                    
                    /* Setup MMM */
                    gui_dispatcher_set_current_screen(GUI_SCREEN_MEMORY_MGMT, TRUE, GUI_INTO_MENU_TRANSITION);
                    
                    /* Clear temp vars */
                    memset((void*)temp_buffer, 0, sizeof(temp_buffer));
                    
                    /* Set success byte */
                    send_msg->payload[0] = HID_1BYTE_ACK;
                }
                else
                {
                    /* Set failure byte */
                    send_msg->payload[0] = HID_1BYTE_NACK;
                }
                
                /* Go back to currently set screen */
                gui_dispatcher_get_back_to_current_screen();
            }
            else
            {
                /* Set failure byte */
                send_msg->payload[0] = HID_1BYTE_NACK;
            }

            send_msg->message_type = rcv_message_type;
            send_msg->payload_length = 1;
            return 1;   
        }
        
        default: break;
    }
    
    return -1;
}
