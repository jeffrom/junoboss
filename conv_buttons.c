/*  conv_buttons.c
  junoboss

  Created by Jeffrey Martin on 3/14/11.

  this contains functions that load the button buffer, and do packet
interpreting and building, it also controls the interal button logics
*/

#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <stddef.h>
#include <string.h>
#include <CoreMIDI/CoreMIDI.h>
#include <CoreFoundation/CoreFoundation.h>
#include <mach/mach_time.h>
#include <pthread.h>


#include "main.h"
#include "bithex.h"
#include "settings.h"


struct btn_conversion_buffer {

    char paramname[CONV_PARAMNAME_MAXLEN]; /* the parameter's name */
    Byte cc_param_number;		   /* cc parameter number */
    Byte sx_param_number;		   /* sysex parameter number */
    Byte ison:1;
	Byte ingroup:1;
	Byte group;
    Byte groupamt:7;	      /* how many buttons are in this group */
	Byte onstate;
	Byte index_start:3;
    Byte index_end:3;		 /* must be >= index_start */
    Byte cc_value_range_start:7; /* max is 127, just like a cc value byte */
	Byte cc_value_range_end:7;
    Byte last_cc_value;		/* used for decreasing time to check button states */
    struct btn_conversion_buffer *prevmember; /* pointers to the previous and next button group member */
    struct btn_conversion_buffer *nextmember;

} *btn_conv_buf;

struct btn_lastbyte_index {
    Byte param;
    Byte lastval;
} *btn_lastbyte;


void init_button_buffer();
void display_btns();
int load_cc_ranges(struct btn_conversion_buffer *bcnv);
int calc_cc_start(int gamt, int gmemb);
int calc_cc_end(int gamt, int gmemb);
int get_group_members(struct btn_conversion_buffer *member);
void link_btn_params();
void show_btn_groups();
void create_sx_param_range();
int conv_BtnOn(struct btn_conversion_buffer *pb, Byte *vbyte);
int conv_BtnOff(struct btn_conversion_buffer *pb, Byte *vbyte);
void display_button_states();

int convCC_SX_btn(Byte parambyte, Byte valuebyte, MIDIPacket *pktToSend);
int convSX_CC_btn(Byte parambyte, Byte valuebyte, MIDIPacket *pktToSend);
Byte convSX_btn_getstates(Byte parambyte, Byte valuebyte);
Byte convGetCCValueByte(struct btn_conversion_buffer *pbuf, Byte sxValByte);
Byte convCC_btn_getstates(Byte pbyte);
Byte convSXAppendGroupPacket(MIDIPacket *pkt, struct btn_conversion_buffer *pb, Byte *loc);
Byte convSXAppendNormPacket(MIDIPacket *pkt, struct btn_conversion_buffer *pb, Byte *loc);
Byte convSXAppendSpecPacket(MIDIPacket *pkt, struct btn_conversion_buffer *pb, struct btn_conversion_buffer *pob, Byte *loc);
Byte convSX_GroupBtnOn(struct btn_conversion_buffer *pb);

/* for dumping state */
Byte convBtnCountSaved();
MIDIPacket *convBtnDumpSaved(MIDIPacket *pkt, MIDIPacketList *pktList, int *count);

Byte CC_btnState = 0;           /* for convCC_btn_getstate() */

int btn_sx_rangeamt = 0;	/* will be the number of params in the range */
int avg_btn_count = 0;
unsigned long long avg_btn_conv = 0;
unsigned int number_of_button_params = 0;

extern int number_of_fader_params; /* needed to time state dump correctly right now */
extern int convGetNumberOfProperties(const char *section);
extern int convGetNextProperty(const char *section, void **paddy);
extern void display_bits(Byte x);
extern int convSet_bitMask(Byte bmsk, Byte *dest, int istart, int iend);
extern int convCmp_bitMask(Byte bmsk, Byte src, int istart, int iend);
extern int verbose;
extern FILE *conversionfile;
extern size_t lastbitmasklen;
extern Byte *sx_fstr;
extern struct defaults_struct *defs;
extern pthread_mutex_t mtx;

