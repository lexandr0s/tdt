/*******************************************************************************
 *
 * File: stgfb/Gamma/STRefGDPFilters.h
 * Copyright (c) 2009 by STMicroelectronics. All rights reserved.
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file COPYING in the main directory of this archive for
 * more details.
 *
 * Description : Contains filter coeffs for Horizontal & vertical filters in
 *               latest GDPs, taken from the ST Reference drivers.
 *
 ******************************************************************************/

#ifndef _STREF_GDP_FILTER_H
#define _STREF_GDP_FILTER_H

typedef char  U8;


#define NB_HSRC_FILTERS         10
#define NB_HSRC_PHASES          8
#define NB_HSRC_TAPS            5
#define NB_HSRC_COEFFS          (NB_HSRC_TAPS * NB_HSRC_PHASES)
#define HFILTERS_ALIGN          (16 - (NB_HSRC_COEFFS % 16))
#define HFILTERS_ENTRY_SIZE     (NB_HSRC_COEFFS + HFILTERS_ALIGN)

#define NB_VSRC_FILTERS         7
#define NB_VSRC_PHASES          8
#define NB_VSRC_TAPS            3
#define NB_VSRC_COEFFS          (NB_VSRC_TAPS * NB_VSRC_PHASES)
#define VFILTERS_ALIGN          (16 - (NB_VSRC_COEFFS % 16))
#define VFILTERS_ENTRY_SIZE     (NB_VSRC_COEFFS + VFILTERS_ALIGN)


static const U8 stlayer_HSRC_Coeffs[NB_HSRC_FILTERS * (NB_HSRC_COEFFS + HFILTERS_ALIGN)] = {
 /* Incr <= 1024 (single coeff set for any upsize) */
 0x00,  0x00,  0x40,  0x00,  0x00,
 0x00,  0x06,  0x3D,  0xFD,  0x00,
 0xFE,  0x0F,  0x38,  0xFB,  0x00,
 0xFD,  0x19,  0x2F,  0xFB,  0x00,
 0xFC,  0x24,  0x24,  0xFC,  0x00,
 0xFB,  0x2F,  0x19,  0xFD,  0x00,
 0xFB,  0x38,  0x0F,  0xFE,  0x00,
 0xFD,  0x3D,  0x06,  0x00,  0x00,
 0,0,0,0,0,0,0,0, /* align next table on 16 bytes */


/*   1024 < Incr <= 1331 */
 0xfc,  0x05,  0x3e,  0x05,  0xfc,
 0xf8,  0x0e,  0x3b,  0xff,  0x00,
 0xf5,  0x18,  0x38,  0xf9,  0x02,
 0xf4,  0x21,  0x31,  0xf5,  0x05,
 0xf4,  0x2a,  0x27,  0xf4,  0x07,
 0xf6,  0x30,  0x1e,  0xf4,  0x08,
 0xf9,  0x35,  0x15,  0xf6,  0x07,
 0xff,  0x37,  0x0b,  0xf9,  0x06,
 0,0,0,0,0,0,0,0, /* align next table on 16 bytes */


/*  1331 < Incr <= 1433 */
 0xf8,  0x0a,  0x3c,  0x0a,  0xf8,
 0xf6,  0x12,  0x3b,  0x02,  0xfb,
 0xf4,  0x1b,  0x35,  0xfd,  0xff,
 0xf4,  0x23,  0x30,  0xf8,  0x01,
 0xf6,  0x29,  0x27,  0xf6,  0x04,
 0xf9,  0x2e,  0x1e,  0xf5,  0x06,
 0xfd,  0x31,  0x16,  0xf6,  0x06,
 0x02,  0x32,  0x0d,  0xf8,  0x07,
 0,0,0,0,0,0,0,0, /* align next table on 16 bytes */


/* 1433 < Incr <= 1536 */
 0xf6,  0x0e,  0x38,  0x0e,  0xf6,
 0xf5,  0x15,  0x38,  0x06,  0xf8,
 0xf5,  0x1d,  0x33,  0x00,  0xfb,
 0xf6,  0x23,  0x2d,  0xfc,  0xfe,
 0xf9,  0x28,  0x26,  0xf9,  0x00,
 0xfc,  0x2c,  0x1e,  0xf7,  0x03,
 0x00,  0x2e,  0x18,  0xf6,  0x04,
 0x05,  0x2e,  0x11,  0xf7,  0x05,
 0,0,0,0,0,0,0,0, /* align next table on 16 bytes */


/* 1536 < Incr <= 2048 */
 0xfb,  0x13,  0x24,  0x13,  0xfb,
 0xfd,  0x17,  0x23,  0x0f,  0xfa,
 0xff,  0x1a,  0x23,  0x0b,  0xf9,
 0x01,  0x1d,  0x22,  0x07,  0xf9,
 0x04,  0x20,  0x1f,  0x04,  0xf9,
 0x07,  0x22,  0x1c,  0x01,  0xfa,
 0x0b,  0x24,  0x17,  0xff,  0xfb,
 0x0f,  0x24,  0x14,  0xfd,  0xfc,
 0,0,0,0,0,0,0,0, /* align next table on 16 bytes */


/* 2048 < Incr <= 3072 */
 0x05,  0x10,  0x16,  0x10,  0x05,
 0x06,  0x11,  0x16,  0x0f,  0x04,
 0x08,  0x13,  0x15,  0x0e,  0x02,
 0x09,  0x14,  0x16,  0x0c,  0x01,
 0x0b,  0x15,  0x15,  0x0b,  0x00,
 0x0d,  0x16,  0x13,  0x0a,  0x00,
 0x0f,  0x17,  0x13,  0x08,  0xff,
 0x11,  0x18,  0x12,  0x07,  0xfe,
 0,0,0,0,0,0,0,0, /* align next table on 16 bytes */


/* 3072 < Incr <= 4096 */
 0x09,  0x0f,  0x10,  0x0f,  0x09,
 0x09,  0x0f,  0x12,  0x0e,  0x08,
 0x0a,  0x10,  0x11,  0x0e,  0x07,
 0x0b,  0x11,  0x11,  0x0d,  0x06,
 0x0c,  0x11,  0x12,  0x0c,  0x05,
 0x0d,  0x12,  0x11,  0x0c,  0x04,
 0x0e,  0x12,  0x11,  0x0b,  0x04,
 0x0f,  0x13,  0x11,  0x0a,  0x03,
 0,0,0,0,0,0,0,0, /* align next table on 16 bytes */


/* 4096 < Incr <= 5120 */
 0x0a,  0x0e,  0x10,  0x0e,  0x0a,
 0x0b,  0x0e,  0x0f,  0x0e,  0x0a,
 0x0b,  0x0f,  0x10,  0x0d,  0x09,
 0x0c,  0x0f,  0x10,  0x0d,  0x08,
 0x0d,  0x0f,  0x0f,  0x0d,  0x08,
 0x0d,  0x10,  0x10,  0x0c,  0x07,
 0x0e,  0x10,  0x0f,  0x0c,  0x07,
 0x0f,  0x10,  0x10,  0x0b,  0x06,
 0,0,0,0,0,0,0,0, /* align next table on 16 bytes */


/* 5120 < Incr */
 0x0b,  0x0e,  0x0e,  0x0e,  0x0b,
 0x0b,  0x0e,  0x0f,  0x0d,  0x0b,
 0x0c,  0x0e,  0x0f,  0x0d,  0x0a,
 0x0c,  0x0e,  0x0f,  0x0d,  0x0a,
 0x0d,  0x0f,  0x0e,  0x0d,  0x09,
 0x0d,  0x0f,  0x0f,  0x0c,  0x09,
 0x0e,  0x0f,  0x0e,  0x0c,  0x09,
 0x0e,  0x0f,  0x0f,  0x0c,  0x08,
 0,0,0,0,0,0,0,0 /* align next table on 16 bytes */
};

