/*!  \file     comms_hid_msgs.h
*    \brief    HID communications
*    Created:  06/03/2018
*    Author:   Mathieu Stephan
*/
#include <asf.h>
#include <string.h>
#include "comms_hid_msgs.h" 


/*! \fn     comms_hid_msgs_parse(hid_message_t* rcv_msg, uint16_t supposed_payload_length, hid_message_t* send_msg)
*   \brief  Parse an incoming message from USB or BLE
*   \param  rcv_msg         Received message
*   \param  msg_length      Supposed payload length
*   \param  send_msg        Where to write a possible reply
*   \return something >= 0 if an answer needs to be sent, otherwise -1
*/
int16_t comms_hid_msgs_parse(hid_message_t* rcv_msg, uint16_t supposed_payload_length, hid_message_t* send_msg)
{    
    /* Check correct payload length */
    if ((supposed_payload_length != rcv_msg->payload_length) || (supposed_payload_length > sizeof(rcv_msg->payload)))
    {
        /* Silent error */
        return -1;
    }
    
    /* By default: copy the same CMD identifier for TX message */
    send_msg->message_type = rcv_msg->message_type;
    
    /* Switch on command id */
    switch (rcv_msg->message_type)
    {
        case HID_CMD_ID_PING:
        {
            /* Simple ping: copy the message contents */
            memcpy((void*)send_msg->payload, (void*)rcv_msg->payload, rcv_msg->payload_length);
            send_msg->payload_length = rcv_msg->payload_length;
            return send_msg->payload_length;
        }
        
        default: break;
    }
    
    return -1;
}