int convCC_SX_btn(Byte parambyte, Byte valuebyte, MIDIPacket *pktToSend) {
    int cnvStatus = 0;
    int sxparamtosend = 0;
    Byte valbytetosend = 0;
    mach_timebase_info_data_t info;
    mach_timebase_info(&info);
    uint64_t duration;
    uint64_t time = mach_absolute_time();

    pthread_mutex_lock(&mtx);
    int i;
    for (i = 0; i < number_of_button_params; i++) {
        if (btn_conv_buf[i].cc_param_number == parambyte)
            /* this is a cc param match -- now check for behavior(groups/non-group/special) */
        {
            sxparamtosend = btn_conv_buf[i].sx_param_number; /* now we have enough info to fill in which param number in the string */
            /* save state */
            btn_conv_buf[i].last_cc_value = valuebyte;
            if (btn_conv_buf[i].ingroup)
                /*  this is a group button - cycle through group
		    members checking if the valuebyte fits.  right
		    now, this will always trigger on the first group
		    member. */
            {
                /* create a pointer to cycle through group members */
                struct btn_conversion_buffer *pb = &btn_conv_buf[i];
                while (pb->prevmember != NULL
                       || pb->nextmember != NULL)
                    /* cycle through group members */
                {
                    if (btn_conv_buf[i].group == pb->group)
                        /* if this member is in the same group as the one we're checking */
                    {
                        if (valuebyte >= pb->cc_value_range_start
                            && valuebyte <= pb->cc_value_range_end)
                            /* finally, a match */
			    {   /* check if it's already on, if it's not, turn it on */
                            if (!pb->ison)
                            {
                                cnvStatus = conv_BtnOn(pb, &valbytetosend); /* this will set cnvStatus to 1 if it writes stuff */
                                if (verbose)
                                    printf("CC->SX [%s]: ", pb->paramname);
                                goto cc_sx_preppacket; /* get out of this loop */
                            }
                            else goto cc_sx_preppacket;	/* we can return 0 now, there's nothing to do */
                        }
                        else
                            /* this one's not a match -- set pb to pb->nextmember */
                            if (pb->nextmember != NULL)
                                pb = pb->nextmember;
                    }
                    else
                        /* we've been led astray -- this is no longer a group member we're checking */
                    {
                        break;
                    }
                }
            }

            else {
                /* not in a group */
                if (btn_conv_buf[i].nextmember != NULL
                    && btn_conv_buf[i].prevmember == NULL)
                    /* this will trigger on the first special group member */
		    {   /* cycle through all the members then break this thang. */
                    struct btn_conversion_buffer *bp = &btn_conv_buf[i];
                    /* check values */
                    if (valuebyte >= 0 && valuebyte <= bp->cc_value_range_start)
                        /* this is position 0 (both off) */
                    {
                        if (bp->ison)
                        {
                            if (verbose)
                                printf("CC->SX [%s]: ", bp->paramname);
                            cnvStatus = conv_BtnOff(bp, &valbytetosend);
                        }
                        if (bp->nextmember->ison)
                        {
                            if (verbose)
                                printf("CC->SX [%s]: ", bp->nextmember->paramname);
                            cnvStatus = conv_BtnOff(bp->nextmember, &valbytetosend);
                        }
                        goto cc_sx_preppacket;
                        break;
                    }

                    if (valuebyte > bp->cc_value_range_start
                        && valuebyte <= bp->nextmember->cc_value_range_start)
                        /* this is position 1 (pulse on saw off) */
                    {
                        if (!bp->ison)
                        {
                            if (verbose)
                                printf("CC->SX [%s]: ", bp->paramname);
                            cnvStatus = conv_BtnOn(bp, &valbytetosend);
                        }
                        if (bp->nextmember->ison)
                        {
                            if (verbose)
                                printf("CC->SX [%s]: ", bp->nextmember->paramname);
                            cnvStatus = conv_BtnOff(bp->nextmember, &valbytetosend);
                        }
                        goto cc_sx_preppacket;
                        break;
                    }

                    if (valuebyte > bp->nextmember->cc_value_range_start
                        && valuebyte <= bp->cc_value_range_end)
                        /* position 2 -- (pulse off saw on) */
                    {
                        if (bp->ison)
                        {
                            if (verbose)
                                printf("CC->SX [%s]: ", bp->paramname);
                            cnvStatus = conv_BtnOff(bp, &valbytetosend);
                        }
                        if (!bp->nextmember->ison)
                        {
                            if (verbose)
                                printf("CC->SX [%s]: ", bp->nextmember->paramname);
                            cnvStatus = conv_BtnOn(bp->nextmember, &valbytetosend);
                        }
                        goto cc_sx_preppacket;
                        break;
                    }

                    if (valuebyte > bp->cc_value_range_end && valuebyte <= 127)
                        /* pos 3 -- both on */
                    {
                        if (!bp->ison)
                        {
                            if (verbose)
                                printf("CC->SX [%s]: ", bp->paramname);
                            cnvStatus = conv_BtnOn(bp, &valbytetosend);
                        }
                        if (!bp->nextmember->ison)
                        {
                            if (verbose)
                                printf("CC->SX [%s]: ", bp->nextmember->paramname);
                            cnvStatus = conv_BtnOn(bp->nextmember, &valbytetosend);
                        }
                        goto cc_sx_preppacket;
                        break;
                    }
                }

                if (btn_conv_buf[i].nextmember == NULL
                    && btn_conv_buf[i].prevmember == NULL)
                    /* not in a group or a special group (0-63 == off, 64-127 == on) */
                {
                    if (valuebyte < 64)
                    {
                        /* off */
                        if (btn_conv_buf[i].ison)
                        {
                            if (verbose)
                                printf("CC->SX [%s]: ", btn_conv_buf[i].paramname);
                            cnvStatus = conv_BtnOff(&btn_conv_buf[i], &valbytetosend);
                        }
                        break;
                    }
                    else
                    {
                        /* on */
                        if (!btn_conv_buf[i].ison)
                        {
                            if (verbose)
                                printf("CC->SX [%s]: ", btn_conv_buf[i].paramname);
                            cnvStatus = conv_BtnOn(&btn_conv_buf[i], &valbytetosend);
                        }
                        break;
                    }
                }
            }
        }
    }
    /* if there's a packet to send, prepare it */
cc_sx_preppacket:

    if (cnvStatus)
        /* if cnvStatus == 1, we know we have a button to turn on that isn't on now */
    {
        pktToSend->length = defs->sysex_strlen;
	int i;
        for (i = 0; i < pktToSend->length; i++)
            if (i != defs->sysex_param_pos && i != defs->sysex_value_pos)
                pktToSend->data[i] = sx_fstr[i];
        pktToSend->data[defs->sysex_param_pos] = sxparamtosend;
        pktToSend->data[defs->sysex_value_pos] = valbytetosend;
        pktToSend->timeStamp = mach_absolute_time();

        /* set lastbyte */
        for (i = 0; i < btn_sx_rangeamt; i++)
        {
            if (sxparamtosend == btn_lastbyte[i].param)
            {
                btn_lastbyte[i].lastval = valbytetosend;
                break;
            }
        }
        duration = mach_absolute_time() - time;
        /* convert duration to nanoseconds */
        duration *= info.numer;
        duration /= info.denom;
        /* timing */
        avg_btn_count++;
        avg_btn_conv = avg_btn_conv + duration;

        if (verbose)
        {
            printf("--> [%d]: ", defs->sysex_strlen);
	    int i;
            for (i = 0; i < defs->sysex_strlen; i++)
            {
                if (i == defs->sysex_value_pos)
                    printf("%02d ", pktToSend->data[i]);
                else
                    printf("%02X ", pktToSend->data[i]);
            }
            printf("|%f ms", duration / 1000000.00);
            printf("\n");
            display_bits(valbytetosend);
        }
    }

 retbtn_cc_sx:
    pthread_mutex_unlock(&mtx);
    return cnvStatus;
}

