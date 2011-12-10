/*
  settings.c
  junoboss

  Created by Jeffrey Martin on 3/14/11.
  Copyright 2011 __MyCompanyName__. All rights reserved.

  this loads the miscellaneous instrument settings, such as sysex_string_format, which all the
conv_*.c files determine value and param byte position information from
*/

#include <stdio.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <CoreMIDI/CoreMIDI.h>
#include <CoreFoundation/CoreFoundation.h>

#include "main.h"
#include "settings.h"          	/* for the defaults struct */

/* this (global) struct holds all the defaults for the instrument */

struct defaults_struct *defs = &defaults;

/* global functions */
extern int convGetDefaultSetting(const char *section, const char *setting, void **paddy);

/* global vars declared elsewhere */
extern int verbose;

extern FILE *conversionfile;

/* global variables declared in this file */
Byte *sx_fstr;
Byte cc_fstr[3] = { 0x00, 0x00, 0x00 };
int sysex_param_pos;

/* local functions */
void load_instrument_settings();
void convSetSysexFormatString(char *p, size_t len, Byte *fstr);
int create_sysex_string(char *str);
int create_cc_string();

/* local vars */
int rec = 0;        		/* for recursion */
int reccount = 0;   		/* a counting var for recursive funx */
int recsecondbyte = 0;      	/* counts bytes in the format string creator */


void load_instrument_settings()
{
    char *p;
    int *ip;

    conversionfile = fopen("conversion.txt", "r");
    if (conversionfile == NULL)
    {
        printf("Error: No conversion.txt file.\n");
        exit(1);
    }

    convGetDefaultSetting(DEFAULTS_SECTION_NAME, "TITLE", (void **)&p);
    if (!strncpy(&defaults.title[0], p, strlen(p)))
    {
        printf("Error: strncpy failed to copy %s\n", p);
        exit(1);
    }

    printf("Loading settings for %s\n", defaults.title);
    convGetDefaultSetting(DEFAULTS_SECTION_NAME, "SENDCHANNEL", (void **)&ip);
    defaults.sendchannel = *ip;
    if (verbose)
        printf("SENDCHANNEL = %d\n", defaults.sendchannel);
    convGetDefaultSetting(DEFAULTS_SECTION_NAME, "RECVCHANNEL", (void **)&ip);
    defaults.recvchannel = *ip;
    if (verbose)
        printf("RECVCHANNEL = %d\n", defaults.recvchannel);
    convGetDefaultSetting(DEFAULTS_SECTION_NAME, "SYSEXSTRINGLENGTH", (void **)&ip);
    defaults.sysex_strlen = *ip;
    if (verbose)
        printf("SYSEXSTRINGLENGTH = %d\n", defaults.sysex_strlen);
    sx_fstr = malloc(defaults.sysex_strlen); /* allocate the format string buffer */
    if (verbose)
        printf("Allocated %d bytes for sysex format string\n", defaults.sysex_strlen);
    convGetDefaultSetting(DEFAULTS_SECTION_NAME, "SYSEXSTRINGFORMAT", (void **)&p);
    fclose(conversionfile);
    reccount = 1;       	/* reccount will count spaces in this function to keep track of byte count. string starts w/ at least one byte */

    convSetSysexFormatString(p, strlen(p), sx_fstr);
    if (verbose)
    {
        printf("SYSEXSTRINGFORMAT = %s\n", p);
        printf("Parsed into heap as: ");
	int i;
        for (i = 0; i < defaults.sysex_strlen; i++)
            printf("%02X ", sx_fstr[i]);
        printf("\n");
    }

    if (!create_cc_string())
    {
        printf("Error creating cc string\n");
        exit(1);
    }
}

int create_cc_string()
{
    cc_fstr[0] = 0xB0 + (defaults.sendchannel - 1);
    return 1;
}

void convSetSysexFormatString(char *p, size_t len, Byte *fstr)
/* parse the format string recursively, filling in the positions and stuff */
{
    if (rec >= len)
    {
        rec = 0;
        reccount = 0;
        /* cbyte = 0; */
        return;
    }

    else if (p[rec] == ' ')
    {
        /* counts spaces */
        reccount++;
        recsecondbyte = 0;
    }

    else if (p[rec] == 'L' && p[rec - 1] == ' ')
        /* if the nibble is on the left */
    {
        defaults.channel_nibble_side = NIBBLE_POS_LEFT;
        defaults.sysex_channel_pos = reccount - 1;
        if (verbose)
            printf("channel nibble is on the left in byte %d\n", reccount);
        fstr[reccount - 1] = (defaults.sendchannel - 1) * 16; /* move it over a digit by multiplying by 16... */
        recsecondbyte = 1;
    }

    else if (p[rec] == 'L' && p[rec + 1] == ' ')
    {
        defaults.channel_nibble_side = NIBBLE_POS_RIGHT;
        defaults.sysex_channel_pos = reccount - 1;
        if (verbose)
            printf("channel nibble is on the right in byte %d\n", reccount);
        fstr[reccount - 1] = defaults.sendchannel - 1; /* set the nibble on right side */
        recsecondbyte = 1;
    }

    else if (p[rec] == 'P' && p[rec + 1] == 'P')
    {
        defaults.sysex_param_pos = reccount - 1;
	/*  sx_param_pos = reccount; */
        if (verbose)
            printf("Parameter byte is %d\n", reccount);
        fstr[reccount - 1] = strtol("FF", NULL, 16);
        recsecondbyte = 1;
    }

    else if (p[rec] == 'V' && p[rec + 1] == 'V')
    {
        defaults.sysex_value_pos = reccount - 1;
        if (verbose)
            printf("Value byte is %d\n", reccount);
        fstr[reccount - 1] = strtol("FF", NULL, 16);
        recsecondbyte = 1;
    }

    else
        /* this is just a regular char -- parse it and the next char */
    {
        if (!recsecondbyte)
        {
            fstr[reccount - 1] = (int)strtol(&p[rec], NULL, 16);
            recsecondbyte = 1;
        }
    }
    rec++;
    convSetSysexFormatString(p, len, fstr);
}


