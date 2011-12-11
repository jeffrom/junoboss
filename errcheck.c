/*errcheck.c
  junoboss

  Created by Jeffrey Martin on 3/21/11.
  Copyright 2011 __MyCompanyName__. All rights reserved.
*/


#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <stddef.h>
#include <CoreMIDI/CoreMIDI.h>
#include <CoreFoundation/CoreFoundation.h>

//#include "errcheck.h"

void errCheck(OSStatus err);

void
errCheck(OSStatus err)
{
    if (err != noErr)
    {
        printf("Error: OSStatus code: %ld: ", (long)err);
        switch (err)
        {
            case kMIDIInvalidClient:
                printf("An invalid MIDIClientRef was passed.\n");
                break;
            case kMIDIInvalidPort:
                printf("An invalid MIDIPortRef was passed.\n");
                break;
            case kMIDIWrongEndpointType:
                printf("A source endpoint was passed to a function expecting a destination, or vice versa.\n");
                break;
            case kMIDINoConnection:
                printf("Attempt to close a non-existant connection.\n");
                break;
            case kMIDIUnknownEndpoint:
                printf("An invalid MIDIEndpointRef was passed.\n");
                break;
            case kMIDIUnknownProperty:
                printf("Attempt to query a property not set on the object.\n");
                break;
            case kMIDIWrongPropertyType:
                printf("Attempt to set a property with a value not of the correct type.\n");
                break;
            case kMIDINoCurrentSetup:
                printf("Internal error; there is no current MIDI setup object.\n");
                break;
            case kMIDIMessageSendErr:
                printf("Communication with MIDIServer failed.\n");
                break;
            case kMIDIServerStartErr:
                printf("Unable to start MIDIServer.\n");
                break;
            case kMIDISetupFormatErr:
                printf("Unable to read the saved state.\n");
                break;
            case kMIDIWrongThread:
                printf("A driver is calling a non-I/O function in the server from a thread other than the server's main thread.\n");
                break;
            case kMIDIObjectNotFound:
                printf("The requested object does not exist.\n");
                break;
            case kMIDIIDNotUnique:
                printf("Attempt to set a non-unique kMIDIPropertyUniqueID on an object.\n");
                break;
            default:
                printf("Unknown error.\n");
                break;
        }
        exit(1);
    }
}