int convSX_CC_btn(Byte parambyte, Byte valuebyte, MIDIPacket *pktToSend) {
    int retval = 0;
    /* set pktToSend->length to 0 now to make sure it gets done */
    pktToSend->length = 0;
    mach_timebase_info_data_t info;
    mach_timebase_info(&info);
    uint64_t duration;
    uint64_t time = mach_absolute_time();
    pthread_mutex_lock(&mtx);
    /* scan state -- first narrow it down to param byte */
    Byte rangei;

    Byte i;
    for (i = 0; i < btn_sx_rangeamt; i++)
    {
        if (parambyte == btn_lastbyte[i].param)
        {
            rangei = i;		/* now rangei is the index number for the appropriate param in the lastbyte struct */
            break;
        }
    }

    /* now scan the lastbyte against the new valuebyte and get all the
       changes create a Byte that = 0 and flip the bit if that index
       number has to change */
    Byte changesbyte = 0;
    Byte morethanonechange = 0;
    Byte indexrange[btn_sx_rangeamt];
    Byte rangecount = 0;
    Byte pktLoc = 0;

    for (i = 0; i < sizeof(Byte) * 8; i++)
        if (GET_BIT(btn_lastbyte[rangei].lastval, i) != GET_BIT(valuebyte, i))
        {
            FLP_BIT(changesbyte, i);
                morethanonechange++;
		indexrange[rangecount] = i;	/* record the index number that has changed in the byte array */
		indexrange[rangecount + 1] = 9;	/* this will be for error checking */
            rangecount++;
        }
    /* if there are multiple changes, try stuffing it all into one MIDIPacket :) */
    /* iterate through changesbyte */
    /* printf("Received these change requests from param # %02X:\n", btn_lastbyte[rangei].param); */
    /* display_bits(valuebyte); */
    /* printf("bit locations with a \"1\" will be changed:\n"); */
    /* display_bits(changesbyte); */

    for (i = 0; i < 8; i++)
        /* iterate through changebyte */
    {
        if (GET_BIT(changesbyte, i))
            /* this is a change -- find the btn_conv member, update state, and append a packet */
        {
            /* first locate the buffer member by index number */
	    Byte ii;
            for (ii = 0; ii < number_of_button_params; ii++)
            {
                if (btn_conv_buf[ii].index_start == i
                    && btn_conv_buf[ii].index_end == 0
                    && btn_conv_buf[ii].sx_param_number == btn_lastbyte[rangei].param)
                    /* this will capture any non groups bitmask--check for special group and update state accordingly */
                {
                    if (btn_conv_buf[ii].nextmember == NULL && btn_conv_buf[ii].prevmember == NULL)
                        /* normal button */
                    {
                        /* just flip the state -- lastbyte should keep it from repeating */
                        FLP_BIT(btn_conv_buf[ii].ison, 0);
                        /* append the packet */
                        convSXAppendNormPacket(pktToSend, &btn_conv_buf[ii], &pktLoc);
                        /* save state */
                        btn_conv_buf[ii].last_cc_value = pktToSend->data[pktLoc - 1];
                        if (verbose)
                            printf("SX->CC: %s(%d) --> %02X %02d %02d\n", btn_conv_buf[ii].paramname
                                   , btn_conv_buf[ii].ison
                                   , pktToSend->data[pktLoc - 3]
                                   , pktToSend->data[pktLoc - 2]
                                   , pktToSend->data[pktLoc - 1]);
                    }
                    /* now special group members - will trigger on first member first */

                    else
			/* check for next/prevmember & corresponding changebyte index no. & ison */
                    {
                        if (btn_conv_buf[ii].nextmember != NULL
                            && !btn_conv_buf[ii].nextmember->group
                            && GET_BIT(changesbyte, btn_conv_buf[ii].nextmember->index_start))
                            /* flip both */
                        {
                            FLP_BIT(btn_conv_buf[ii].ison, 0);
                            FLP_BIT(btn_conv_buf[ii].nextmember->ison, 0);
                            convSXAppendSpecPacket(pktToSend, &btn_conv_buf[ii], btn_conv_buf[ii].nextmember, &pktLoc);
                            /* save state */
                            btn_conv_buf[ii].last_cc_value = pktToSend->data[pktLoc - 1];
                            if (verbose)
                                printf("SX->CC: %s(%d)|%s(%d) --> %02X %02d %02d\n"
                                       , btn_conv_buf[ii].paramname
                                       , btn_conv_buf[ii].ison
                                       , btn_conv_buf[ii].nextmember->paramname
                                       , btn_conv_buf[ii].nextmember->ison
                                       , pktToSend->data[pktLoc - 3]
                                       , pktToSend->data[pktLoc - 2]
                                       , pktToSend->data[pktLoc - 1]);
                        }
                        else if (btn_conv_buf[ii].nextmember != NULL
                                 && !btn_conv_buf[ii].nextmember->group
                                 && !GET_BIT(changesbyte, btn_conv_buf[ii].nextmember->index_start))
                            /* flip 1 (pulse), dont flip 2 (saw) */
                        {
                            FLP_BIT(btn_conv_buf[ii].ison, 0);
                            convSXAppendSpecPacket(pktToSend, &btn_conv_buf[ii], btn_conv_buf[ii].nextmember, &pktLoc);
                            /* save state */
                            btn_conv_buf[ii].last_cc_value = pktToSend->data[pktLoc - 1];
                            if (verbose)
                                printf("SX->CC: %s(%d) --> %02X %02d %02d\n", btn_conv_buf[ii].paramname
                                       , btn_conv_buf[ii].ison
                                       , pktToSend->data[pktLoc - 3]
                                       , pktToSend->data[pktLoc - 2]
                                       , pktToSend->data[pktLoc - 1]);
                        }
                        else if (btn_conv_buf[ii].prevmember != NULL
                                 && !btn_conv_buf[ii].prevmember->group
                                 && !GET_BIT(changesbyte, btn_conv_buf[ii].prevmember->index_start))
                            /* dont flip 1 (pulse), flip 2 (saw) */
                        {
                            FLP_BIT(btn_conv_buf[ii].ison, 0);
                            convSXAppendSpecPacket(pktToSend, btn_conv_buf[ii].prevmember, &btn_conv_buf[ii], &pktLoc);
                            /* save state */
                            btn_conv_buf[ii].last_cc_value = pktToSend->data[pktLoc - 1];
                            if (verbose)
                                printf("SX->CC: %s(%d) --> %02X %02d %02d\n", btn_conv_buf[ii].paramname
                                       , btn_conv_buf[ii].ison
                                       , pktToSend->data[pktLoc - 3]
                                       , pktToSend->data[pktLoc - 2]
                                       , pktToSend->data[pktLoc - 1]);
                        }
                    }
                }

                else if (i >= btn_conv_buf[ii].index_start
                         && i < btn_conv_buf[ii].index_end
                         && btn_conv_buf[ii].sx_param_number == btn_lastbyte[rangei].param)
                    /* falls within a group bitmask--compare bitmask to lastbyte to figure out which member */
		    {   /* this should land on the first group member first */
			int iii;
                    for (iii = ii; btn_conv_buf[iii].group == btn_conv_buf[ii].group; iii++)
                        /* because it will trigger first group member first, we can figure it out in one pass from the first member */
                    {
                        if (convCmp_bitMask(btn_conv_buf[iii].onstate, valuebyte, btn_conv_buf[iii].index_start, btn_conv_buf[iii].index_end))
                            /* it's a match */
                        {
                            /* finally check that the thing isn't on already before appending the packet */
                            if (!btn_conv_buf[iii].ison)
                            {
                                /* append packet */
                                convSXAppendGroupPacket(pktToSend, &btn_conv_buf[iii], &pktLoc);
                                /* save state */
                                btn_conv_buf[ii].last_cc_value = pktToSend->data[pktLoc - 1];
                                if (verbose)
                                    printf("SX->CC: %s --> %02X %02d %02d\n", btn_conv_buf[iii].paramname
                                           , pktToSend->data[pktLoc - 3]
                                           , pktToSend->data[pktLoc - 2]
                                           , pktToSend->data[pktLoc - 1]);
                                /* change states */
                                convSX_GroupBtnOn(&btn_conv_buf[iii]);
                            }
                        }
                        if (iii + 1 >= number_of_button_params)
                            break;
                    }
                }
            }
        }
    }

    if (changesbyte != 0)
        /* there are changes */
    {
        /* set lastbyte */
	Byte i;
        for (i = 0; i < btn_sx_rangeamt; i++)
        {
            if (parambyte == btn_lastbyte[i].param)
            {
                btn_lastbyte[i].lastval = valuebyte;
                break;
            }
        }
        pktToSend->timeStamp = mach_absolute_time();
        duration = mach_absolute_time() - time;
        /* convert duration to nanoseconds */
        duration *= info.numer;
        duration /= info.denom;
        avg_btn_count++;
        avg_btn_conv = avg_btn_conv + duration;
        retval = 1;
    }
    pthread_mutex_unlock(&mtx);
    return retval;
    /* ret 0 = nothing to convert, ret 1 = cc message(s) to send */
}

