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


#ifndef COMMS_HID_MSGS_H_
#define COMMS_HID_MSGS_H_

#include "comms_aux_mcu_defines.h"
#include "custom_fs.h"
#include "defines.h"

/* Defines */
#define HID_1BYTE_NACK      0x00
#define HID_1BYTE_ACK       0x01
#define HID_1BYTE_NA        0x02

// AES GCM stuff
#define HID_MESSAGE_AES_GCM_BITMASK 0x4000
#define HID_MESSAGE_GCM_TAG_LGTH    16

/* Command defines */
#define HID_CMD_ID_PING             0x0001
#define HID_CMD_ID_RETRY            0x0002
#define HID_CMD_ID_PLAT_INFO        0x0003
#define HID_CMD_ID_SET_DATE         0x0004
#define HID_CMD_ID_CANCEL_REQ       0x0005
#define HID_CMD_ID_STORE_CRED       0x0006
#define HID_CMD_ID_GET_CRED         0x0007
#define HID_CMD_ID_GET_32B_RNG      0x0008
#define HID_CMD_START_MMM           0x0009
#define HID_CMD_GET_USER_CHANGE_NB  0x000A
#define HID_CMD_GET_CUR_CARD_CPZ    0x000B
#define HID_CMD_GET_DEVICE_SETTINGS 0x000C
#define HID_CMD_SET_DEVICE_SETTINGS 0x000D
#define HID_CMD_RESET_UNKNOWN_CARD  0x000E
#define HID_CMD_GET_NB_FREE_USERS   0x000F
#define HID_CMD_LOCK_DEVICE         0x0010
#define HID_CMD_GET_DEVICE_STATUS   0x0011
#define HID_CMD_CHECK_PASSWORD      0x0012
#define HID_CMD_GET_USER_SETTINGS   0x0013
#define HID_CMD_GET_CATEGORIES_STR  0x0014
#define HID_CMD_SET_CATEGORIES_STR  0x0015
#define HID_CMD_GET_USER_LANG_ID    0x0016
#define HID_CMD_GET_DEVICE_LANG_ID  0x0017
#define HID_CMD_GET_USER_KEYB_ID    0x0018
#define HID_CMD_GET_NB_LANGUAGES    0x0019
#define HID_CMD_GET_NB_LAYOUTS      0x001A
#define HID_CMD_GET_LANGUAGE_DESC   0x001B
#define HID_CMD_GET_LAYOUT_DESC     0x001C
#define HID_CMD_SET_USER_KEYB_ID    0x001D
#define HID_CMD_SET_USER_LANG_ID    0x001E
#define HID_CMD_SET_DEVICE_LANG_ID  0x001F
#define HID_CMD_ADD_UNKNOWN_CARD_ID 0x0020
#define HID_CMD_CREATE_FILE_ID      0x0021
#define HID_CMD_ADD_FILE_DATA_ID    0x0022
#define HID_CMD_GET_FILE_DATA_ID    0x0023
// Below: commands requiring MMM
#define HID_CMD_GET_START_PARENTS   0x0100
#define HID_CMD_END_MMM             0x0101
#define HID_CMD_READ_NODE           0x0102
#define HID_CMD_SET_CRED_CHANGE_NB  0x0103
#define HID_CMD_SET_DATA_CHANGE_NB  0x0104
#define HID_CMD_SET_CRED_ST_PARENT  0x0105
#define HID_CMD_SET_DATA_ST_PARENT  0x0106
#define HID_CMD_SET_START_PARENTS   0x0107
#define HID_CMD_GET_FREE_NODES      0x0108
#define HID_CMD_GET_CTR_VALUE       0x0109
#define HID_CMD_SET_CTR_VALUE       0x010A
#define HID_CMD_SET_FAVORITE        0x010B
#define HID_CMD_GET_FAVORITE        0x010C
#define HID_CMD_WRITE_NODE          0x010D
#define HID_CMD_GET_CPZ_LUT_ENTRY   0x010E
#define HID_CMD_GET_FAVORITES       0x010F
// Define used to identify commands
#define HID_FIRST_CMD_FOR_MMM       HID_CMD_GET_START_PARENTS
#define HID_LAST_CMD_FOR_MMM        0x0200

