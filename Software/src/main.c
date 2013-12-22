/**
 * @mainpage PICTrak
 *
 * @section overview_sec Overview
 *
 * The PICTrak aims to be a self-contained APRS based tracking system that generates modulated audio (AFSK) to interface
 * with an FM transmitter.  It uses an on-board 4 bit resistor DAC followed by an RC LPF to generate the audio.  It expect
 * NMEA (GGA and RMC strings) in from a GPS on the hardware UART ports.  Currently it's designed to run on a Microchip
 * PIC18F2525 clocked at 32 Mhz although porting to another architecture shouldn't be too difficult (tm) :-)
 *
 *
 * @section copyright_sec Copyright
 *
 * Copyright (c) 2013 Elijah Brown, AD7ZJ
 *
 *
 * @section gpl_sec GNU General Public License
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301 USA
 *
 *
 * @section history_sec Revision History
 *
 *         @subsection v001 V0.01
 *         03 Nov 2013, Initial release.
 *
 */

#include <htc.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "main.h"
#include "tnc.h"
#include "serial.h"
#include "Engineering.h"
#include "led.h"
#include "fifo.h"
#include "gps.h"
#include "mic-e.h"

#define _XTAL_FREQ  32000000
#define ONE_SEC     20

/*
 * Fuse settings
 */
#pragma config OSC      = INTIO67
#pragma config WDT      = OFF
#pragma config BOREN    = OFF
#pragma config DEBUG    = ON
#pragma config MCLRE    = OFF
#pragma config LVP      = OFF

/*
 * Forward definitions
 */
void sysInit(void);
void LedBootBlink(void);

/*
 * Definitions
 *
 */
typedef enum {
    STARTUP,
    GPS_MODE,
    CONSOLE_MODE
} SER_PORT_MODE;

/**
 * Global variables
 */
static uint16_t secCount;

/// Holds the last received byte from the serial port
volatile char serbuff = 0;

/// system 50ms time tick
static uint32_t sysTick;

/// keeps track of the one second tick
static uint32_t oneSecTick;

/// keeps track of the GPS status LED's tick
static uint32_t statusLedOffTick;

/// Keeps track of whether the serial port is in console mode or GPS mode
SER_PORT_MODE serMode;

void SendPosition(GPSData * gps) {
    MicEEncode(gps);
    TncPreparePacket(MicEGetInfoField(), MicEGetDestAddress());
    printf("Lat: %ld Long: %ld\r\n", gps->latitude, gps->longitude);

    // transmit the Mic-E compressed packet
    RadioTX();
    TncSendPacket();
    RadioRX();
}

void SendStatus() {
    char buffer[50];
    strcpy(buffer, ">Balloon www.ansr.org\015");
    TncPreparePacket(buffer, "APRS  ");

    // transmit the packet
    RadioTX();
    TncSendPacket();
    RadioRX();
}

void main(void) {
    sysInit();
    SerialInit();
    GPSData * gps;

    // get the pointer to the GPS data structure
    gps = GpsGetData();

    // configure the TNC
    TncConfigDefault();

    // indicate the system is up and running
    LedBootBlink();

    SetLED(3, 1);
    // wait for someone to press '1' a few times to enter console mode
    putst("Press '1' to enter console mode\r\n");
    while (sysTick < 300) {
        if (serbuff == '1') {
            serMode = CONSOLE_MODE;
            break;
        }
    }
    SetLED(3, 0);

    // if console mode was not selected, default to using the GPS
    if (serMode != CONSOLE_MODE)
        serMode = GPS_MODE;

    while (1) {
        if (serMode == CONSOLE_MODE)
            EngineeringConsole();
        else {
            // Read data from the GPS
            GpsUpdate();

            if (GpsIsDataReady()) {
                switch (gps->seconds) {
                    case 6:
                    case 36:
                        SendPosition(gps);
                        break;

                    case 15:
                        // send a status packet
                        SendStatus();
                        break;
                }

                SetLED(1, 1);
                if (gps->fixType == NoFix)
                    statusLedOffTick = sysTick + 10;
                else
                    statusLedOffTick = sysTick + 2;
            }

            /** 1s tasks **/
            if (sysTick > oneSecTick) {
                //printf("Uptime: %ul\r\n", uptime);
                oneSecTick = sysTick + ONE_SEC;
            }

            // update the GPS status LED
            if (sysTick > statusLedOffTick)
                SetLED(1, 0);
        }
    }
}

/**
 * Blink the LEDs in a pattern to signal a successful boot
 */
void LedBootBlink(void) {
    uint8_t i;
    for (i = 0; i < 3; i++) {
        SetLED(1, 1);
        __delay_ms(10);
        SetLED(1, 0);
        SetLED(2, 1);
        __delay_ms(10);
        SetLED(2, 0);
        SetLED(3, 1);
        __delay_ms(10);
        SetLED(3, 0);
        __delay_ms(10);
        __delay_ms(10);
        __delay_ms(10);
    }
}

void sysInit(void) {
    // port directions: 1=input, 0=output
    LATA = 0x00;
    TRISA = 0b00000000;
    LATB = 0x00;
    TRISB = 0b00000000;
    LATC = 0x00;
    TRISC = 0b11000000;

    // multiply internal 8 MHz clock x4
    OSCCON = 0b01110000;
    PLLEN = 0x01;
    // All digital inputs
    ADCON1 = 0x0F;

    // Clear the timers
    TMR1H = 0x00;
    TMR1L = 0x00;

    // Enable timer 1
    T1CONbits.TMR1ON = 0x1;
    // System clock is not derived from Timer1 Osc
    T1CONbits.T1RUN = 0x0;
    // Timer1 Osc is disabled
    T1CONbits.T1OSCEN = 0x0;
    // Use Fosc/4 for the clock source
    T1CONbits.TMR1CS = 0x0;
    // prescaler of 1:8
    T1CONbits.T1CKPS = 0x3;
    // enable its interrupt
    TMR1IE = 0x01;

    // enable Timer2
    T2CONbits.TMR2ON = 0x1;
    // Prescale of 1:2
    T2CONbits.T2OUTPS0 = 0x1;

    // GIE, PEIE Interrupts
    GIE = 0x1;
    PEIE = 0x1;
    INTCON = 0b11000000;
    RCIE = 0x01;
    secCount = 0;

    // Put the serial port in startup mode
    serMode = STARTUP;

    // zero the uptime
    sysTick = 0;

    sysTick = 0;
    oneSecTick = 0;
    statusLedOffTick = 0;
}

interrupt isr(void) {
    // Serial receive interrupt
    if (RCIF) {
        serbuff = RCREG;
        if (serMode != STARTUP) {
            FifoWrite(serbuff);
        }
    }

    // Timer 1 interrupt every 50ms
    if (TMR1IF) {
        secCount++;
        // Clear interrupt flag & reload timer
        TMR1IF = 0x00;
        TMR1H = 0xC3;
        TMR1L = 0x50;
        sysTick++;
    }
}