Byte convSX_GroupBtnOn(struct btn_conversion_buffer *pb)
/* set pb on and it's fellow group members off. it also sets the last valbyte properly */
{
    Byte retByte = 0;
    /* first set pb on */
    pb->ison = 1;
    /* check nextmembers */
    struct btn_conversion_buffer *ppb = pb;

    if (ppb->nextmember != NULL && ppb->nextmember->group == pb->group)
        /* go through nextmembers */
    {
        ppb = ppb->nextmember;
        if (ppb->group == pb->group)
            ppb->ison = 0;
        while (ppb->nextmember != NULL)
        {
            ppb = ppb->nextmember;
            if (ppb->group == pb->group)
                ppb->ison = 0;
        }
    }

    /* check prevmembers */
    ppb = pb;
    if (ppb->prevmember != NULL && ppb->prevmember->group == pb->group)
    {
        ppb = ppb->prevmember;
        if (ppb->group == pb->group)
            ppb->ison = 0;
        while (ppb->prevmember != NULL)
        {
            ppb = ppb->prevmember;
            if (ppb->group == pb->group)
                ppb->ison = 0;
        }
    }
    retByte = 1;
    return retByte;
}

Byte convSXAppendGroupPacket(MIDIPacket *pkt, struct btn_conversion_buffer *pb, Byte *loc)
/* append pkt with cc state of pb using loc. also updates loc with the new data location */
{
    Byte retByte = 0;
    /* update packet length */
    pkt->length = pkt->length + 3;
    pkt->data[*loc] = CC_MSG_BYTE + (defs->recvchannel - 1);
    /* set proper param */
    pkt->data[*loc + 1] = pb->cc_param_number;
    /* set proper value */
    if (pb->prevmember == NULL || pb->prevmember->group != pb->group)
	/* first member should be set at zero for prettyness */
        pkt->data[*loc + 2] = 0;
    else if (pb->nextmember == NULL || pb->prevmember->group != pb->group)
        /* last member should be 127 */
        pkt->data[*loc + 2] = 127;
    else
        /* otherwise put it in the middle of its value range */
        pkt->data[*loc + 2] = pb->cc_value_range_start + ((pb->cc_value_range_end - pb->cc_value_range_start) / 2);
    /* update loc */
    *loc = *loc + 3;
    return retByte;
}

Byte convSXAppendNormPacket(MIDIPacket *pkt, struct btn_conversion_buffer *pb, Byte *loc)
/* this one checks the members ison, because we are just flipping bits w/ this behavior */
{
    Byte retByte = 0;
    pkt->length = pkt->length + 3;
    pkt->data[*loc] = CC_MSG_BYTE + (defs->recvchannel - 1);
    /* set proper param */
    pkt->data[*loc + 1] = pb->cc_param_number;
    /* set proper value */
    if (pb->ison)
        pkt->data[*loc + 2] = 127;
    else
        pkt->data[*loc + 2] = 0;
    /* update loc */
    *loc = *loc + 3;
    return retByte;
}