/*
 * Hmmm the comments from the ref code are for increments in n.8 format, but
 * the GDP hardware is clearly n.8, so we have divded by 4
 */
static const ULONG HSRC_index[] = {256,332,358,384,512,768,1024,1280,2048};


static const U8 stlayer_VSRC_Coeffs[  NB_VSRC_FILTERS * (NB_VSRC_COEFFS + VFILTERS_ALIGN)] = {
    /*filter coefs computed with : inc <=256 */
    0x00  ,  0x40 ,  0x00,
    0x09  ,  0x3D ,  0xFA,
    0x13  ,  0x37,   0xF6,
    0x1D  ,  0x2F,   0xF4,
    0x26  ,  0x26,   0xF4,
    0x2F  ,  0x1B,   0xF6,
    0x36  ,  0x11,   0xF9,
    0x3B  ,  0x08,   0xFD ,
    0,0,0,0,0,0,0,0, /* align next table on 16 bytes */

    /* filter coefs computed with :256<inc <=384 */
    0x0E  ,    0x24 ,  0x0E,
    0x13 ,     0x22 ,  0x0B,
    0x17 ,     0x22,   0x07,
    0x1B,      0x22,   0x03,
    0x20,      0x20,   0x00,
    0x25,      0x1D,   0xFE,
    0x2A,      0x1B,   0xFB,
    0x2F,      0x19,   0xF8 ,
    0,0,0,0,0,0,0,0, /* align next table on 16 bytes */
    /* filter coefs computed with :384<inc <=512 */
    0x12  ,    0x1C ,  0x12,
    0x14 ,     0x1C ,  0x10,
    0x17 ,     0x1B,   0x0E,
    0x19,      0x1C,   0x0B,
    0x1B,      0x1C,   0x09,
    0x1E,      0x1B,   0x07,
    0x21,      0x1A,   0x05,
    0x24,      0x1A,   0x02 ,
    0,0,0,0,0,0,0,0, /* align next table on 16 bytes */
    /* filter coefs computed with :512<inc <=768 */
    0x14  ,    0x18 ,  0x14,
    0x15 ,     0x18 ,  0x13,
    0x16 ,     0x18,   0x12,
    0x17,      0x18,   0x11,
    0x18,      0x18,   0x10,
    0x19,      0x18,   0x0F,
    0x1A,      0x18,   0x0E,
    0x1B,      0x18,   0x0D ,
    0,0,0,0,0,0,0,0, /* align next table on 16 bytes */
    /* filter coefs computed with :768<inc <=1024 */
    0x15  ,    0x16 ,  0x15,
    0x15 ,     0x17 ,  0x14,
    0x16 ,     0x17,   0x13,
    0x17,      0x17,   0x13,
    0x17,      0x17,   0x12,
    0x17,      0x17,   0x12,
    0x18,      0x17,   0x11,
    0x19,      0x16,   0x11 ,
    0,0,0,0,0,0,0,0, /* align next table on 16 bytes */
    /* filter coefs computed with :1024<inc  */
    0x15  ,    0x16 ,  0x15,
    0x15 ,     0x16 ,  0x15,
    0x16 ,     0x16,   0x14,
    0x16,      0x16,   0x14,
    0x17,      0x17,   0x13,
    0x17,      0x16,   0x13,
    0x17,      0x16,   0x13,
    0x17,      0x17,   0x12 ,
    0,0,0,0,0,0,0,0 /* align next table on 16 bytes */

};


static const ULONG VSRC_index[] = {256,384,768,1024,2048};

#endif // _STREF_GDP_FILTER_H
