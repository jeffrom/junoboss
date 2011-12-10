/*
  conv_faders.c
  junoboss

  Created by Jeffrey Martin on 3/14/11.


  this contains functions that load the fader conversion buffer and do packet interpreting and building with it
*/

#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <stddef.h>
#include <CoreMIDI/CoreMIDI.h>
#include <AvailabilityMacros.h>
#include <CoreAudio/CoreAudioTypes.h>
#include <CoreAudio/CoreAudio.h>
#include <CoreFoundation/CoreFoundation.h>
#include <pthread.h>
#include <mach/mach_time.h>


#include "main.h"
#include "settings.h"

struct conv_buffer_fader {
    /* the fader conversion buffer */
    char paramname[CONV_PARAMNAME_MAXLEN]; /* the parameter's name */
    Byte cc_param_number;       /* cc parameter number */
    Byte sx_param_number;       /* sysex parameter number */
    Byte last_value_byte;       /* the last value byte sent/received on this parameter */
} *fader_conv_buf;

extern struct defaults_struct *defs;

/* xternal functions and global vars */
extern int convGetNumberOfProperties(const char *section);
extern int convGetNextProperty(const char *section, void **paddy);

extern int verbose;

extern FILE *conversionfile;
extern char line[1024];
extern char *token;

extern Byte *sx_fstr;
extern struct defaults_struct *defs;

extern int sx_param_pos;

extern MIDIPacket lastCCpkt;

extern pthread_mutex_t mtx;

/* global vars declared in this file */
unsigned int number_of_fader_params = 0;

/* local functions and static vars */
void init_fader_buffer();
int convCC_SX_fader(Byte parambyte, Byte valuebyte, MIDIPacket *pktToSend);
int convSX_CC_fader(Byte parambyte, Byte valuebyte, MIDIPacket *pktToSend);

/* dump state functions */
Byte convFaderCountSaved();
MIDIPacket *convFaderDumpSaved(MIDIPacket *pkt, MIDIPacketList *pktList, int *count);

/* timing variables */
unsigned long long avg_fader_conv = 0;
int avg_fader_count = 0;


int convCC_SX_fader(Byte parambyte, Byte valuebyte, MIDIPacket *pktToSend)
{
    int cnvStatus = 0;
    /* lock the mutex */
    pthread_mutex_lock(&mtx);

    int i;
    for (i = 0; i < number_of_fader_params; i++)
    {
        if (parambyte == fader_conv_buf[i].cc_param_number)
            /* this is a match, prepare the packet */
        {
            if (verbose)
            {
                printf("CC->SX [%s]: ", fader_conv_buf[i].paramname);
            }
            /* timing things */
            mach_timebase_info_data_t info;
            mach_timebase_info(&info);
            uint64_t duration;
            uint64_t time = mach_absolute_time();
            pktToSend->length = defs->sysex_strlen;

	    int i;
            for (i = 0; i < pktToSend->length; i++)
                if (i != defs->sysex_param_pos && i != defs->sysex_value_pos)
                    pktToSend->data[i] = sx_fstr[i];
            pktToSend->data[defs->sysex_param_pos] = fader_conv_buf[i].sx_param_number;
            pktToSend->data[defs->sysex_value_pos] = valuebyte;
            pktToSend->timeStamp = mach_absolute_time();
            lastCCpkt = *pktToSend;

            /* lastbyte */
            fader_conv_buf[i].last_value_byte = valuebyte;
            /* should now be prepared */
            duration = mach_absolute_time() - time;
	    /* convert duration to nanoseconds */
            duration *= info.numer;
            duration /= info.denom;
            /* averaging */
            avg_fader_count++;
            avg_fader_conv = avg_fader_conv + duration;	/* when will this overflow? */
            if (verbose)
            {
                printf("--> [%d] ", defs->sysex_strlen);
		int i;
                for (i = 0; i < defs->sysex_strlen; i++)
                {
                    if (i == defs->sysex_value_pos)
                        printf("%02d ", pktToSend->data[i]);
                    else
                        printf("%02X ", pktToSend->data[i]);
                }
                printf("|%llu ns", duration);
                printf("\n");
            }

            cnvStatus = 1;
            break;
        }
    }
    pthread_mutex_unlock(&mtx);
    return cnvStatus;
}

int convSX_CC_fader(Byte parambyte, Byte valuebyte, MIDIPacket *pktToSend)
{
    int cnvStatus = 0;
    mach_timebase_info_data_t info;
    mach_timebase_info(&info);
    uint64_t duration;
    uint64_t time = mach_absolute_time();

    pthread_mutex_lock(&mtx);

    int i;
    for (i = 0; i < number_of_fader_params; i++)
    {
        if (fader_conv_buf[i].sx_param_number == parambyte)
        {
            pktToSend->length = 3;
            pktToSend->timeStamp = mach_absolute_time();
            /* no midi channel support yet -- it should go to recvchannel */
            pktToSend->data[0] = CC_MSG_BYTE + (defs->recvchannel - 1);
            pktToSend->data[1] = fader_conv_buf[i].cc_param_number;
            pktToSend->data[2] = valuebyte;
            /* lastbyte */
            fader_conv_buf[i].last_value_byte = valuebyte;

            /* timing */
            duration = mach_absolute_time() - time;
            /* convert duration to nanoseconds */
            duration *= info.numer;
            duration /= info.denom;
            /* averaging */
            avg_fader_count++;
            avg_fader_conv = avg_fader_conv + duration;	/* when will this overflow? */

            if (verbose)
            {
                printf("[%s] --> CC:          ", fader_conv_buf[i].paramname);
		int i;
                for (i = 0; i < 3; i++)
                {
                    if (i != 0)
                        printf("%02d ", pktToSend->data[i]);
                    else
                        printf("%02X ", pktToSend->data[i]);
                }
                printf("|%llu ns", duration);
                printf("\n");
            }
            cnvStatus = 1;
            break;
        }
    }

    pthread_mutex_unlock(&mtx);
    return cnvStatus;
}