Byte convSXAppendSpecPacket(MIDIPacket *pkt, struct btn_conversion_buffer *pb, struct btn_conversion_buffer *pob, Byte *loc)
/* make a packet -- pb = first group member, pob = second group member */
/* if pob != NULL, set both members */
{
    Byte retByte = 0;
    pkt->length = pkt->length + 3;
    pkt->data[*loc] = CC_MSG_BYTE + (defs->recvchannel - 1);
    /* set proper param */
    pkt->data[*loc + 1] = pb->cc_param_number;
    /* set proper value */
    if (pb->ison && pob->ison)
        pkt->data[*loc + 2] = 127;
    else if (!pb->ison && pob->ison)
        pkt->data[*loc + 2] = 80;
    else if (pb->ison && !pob->ison)
        pkt->data[*loc + 2] = 40;
    else
        pkt->data[*loc + 2] = 0;
    /* update loc */
    *loc = *loc + 3;
    return retByte;
}

Byte convGetCCValueByte(struct btn_conversion_buffer *pbuf, Byte sxValByte) {
    int retval = 0;
    if (pbuf->cc_value_range_end == 0 && pbuf->cc_value_range_start == 0)
        /* this is a nongroup, non special button */
    {
        /* this can only turn these buttons on for now. */
        retval = 127;
    }
    if (!pbuf->ingroup)
        /* special group button */
	{	/* need a way to see if the other special group member is on already */
        pbuf->ison = 1;
        if (pbuf->prevmember != NULL)
        {
            if (pbuf->prevmember->group == pbuf->group)
                /* pbuf is the first member */
            {
                if (convCmp_bitMask(pbuf->prevmember->onstate, sxValByte, pbuf->prevmember->index_start, pbuf->prevmember->index_end))
                    /* the prevmember is on */
                {
                    /* return pos 4 - both on */
                    retval = 127;
                }
                else
                {
                    /* return pos 3 pulse off saw on */
                    retval = 80;
                pbuf->prevmember->ison = 0;
                }
            }
        }

        if (pbuf->nextmember != NULL)
        {
            if (pbuf->nextmember->group == pbuf->group)
                /* pbuf is the second member */
            {
                if (convCmp_bitMask(pbuf->nextmember->onstate, sxValByte, pbuf->nextmember->index_start, pbuf->nextmember->index_end))
                    /* the member is on */
                {
                    /* should return position 4 -- both on */
                    retval = 127;
                    pbuf->nextmember->ison = 1;
                }
                else
                {
                    /* return position 2 - pulse on saw off */
                    retval = 40;
                    pbuf->nextmember->ison = 0;
                }
            }
        }
    }

    else
        /* should trigger on groups */
    {
        int lastvalbyteiszero = 0;
	int x;
        for (x = 0; x < btn_sx_rangeamt; x++)
        {
            if (btn_lastbyte[x].param == pbuf->sx_param_number)
            {
                if (btn_lastbyte[x].lastval == 0)
                    lastvalbyteiszero = 1;
            }
        }
        if (lastvalbyteiszero && pbuf->onstate == 0)
        {
            if (verbose)
                printf("(wrong button) ");
        }
        else
        {
            /* this should be the middle of the cc range for this group member */
            retval = pbuf->cc_value_range_start + ((pbuf->cc_value_range_end - pbuf->cc_value_range_start) / 2) - 1;
            /* if it's a top member, should make it 127, bottom member should be 0 probably */
            /* now set it as on in the buffer and it's group members off */
            pbuf->ison = 1;
            if (verbose)
                printf("ON ");
            if (pbuf->nextmember != NULL)
            {
                struct btn_conversion_buffer *ppbuf = pbuf->nextmember;
                ppbuf->ison = 0;
                while (ppbuf->nextmember != NULL)
                {
                    ppbuf = ppbuf->nextmember;
                    ppbuf->ison = 0;
                }
            }

            if (pbuf->prevmember != NULL)
            {
                struct btn_conversion_buffer *ppbuf = pbuf->prevmember;
                if (ppbuf->group == pbuf->group)
                    ppbuf->ison = 0;
                while (ppbuf->prevmember != NULL)
                {
                    if (ppbuf->group == pbuf->group)
                    {
                        ppbuf = ppbuf->prevmember;
                        if (ppbuf->group == pbuf->group)
                            ppbuf->ison = 0;
                    }
                }
            }
        }
    }
    return retval;
}

Byte convCC_btn_getstates(Byte pbyte)
/* this should use a static byte and check itself for changes from 0 */
{
    CC_btnState = 0;
    /* if (CC_btnState == 0)           //if all the buttons are 0 (this is the first time program is loaded probably) */
    /* this loop uses the button buffer, the second one will use bit operations */
    int i;
        for (i = 0; i < number_of_button_params; i++)
            if (btn_conv_buf[i].sx_param_number == pbyte)
                /* its a param match, now check onstates */
            {
                if (btn_conv_buf[i].ison)
                convSet_bitMask(btn_conv_buf[i].onstate, &CC_btnState, btn_conv_buf[i].index_start, btn_conv_buf[i].index_end);
            }
	/* can check for 0 if you want to check if this actually writes the bitmask */
    /*else
        //this loop should just check the CC_btnState using bit operations
    {
        for (int i = 0; i < sizeof(CC_btnState); i++)
            //i = index pos
        {
        }
    }*/
    return CC_btnState;
}

Byte convSX_btn_getstates(Byte parambyte, Byte valuebyte)
/* check this valuebyte -- need parambyte to get index numbers properly */
{
    Byte retByte = 0;
    int pindex = 0;             /* the index number for the lastbyte param number we want */
    int i;
    for (i = 0; i < btn_sx_rangeamt; i++)
        if (btn_lastbyte[i].param == parambyte)
            pindex = i;

    for (i = 0; i < 8; i++)	/* 8 = the number of bits in a byte */
    {
        if (GET_BIT(parambyte, i) != GET_BIT(btn_lastbyte[pindex].lastval, i))
            FLP_BIT(retByte, i);
    }
    return retByte;
    /* returns a byte with 1's where there are changes to make, 0's where there aren't */
}

