/*
  conv_settings.h
  junoboss

  Created by Jeffrey Martin on 3/22/11.
  Copyright 2011 __MyCompanyName__. All rights reserved.
*/


struct defaults_struct {

    char title[CONV_SYNTHNAME_MAXLEN];
    unsigned int sendchannel:4;
    unsigned int recvchannel:4;
    unsigned int sysex_strlen;
    unsigned int sysex_channel_pos;
    unsigned int channel_nibble_side:1;	/* 0 = left, 1 = right */
    unsigned int sysex_param_pos;
    unsigned int sysex_value_pos;

} defaults;