/* functions for dumping state */
Byte convFaderCountSaved()
{
    Byte retval = 0;
    int i;
    for (i = 0; i < number_of_fader_params; i++)
	if (fader_conv_buf[i].last_value_byte != 231) /* 231 is the initialization number. max value in midi is 127 */
        retval++;
    return retval;
}

/* this may need some time parameter to sync with the button dump */
MIDIPacket *convFaderDumpSaved(MIDIPacket *pkt, MIDIPacketList *pktList, int *count)
{
    int packetcount = 1;	/* a multiplier for the interval */

    int i;
    for (i = 0; i < number_of_fader_params; i++)
    {
        if (fader_conv_buf[i].last_value_byte != 231)
            /* if the fader has been used at all */
        {
            /* add it to the packetlist */
            MIDIPacket pktToAdd;
            pktToAdd.length = 3;
            pktToAdd.data[0] = CC_MSG_BYTE + (defs->recvchannel - 1);
            pktToAdd.data[1] = fader_conv_buf[i].cc_param_number;
            pktToAdd.data[2] = fader_conv_buf[i].last_value_byte;
            pktToAdd.timeStamp = mach_absolute_time(); /* this needs to be set on an interval * count */
            /* to convert mach_absolute_time to nanos */
            mach_timebase_info_data_t info;
            mach_timebase_info(&info);
            /* convert to nanos */
            pktToAdd.timeStamp *= info.numer;
            pktToAdd.timeStamp /= info.denom;
            /* calculate the scheduling based on interval */
            pktToAdd.timeStamp = pktToAdd.timeStamp + ((5 NS_TO_MS) * packetcount) + 30 NS_TO_MS; /* 5 will be replaced with defs->dump_interval */
	    /* this is correct calculation though */
            packetcount++;

            pkt = MIDIPacketListAdd(pktList,
                                     sizeof(*pktList),
                                     pkt,
                                     pktToAdd.timeStamp,
                                     pktToAdd.length,
                                     &pktToAdd.data[0]);
            *count = *count + 1;
        }
    }
    /* *count = packetcount - 1; */
    return pkt;
}

/* initialization of the buffer */
void init_fader_buffer()
{
#ifdef TIMING
    /* timing stuff, just to test it out for the message relay */
    mach_timebase_info_data_t info;
    mach_timebase_info(&info);
    uint64_t duration;
    uint64_t time = mach_absolute_time();
#endif

    /* first open the file */
    conversionfile = fopen("conversion.txt", "r");
    if (conversionfile == NULL)
    {
        printf("Error: No conversion.txt file.\n");
        exit(1);
    }

    number_of_fader_params = convGetNumberOfProperties(FADER_SECTION_NAME);
    if (verbose)
        printf("Allocating %lu bytes for fader buffer ", (sizeof(*fader_conv_buf) * (number_of_fader_params + 1)));

    fader_conv_buf = malloc((sizeof(*fader_conv_buf) * number_of_fader_params)); /* allocate the fader buffer */

    if (fader_conv_buf == NULL)
        exit(1);

    /* print sizeof the buffer */
    /* printf("%lu bytes\n", sizeof(*fader_conv_buf)); */
    if (verbose)
        printf("to load %d fader parameters... \n", number_of_fader_params);

    int i;
    for (i = 0; i < number_of_fader_params; i++)
    {
        char *p = NULL;        	/* points to the var linked to the char array for paramname */
        Byte *pb = NULL;
        token = NULL;
        convGetNextProperty(FADER_SECTION_NAME, (void **)&p); /* load param name */
        strncpy(&fader_conv_buf[i].paramname[0], p, strlen(p));
        /* err checking -- maybe not neccesary */
        if (!p)
        {
            printf("pointer to paramname == NULL\n");
            exit(1);
        }

        convGetNextProperty(FADER_SECTION_NAME, (void **)&pb); /* load cc param number */
        fader_conv_buf[i].cc_param_number = *pb;
        convGetNextProperty(FADER_SECTION_NAME, (void **)&pb); /* load sysex param number */
        fader_conv_buf[i].sx_param_number = *pb;
        fader_conv_buf[i].last_value_byte = 231; /* a number it will never get, just to check state later */
        /* printf("%s - %d - %d\n", fader_conv_buf[i].paramname, fader_conv_buf[i].cc_param_number, fader_conv_buf[i].sx_param_number); */
    }
    fclose(conversionfile);

#ifdef TIMING
    duration = mach_absolute_time() - time;
    /* convert duration to nanoseconds */
    duration *= info.numer;
    duration /= info.denom;
    printf("time to fill fader buffer: %llu nanos, %f ms\n", duration, (duration / 1000000.00));
#endif
}
