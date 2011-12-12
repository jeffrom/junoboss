/*
  Junoboss - A program to do MIDI trickery.

  Created 3/14/11.
  By: Jeff Martin <jeffmartin@gmail.com>
 */


#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <coreMIDI/CoreMIDI.h>
#include <CoreFoundation/CoreFoundation.h>
#include <pthread.h>

#include "main.h"

#define TRUE 1
#define FALSE 0

/* External functions and vars */
extern void load_instrument_settings();
extern void init_fader_buffer();
extern void init_button_buffer();
extern void listen();
extern void display_btns();
extern void midi_init(int choose);

/* Things to free at the end */
extern struct conv_buffer_fader *fader_conv_buf;
extern struct btn_conversion_buffer *btn_conv_buf;
extern struct btn_lastbyte_index *btn_lastbyte;
extern MIDIClientRef client;

extern unsigned long long avg_fader_conv;
extern int avg_fader_count;
extern unsigned long long avg_btn_conv;
extern int avg_btn_count;

/* globals declared in this file */
int verbose = 0;
int changedefaults = 0;


static void
cleanup()
{
    MIDIClientDispose(client);
    free(fader_conv_buf);
    free(btn_conv_buf);
    free(btn_lastbyte);
}

int
main(int argc, char *argv[])
{
    int c;
    while ((c = getopt(argc, argv, "hvd")) != -1) {
	switch (c) {
	case 'd':
	    changedefaults = TRUE;
	    break;
	case 'v':
	    verbose = TRUE;
	    break;
	case 'h':
	default:
	    printf("Usage: junoboss [-d] [-v]\n    [-d] | change default interfaces\n    [-v] | verbose (debug) mode\n");
	    exit(0);
	}
    }

    load_instrument_settings();
    init_fader_buffer();
    init_button_buffer();

    (changedefaults) ? midi_init(MIDI_INIT_NODEFAULTS) : midi_init(MIDI_INIT_DEFAULTS);

    listen();
    cleanup();

    if (avg_fader_count != 0) {
    	printf("Number of fader packets sent: %d\nAverage time: %llu nanos\n", avg_fader_count, avg_fader_conv / avg_btn_count);
    } else {
        printf("No fader messages converted.\n");
    }

    if (avg_btn_count != 0) {
        printf("Number of button packets sent: %d\nAverage time: %llu nanos\n", avg_btn_count, avg_btn_conv / avg_btn_count);
    } else {
        printf("No button messages converted.\n");
    }

    return 0;
}