/* if this gets called, we have already checked that the button isn't on already */
int conv_BtnOn(struct btn_conversion_buffer *pb, Byte *vbyte)
/* these two funx must return 1 if they change something, otherwise the conversion functions wont create and send a packet */
{				/* determine the behavior for this button and turn it on */
    /* set the proper bitmask in vbyte then set the buffer .ison's that must change according to the button behavior */
    int retval = 1;
    *vbyte = convCC_btn_getstates(pb->sx_param_number);	/* returns the current button state */
    if (pb->ingroup)
        /* this is a group button */
    {
        /* set the bitmask then set other group members as off in da buffer */
        convSet_bitMask(pb->onstate, vbyte, pb->index_start, pb->index_end);
        pb->ison = 1;
        /* check for group members in both directions */
        struct btn_conversion_buffer *pscan = pb;
        if (pb->prevmember != NULL)
            /* back */
        {
            pscan = pb->prevmember;
            if (pscan->group == pb->group)
                pscan->ison = 0;
            while (pscan->prevmember != NULL)
            {
                if (pscan->group == pb->group)
                {
                    pscan->ison = 0;
                    pscan = pscan->prevmember;
                    if (pscan->prevmember == NULL)
                        pscan->ison = 0;
                }
                else break;
            }
        }

        if (pb->nextmember != NULL)
            /* next */
        {
            pscan = pb->nextmember;
            while (pscan->nextmember != NULL)
            {
                if (pscan->group == pb->group)
                {
                    pscan->ison = 0;
                    pscan = pscan->nextmember;
                    if (pscan->nextmember == NULL && pscan->group == pb->group)
                        /* for the last group member */
                        pscan->ison = 0;
                }
            }
        }
    }

    else
	/* this should handle everything else. btnOff should turn it off if it should be off */
    {
        convSet_bitMask(pb->onstate, vbyte, pb->index_start, pb->index_end);
        pb->ison = 1;
    }
    if (verbose)
        printf("ON  ");
    return retval;
}

int conv_BtnOff(struct btn_conversion_buffer *pb, Byte *vbyte)
/* this should only ever have to turn off a single bit, never a range of index numbers (group) */
/* also it shouldn't ever have to check for any other buttons' states (so far :D) */
{
    int retval = 1;
    *vbyte = convCC_btn_getstates(pb->sx_param_number);
    pb->ison = 0;   		/* off */
    /* set the bitmask by flipping the onstate -- this works because there are never more than 1 index to change w/ this subroutine */
    convSet_bitMask(FLP_BIT(pb->onstate, 0), vbyte, pb->index_start, pb->index_end);
    FLP_BIT(pb->onstate, 0);	/* flip it back afterwards */
    if (verbose)
        printf("OFF ");
    return retval;
}

/* dumping state functions */
Byte convBtnCountSaved() {
    Byte retval = 0;
    int i;
    for (i = 0; i < number_of_button_params; i++)
        if (btn_conv_buf[i].last_cc_value != 231)
            retval++;
    return retval;
}

/* appends a packetlist with saved cc button messages */
MIDIPacket *convBtnDumpSaved(MIDIPacket *pkt, MIDIPacketList *pktList, int *count) {
    int packetcount = 1;	/* a multiplier for the interval */
    int i;
    for (i = 0 ; i < number_of_button_params; i++)
    {
        if (btn_conv_buf[i].last_cc_value != 231)
            /* if this buffer member has been used at all */
        {
            MIDIPacket pktToAdd;
            pktToAdd.length = 3;
            pktToAdd.data[0] = CC_MSG_BYTE + (defs->recvchannel - 1);
            pktToAdd.data[1] = btn_conv_buf[i].cc_param_number;
            pktToAdd.data[2] = btn_conv_buf[i].last_cc_value;
            pktToAdd.timeStamp = mach_absolute_time(); /* this needs to be set on an interval * count */
            /* to convert mach_absolute_time to nanos */
            mach_timebase_info_data_t info;
            mach_timebase_info(&info);
            /* convert to nanos */
            pktToAdd.timeStamp *= info.numer;
            pktToAdd.timeStamp /= info.denom;
            /* calculate the scheduling based on interval */
            pktToAdd.timeStamp = pktToAdd.timeStamp + ((5 NS_TO_MS) * packetcount)
                                                    + (number_of_fader_params
                                                       * (5 NS_TO_MS)
                                                       + (5 NS_TO_MS)
                                                       + 30 NS_TO_MS); /* replace w/ defs->dump_interval */
            /* this is correct calculation though */
            packetcount++;

            printf("Dumping %02X %02d %02d\n", pktToAdd.data[0], pktToAdd.data[1], pktToAdd.data[2]);
            pkt = MIDIPacketListAdd(pktList,
                                    sizeof(*pktList),
                                    pkt,
                                    pktToAdd.timeStamp,
                                    pktToAdd.length,
                                    &pktToAdd.data[0]);
            /* add it to the packetList */
            *count = *count++;
        }
    }
    return pkt;
}

