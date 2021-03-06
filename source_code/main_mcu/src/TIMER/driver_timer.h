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
/*
 * timer.h
 *
 * Created: 19/04/2017 09:38:49
 *  Author: stephan
 */ 


#ifndef DRIVER_TIMER_H_
#define DRIVER_TIMER_H_

#include <asf.h>
#include "defines.h"

/* Structs */
typedef struct
{
    uint32_t timer_val;
    uint32_t flag;
} timerEntry_t;

/* Typedefs */
typedef RTC_MODE2_CLOCK_Type calendar_t;

/* Enums */
typedef enum {TIMER_WAIT_FUNCTS = 0, TIMER_TIMEOUT_FUNCTS = 1, TIMER_USER_INTERACTION = 2, TIMER_SCROLLING = 3, TIMER_ANIMATIONS = 4, TIMER_SCREEN = 5, TIMER_CHECK_PASSWORD = 6, TIMER_BATTERY_ANIM = 7, TIMER_HANDED_MODE_CHANGE = 8, TIMER_AUX_MCU_FLOOD = 9, TIMER_ADC_WATCHDOG = 10, TIMER_DEVICE_ACTION_TIMEOUT = 11, TOTAL_NUMBER_OF_TIMERS} timer_id_te;
typedef enum {TIMER_EXPIRED = 0, TIMER_RUNNING = 1} timer_flag_te;
    
/* Macros */
#ifdef EMULATOR_BUILD
#include <unistd.h>
#define DELAYUS(us)                 usleep(us)
#define DELAYMS(ms)                 usleep((ms)*1000)
#define DELAYMS_8M(ms)              usleep((ms)*1000)
#else

#define CYCLES_IN_DLYTICKS_FUNC     8
#define US_TO_DLYTICKS(us)          (uint32_t)((CPU_SPEED_HF / 1000000UL) * us / CYCLES_IN_DLYTICKS_FUNC)
#define DELAYTICKS(ticks)           {volatile uint32_t n=ticks; while(n--);}                    //takes 8 cycles
#define DELAYUS(us)                 DELAYTICKS(US_TO_DLYTICKS(us))                              //uses 20bytes
#define DELAYMS(ms)                 DELAYTICKS(US_TO_DLYTICKS(ms*1000))                         //uses 20bytes
#define US_TO_DLYTICKS_8M(us)       (uint32_t)((CPU_SPEED_MF / 1000000UL) * us / CYCLES_IN_DLYTICKS_FUNC)
#define DELAYMS_8M(ms)              DELAYTICKS(US_TO_DLYTICKS_8M(ms*1000))                      //uses 20bytes

#endif

/* Prototypes */
void timer_set_calendar(uint16_t year, uint16_t month, uint16_t day, uint16_t hour, uint16_t minute, uint16_t second);
timer_flag_te timer_has_timer_expired(timer_id_te uid, BOOL clear);
void timer_start_timer(timer_id_te uid, uint32_t val);
void timer_get_calendar(calendar_t* calendar_pt);
uint32_t timer_get_timer_val(timer_id_te uid);
BOOL timer_get_mcu_systick(uint32_t* value);
void timer_initialize_timebase(void);
uint32_t timer_get_systick(void);
void timer_delay_ms(uint32_t ms);
void timer_ms_tick(void);

#endif /* TIMER_H_ */