/* Typedefs */
typedef struct
{
    uint8_t aux_mcu_infos[64];
    uint16_t main_mcu_fw_major;
    uint16_t main_mcu_fw_minor;
} hid_message_detailed_plat_info_t;

typedef struct
{
    uint16_t main_mcu_fw_major;
    uint16_t main_mcu_fw_minor;
    uint16_t aux_mcu_fw_major;
    uint16_t aux_mcu_fw_minor;
    uint32_t plat_serial_number;
    uint16_t memory_size;
} hid_message_plat_info_t;

typedef struct  
{
    uint16_t service_name_index;
    uint16_t login_name_index;
    uint16_t description_index;
    uint16_t third_field_index;
    uint16_t password_index;
    cust_char_t concatenated_strings[0];
} hid_message_store_cred_t;

typedef struct
{
    uint16_t service_name_index;
    uint16_t login_name_index;
    cust_char_t concatenated_strings[0];
} hid_message_get_cred_req_t;

typedef struct
{
    uint16_t service_name_index;
    uint16_t login_name_index;
    uint16_t password_index;
    cust_char_t concatenated_strings[0];
} hid_message_check_cred_req_t;

typedef struct
{
    uint16_t login_name_index;
    uint16_t description_index;
    uint16_t third_field_index;
    uint16_t password_index;
    cust_char_t concatenated_strings[0];
} hid_message_get_cred_answer_t;

typedef struct
{
    cust_char_t category_strings[4][33];
} hid_message_get_set_category_strings_t;

typedef struct
{
    cpz_lut_entry_t cpz_lut_entry;
    uint16_t security_preferences;
    uint16_t language_id;
    uint16_t usb_keyboard_id;
    uint16_t ble_keyboard_id;
} hid_message_setup_existing_user_req_t;

typedef struct
{
    uint32_t power_source;
    uint32_t platform_charging;
    uint16_t main_adc_battery_value;
    uint16_t aux_charge_status;
    uint16_t aux_battery_voltage;
    int16_t aux_charge_current;
    uint16_t aux_stepdown_voltage;
    uint16_t aux_dac_register_val;
} hid_message_get_battery_status_t;

typedef struct
{
    uint16_t message_type;
    uint16_t payload_length;
    union
    {
        uint8_t payload[AUX_MCU_MSG_PAYLOAD_LENGTH-sizeof(uint16_t)-sizeof(uint16_t)];
        uint16_t payload_as_uint16[(AUX_MCU_MSG_PAYLOAD_LENGTH-sizeof(uint16_t)-sizeof(uint16_t))/sizeof(uint16_t)];
        uint32_t payload_as_uint32[(AUX_MCU_MSG_PAYLOAD_LENGTH-sizeof(uint16_t)-sizeof(uint16_t))/sizeof(uint32_t)];
        cust_char_t payload_as_cust_char_t[(AUX_MCU_MSG_PAYLOAD_LENGTH-sizeof(uint16_t)-sizeof(uint16_t))/sizeof(cust_char_t)];
        hid_message_detailed_plat_info_t detailed_platform_info;
        hid_message_plat_info_t platform_info;
        hid_message_store_cred_t store_credential;
        hid_message_check_cred_req_t check_credential;
        hid_message_get_battery_status_t battery_status;
        hid_message_get_cred_req_t get_credential_request;
        hid_message_get_cred_answer_t get_credential_answer;
        hid_message_get_set_category_strings_t get_set_cat_strings;
        hid_message_setup_existing_user_req_t setup_existing_user_req;
    };
} hid_message_t;

/* Prototypes */
int16_t comms_hid_msgs_parse(hid_message_t* rcv_msg, uint16_t supposed_payload_length, hid_message_t* send_msg, msg_restrict_type_te answer_restrict_type, BOOL is_message_from_usb);
uint16_t comms_hid_msgs_fill_get_status_message_answer(uint16_t* msg_array_uint16);


#endif /* COMMS_HID_MSGS_H_ */