/*  [INIT] these are functions to initialize the button conversion buffer */
void init_button_buffer() {
#ifdef TIMING
    mach_timebase_info_data_t info;
    mach_timebase_info(&info);
    uint64_t duration;
    uint64_t time = mach_absolute_time();
#endif

    conversionfile = fopen("conversion.txt", "r");
    if (conversionfile == NULL)
    {
        printf("Error: No conversion.txt file.\n");
        exit(1);
    }
    number_of_button_params = convGetNumberOfProperties(BTN_SECTION_NAME);
    if (verbose)
        printf("Allocating %lu bytes for button buffer ", (sizeof(*btn_conv_buf) * number_of_button_params));
    btn_conv_buf = malloc(sizeof(*btn_conv_buf) * number_of_button_params);
    if (verbose)
        printf("to load %d button parameters... \n", number_of_button_params);

    int i;
    for (i = 0; i < number_of_button_params; i++)
        /* fill in some of the buffer */
    {
        char *p = NULL;		/* pointer for loading strings into the buffer */
        Byte *pb = NULL;
        convGetNextProperty(BTN_SECTION_NAME, (void **)&p); /* pass the paramname to our pointer */
        if (!strncpy(&btn_conv_buf[i].paramname[0], p, sizeof(btn_conv_buf[i].paramname))) /* copy paramname to the buffer */
            {
                printf("error: strncpy sux. failed to copy parameter name %s\n", p);
                exit(1);
            }
        convGetNextProperty(BTN_SECTION_NAME, (void **)&pb); /* load cc param */
        btn_conv_buf[i].cc_param_number = *pb;
        convGetNextProperty(BTN_SECTION_NAME, (void **)&pb);
        btn_conv_buf[i].sx_param_number = *pb;
        convGetNextProperty(BTN_SECTION_NAME, (void **)&pb);
        btn_conv_buf[i].group = *pb;

        if (btn_conv_buf[i].group != 0)
            btn_conv_buf[i].ingroup = 1;
        else
            btn_conv_buf[i].ingroup = 0;

        convGetNextProperty(BTN_SECTION_NAME, (void **)&pb); /* load onstate bitmask */
        btn_conv_buf[i].onstate = *pb;
        convGetNextProperty(BTN_SECTION_NAME, (void **)&pb);
        btn_conv_buf[i].index_start = *pb; /* can't pass a bitfields address into GetNextProperty */
        if (btn_conv_buf[i].ingroup)
            btn_conv_buf[i].index_end = btn_conv_buf[i].index_start + lastbitmasklen;
        else
            btn_conv_buf[i].index_end = 0;
        btn_conv_buf[i].last_cc_value = 231; /* to check if state has changed later */
        /* display_bits(btn_conv_buf[x].onstate); */
    }

    fclose(conversionfile);
    /* create a sx param range int array that stores the different sx param numbers -- this decreases calculations for a juno 106 by up to 1/7 */
    create_sx_param_range();	/* this uses malloc */
    /* linking -- go through and link addresses between group members and nongroup members if they are special */
    link_btn_params();
    /* cc ranges - first get the number of buttons in each group, buttons not in a group, or buttons in a group with an adjacent member with the same cc parameter number */
    /* show_btn_groups(); */
    load_cc_ranges(btn_conv_buf);

#ifdef TIMING
    duration = mach_absolute_time() - time;
    /* convert duration to nanoseconds */
    duration *= info.numer;
    duration /= info.denom;
    printf("time to fill button buffer: %llu nanos, %f ms\n", duration, (duration / 1000000.00));
#endif
}

/* this function should first determine if this member of the buffer is in a group, not in a group,
 or not in a group but still sharing cc with another member.
 then it should load the proper cc ranges based on the group--
 0-64 65-127 if not in group
 127/4 for nongroup but cc matches - one iteration per permutation of the four groups being off/on
 127/x(the number of buttons in group) - one iteration per group being on (the rest in group are turned off) */
int load_cc_ranges(struct btn_conversion_buffer *bcnv)
/* subroutine for init_button_buffer to load the cc ranges properly using links nextmember and prevmember */
{
    int i;
    for (i = 0; i < number_of_button_params; i++)
    {
        if (bcnv[i].ingroup && bcnv[i].prevmember == NULL)
            /* if it's the first member in a group */
        {
            /* first get amount in group */
            int gamt = 1;
            struct btn_conversion_buffer *bp = bcnv[i].nextmember;
            for (; bp != NULL; bp = bp->nextmember)
                if (bp->group == bcnv[i].group)
                    gamt++;
            /* printf("%d group members in group %d\n", gamt, bcnv[i].group); */
            /* all this cc range setting stuff should probably use subroutines that return int values */
            /* but use floating point math to determine the ranges more precisely */
            bp = bcnv[i].nextmember;
            int count = 0;

	    /* load first group member */
            bcnv[i].cc_value_range_start = calc_cc_start(gamt, count);
            bcnv[i].cc_value_range_end = calc_cc_end(gamt, count);
            /* printf("loaded %s with CCS: %d, CCE: %d\n", bcnv[i].paramname, bcnv[i].cc_value_range_start, bcnv[i].cc_value_range_end); */
            count++;
            for (; bp != NULL; bp = bp->nextmember)
            {
                bp->cc_value_range_start = calc_cc_start(gamt, count);
                bp->cc_value_range_end = calc_cc_end(gamt, count);
                /* printf("loaded %s with CCS: %d, CCE: %d\n", bp->paramname, bp->cc_value_range_start, bp->cc_value_range_end); */
                count++;
            }
        }

        else if (!bcnv[i].ingroup && btn_conv_buf[i].prevmember == NULL && btn_conv_buf[i].nextmember != NULL)
            /* special btn non group first member */
        {
	    /* this is a total hack right now, implement this for more than two buttons in a special group later */
            bcnv[i].cc_value_range_start = 32;
            bcnv[i].cc_value_range_end = 96;
            bcnv[i].nextmember->cc_value_range_start = 64;
        }

        else
        {
            /* not in group */
            if (bcnv[i].cc_value_range_start == 0 && bcnv[i].cc_value_range_end == 0)
                /* if the cc range hasn't been filled, this is a non-group, non-special member */
            {
                /* bcnv[i].cc_value_range_start = 64; */
                /* bcnv[i].cc_value_range_end = 127; */
            }
        }
    }
    /* printf("%s\n", bcnv[10].paramname); */
    return 1;
}

void create_sx_param_range()
/* this creates a struct to hold lastvalbytes for the sx host to figure out what button state has changed */
{
    int lastparam = 0, count = 0;
    int i;
    for (i = 0; i < number_of_button_params; i++)
        /* cycle through and count unique sx param numbers */
        if (btn_conv_buf[i].sx_param_number != lastparam)
        {
            lastparam = btn_conv_buf[i].sx_param_number;
            count++;
        }
    /* malloc the lastbyte struct and fill it */
    btn_lastbyte = malloc(sizeof(*btn_lastbyte) * count);
    if (verbose)
        printf("Allocated %lu bytes for %d unique sysex buttons parameters...\n", (sizeof(*btn_lastbyte) * count), count);
    lastparam = 0;      	/* in case there's only one param number */

    for (i = 0; i < number_of_button_params; i++)
        if (btn_conv_buf[i].sx_param_number != lastparam)
        {
            lastparam = btn_conv_buf[i].sx_param_number;
            btn_lastbyte[btn_sx_rangeamt].param = btn_conv_buf[i].sx_param_number;
            btn_lastbyte[btn_sx_rangeamt].lastval = 0;
            btn_sx_rangeamt++;
        }
    if (verbose)
    {
        printf("Button param range numbers:\n");
	int i;
        for (i = 0; i < btn_sx_rangeamt; i++)
            printf("%d:0x%02X\n", i, btn_lastbyte[i].param);
    }
}

