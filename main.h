/* main.h
   junoboss
   Created by Jeffrey Martin on 3/14/11.
   Copyright 2011 __MyCompanyName__. All rights reserved.
   This is where the defines for basically the entire project will go, except for the ones in bithex.h
*/

#define CONV_MAX_STRLEN         24
#define CONV_PARAMNAME_MAXLEN   24
#define CONV_SYNTHNAME_MAXLEN   36

/* convGetNextProp stuff */
#define CONV_NEXTPROP_STR       2
#define CONV_NEXTPROP_INT       1
#define CONV_NEXTPROP_ERR       0
#define CONV_NEXTPROP_STRSYM    '\"'
#define CONV_NEXTPROP_BMASKSYM  ':'
#define CONV_NEXTPROP_SECTIONSYM '['

/* colon_blow() stuff */
#define MAX_DELIMS              8

#define FADER_SECTION_NAME      "[FADER_BUFFER_START]\n"
#define NUMBER_OF_FADER_SECTION_TOKENS 3
#define FADER_DELIMS            "=,"
#define BTN_DELIMS              "=,"
#define DEFAULTS_DELIMS         "=,"

#define BTN_SECTION_NAME        "[BUTTON_BUFFER_START]\n"

#define DEFAULTS_SECTION_NAME   "[CONVERT_START]\n"
#define MIDI_INIT_DEFAULTS      0
#define MIDI_INIT_NODEFAULTS    1

#define NIBBLE_POS_LEFT         0
#define NIBBLE_POS_RIGHT        1


#define CC_MSG_BYTE             0xB0
#define SX_OPEN_BYTE            0xF0


/* macros to make time conversion less ugly */
#define NS_TO_MS                * 1000000