/* fills in the button buffer next and prevmember pointers. */
void link_btn_params() {
    int i;
    for (i = 0; i < number_of_button_params; i++)
    {
        if (btn_conv_buf[i].ingroup)
        {
            /* this should have subroutines that don't require things to be next to each other in the buffer to be linked */
            if (btn_conv_buf[i].group == btn_conv_buf[i - 1].group)
                /* if prev group is the same link */
                btn_conv_buf[i].prevmember = &btn_conv_buf[i - 1];
            else
                btn_conv_buf[i].prevmember = NULL;
            if (i != number_of_button_params - 1) /* to protect memory */
                if (btn_conv_buf[i].group == btn_conv_buf[i + 1].group)
                    btn_conv_buf[i].nextmember = &btn_conv_buf[i + 1];
            else
                btn_conv_buf[i].nextmember = NULL;
        }

        else
            /* same treatment for special groups */
        {
            if (btn_conv_buf[i].cc_param_number == btn_conv_buf[i - 1].cc_param_number)
                btn_conv_buf[i].prevmember = &btn_conv_buf[i - 1];
            else
                btn_conv_buf[i].prevmember = NULL;
            if (i != number_of_button_params - 1) /* to protect memory */
                if (btn_conv_buf[i].cc_param_number == btn_conv_buf[i + 1].cc_param_number)
                    btn_conv_buf[i].nextmember = &btn_conv_buf[i + 1];
            else
                btn_conv_buf[i].nextmember = NULL;
        }
    }
}

void display_btns() {
    int i;
    for (i = 0; i < number_of_button_params; i++)
    {
        printf("[%d]%s CC- %d SX- %d G- %d IG- %d OS- %d IS- %d IE- %d CCS- %d CCE- %d ", i, btn_conv_buf[i].paramname, btn_conv_buf[i].cc_param_number, btn_conv_buf[i].sx_param_number, btn_conv_buf[i].group, btn_conv_buf[i].ingroup, btn_conv_buf[i].onstate, btn_conv_buf[i].index_start, btn_conv_buf[i].index_end, btn_conv_buf[i].cc_value_range_start, btn_conv_buf[i].cc_value_range_end);
        if (btn_conv_buf[i].prevmember != NULL)
            /* check that pointer addresses match */
        {
            if (btn_conv_buf[i].prevmember == &btn_conv_buf[i - 1])
                printf("PM- $$ ");
            else
                printf("PM- XX ");
        }
        if (btn_conv_buf[i].nextmember != NULL)
        {
            if (btn_conv_buf[i].nextmember == &btn_conv_buf[i + 1])
                printf("NM- $$");
            else
                printf("NM- XX");
        }
        printf("\n");
    }
}

/* later these two following sub routines should be updated to do more precise calcs using floats
    also these subroutines are only for group buttons right now */
int calc_cc_start(int gamt, int gmemb) {
    float fret = ((127 / gamt) * gmemb) + 1;
    if (gmemb == 0)
        fret = fret - 1;
    /* printf("CCS: %f\n", fret); */
    return (int)fret;
}

int calc_cc_end(int gamt, int gmemb) {
    /* if it's the last group member return 127 */
    if (gmemb == gamt - 1)
        return 127;
    float fret = (127 / gamt) * (gmemb + 1);
    /* printf("CCE: %f\n", fret); */
    return (int)fret;
}

void show_btn_groups() {
    /* searches through the buffer and displays each grouped button organized by member */
    int i;
    for (i = 0; i < number_of_button_params; i++)
    {
        if (btn_conv_buf[i].ingroup && btn_conv_buf[i].prevmember == NULL)
            /* this button is the first member in a group */
        {
            int count = 0;
	    int x;
            for (x = i; btn_conv_buf[x].nextmember != NULL || (btn_conv_buf[x].prevmember != NULL && btn_conv_buf[x].nextmember == NULL); x++)
                /* follow nextmembers till we run out of them */
                /* this still assumes they are next to each other in the buffer and it probably shouldn't have to */
                if (btn_conv_buf[x].group == btn_conv_buf[i].group)
                    count++;
            printf("%d members in group %d\n%s\n", count, btn_conv_buf[i].group, btn_conv_buf[i].paramname);

	    struct btn_conversion_buffer *bp;
            for (bp = btn_conv_buf[i].nextmember;
                 bp != NULL;
                 bp = bp->nextmember)
            {
                if (bp->group == btn_conv_buf[i].group)
                    printf("%s\n", bp->paramname);
            }
        }

        if (!btn_conv_buf[i].ingroup && btn_conv_buf[i].prevmember == NULL && btn_conv_buf[i].nextmember != NULL)
            /* this is the first member of a special button (non)group */
        {
            int count = 0;
	    int x;
            for (x = i; btn_conv_buf[x].nextmember != NULL && (!btn_conv_buf[x].ingroup || (btn_conv_buf[x].prevmember != NULL && btn_conv_buf[x].nextmember == NULL)); x++)
                /* follow nextmembers till we run out of them */
                /* this still assumes they are next to each other in the buffer and it probably shouldn't have to */
                    count++;
            printf("%d members in special group\n%s\n", count, btn_conv_buf[i].paramname);
            struct btn_conversion_buffer *bp = btn_conv_buf[i].nextmember;
            while (bp != NULL)
            {
                printf("%s\n", bp->paramname);
                bp = bp->nextmember;
            }
        }
    }
}

void display_button_states() {
    int i;
    for (i = 0; i < number_of_button_params; i++)
	{
		printf("%s | ", btn_conv_buf[i].paramname);
		if (btn_conv_buf[i].ison)
			printf("ON\n");
		else printf("OFF\n");
	}
}
