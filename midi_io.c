
#include <stdio.h>
#include <stddef.h>
#include <unistd.h>
#include <string.h>
#include <CoreMIDI/CoreMIDI.h>
#include <CoreFoundation/CoreFoundation.h>
#include <mach/mach_time.h>
#include <time.h>
#include <pthread.h>

#include "main.h"
#include "settings.h"

extern int verbose;
extern struct defaults_struct *defs;

extern int GetDefaultMIDIInterfaces(MIDIUniqueID *HostID, MIDIUniqueID *SynthID);
extern int SetDefaultMIDIInterfaces(MIDIUniqueID HostID, MIDIUniqueID SynthID);
extern void errCheck(OSStatus err);

extern FILE defaultsfile;

extern Byte *sx_fstr;
extern Byte cc_fstr[3];

extern int convCC_SX_fader(Byte parambyte, Byte valuebyte, MIDIPacket *pktToSend);
extern int convSX_CC_fader(Byte parambyte, Byte valuebyte, MIDIPacket *pktToSend);

extern int convCC_SX_btn(Byte parambyte, Byte valuebyte, MIDIPacket *pktToSend);
extern int convSX_CC_btn(Byte parambyte, Byte valuebyte, MIDIPacket *pktToSend);
extern void display_button_states();

/* external state dumping functions */

extern Byte convFaderCountSaved();
extern Byte convBtnCountSaved();
extern MIDIPacket *convFaderDumpSaved(MIDIPacket *pkt, MIDIPacketList *pktList, int *count);
extern MIDIPacket *convBtnDumpSaved(MIDIPacket *pkt, MIDIPacketList *pktList, int *count);


/* local functions */
void listen();
void midi_init(int choose);
MIDIUniqueID choose_midi_device();
void send_testnote();
void get_dests();
void convDumpState();

void convReadProcCCHost(const MIDIPacketList *packetList, void *rpRefCon, void *rpHostSrcRefCon);
MIDIPacket *convCCKill(const MIDIPacketList *pktList, MIDIPacketList *newList);
int throttle_cc_faders(MIDIPacket *pkt, MIDIPacket *lastpkt);

void convReadProcSXHost(const MIDIPacketList *packetList, void *rpRefCon, void *rpSynthSrcRefCon);
MIDIPacket *convSXKill(const MIDIPacketList *pktList, MIDIPacketList *newList);


/* local vars */
MIDIUniqueID HostID, SynthID;
MIDIEndpointRef epHost, epSynth, epHostDest, epSynthDest;
MIDIClientRef client;
MIDIPortRef inPort = 0, inPort2 = 0, outPort = 0;
MIDIObjectType oType;
MIDIReadProc rpHost, rpSynth;

void *rpHostConRef, *rpHostSrcConRef, *rpSynthConRef, *rpSynthSrcConRef;

/* for outPort thread sync */
pthread_mutex_t mtx;

MIDIPacket lastCCpkt;


MIDIPacketList pktListToSend;
OSStatus err;


void
listen()
{
    printf("Listening for CC/SYSEX data... \n[s] - send state to host | [t] - test note | [q] quit ");
    if (verbose)
	printf("| [b] show button states ");
    printf("\n");

    while (true) {
	int c = getchar();

	if (c == 'q' || c == 'Q') {
	    break;
	}

	if (c == 's' || c == 'S') {
	    /* convDumpState(); */
	}

	if (c == 't' || c == 'T') {
	    send_testnote();
	}

	if (c == 'b' || c == 'B') {
	    /* if (verbose) */
	    /* display_button_states(); */
	}
    }
}

void
convReadProcCCHost(const MIDIPacketList *ccrpacketList, void *rpRefCon, void *rpHostSrcRefCon)
{
    /* MIDIPacket *ccpkt = (MIDIPacket *)ccrpacketList->packet; */
    MIDIPacketList clnccPacketList;
    /* first filter out any messages coming from the other readproc (ones going toward recvchannel) */
    /*  (this might need to be mutexed because it uses one of the parameters of the threads proc..?) */
    MIDIPacket *ccpkt = convCCKill(ccrpacketList, &clnccPacketList); /* filter out converted packets into clnccPacketList */
    /* ccpkt now points to the first packet of the cleaned packetlist */
    if (ccpkt == NULL)
        /* return if there's only packets to convert in this list */
        return;
    MIDIPacket ccpktToSend;
    MIDIPacketList ccpktListToSend;
    MIDIPacket *ccspkt = MIDIPacketListInit(&ccpktListToSend);
    for (unsigned int i = 0; i < (clnccPacketList.numPackets + 1); i++) {
        if (ccpkt->data[0] == (CC_MSG_BYTE + (defs->sendchannel - 1))) {/* CC message--checks if the packet is for sendchannel */
            /* recvchannel messages should just get passed through to the host... */

            /* throttle cc packets (don't add it to the packetlist if the last packet had the same value) */
            if (ccpkt->data[2] == lastCCpkt.data[defs->sysex_value_pos]) {
                /* this just dumps repetitive packets, it does not replace variable-based throttling */
                goto dropthisccpacket;
            }

            /* check for fader/button param -- mutexs are in the functions */
            if (!convCC_SX_fader(ccpkt->data[1], ccpkt->data[2], &ccpktToSend)) {
                if (!convCC_SX_btn(ccpkt->data[1], ccpkt->data[2], &ccpktToSend)) {
                    /* if neither one of these has to happen return */
                    /* printf("\n"); */
                    /* return; */
                    goto dropthisccpacket;
                }
                /*else
		  {
		  lastCCpkt = ccpktToSend;
		  }*/
            } else {
                lastCCpkt = ccpktToSend;
                /*for (int i = 0; i < ccpktToSend.length; i++)
		  {
		  lastCCpkt.data[i] = ccpktToSend.data[i];
		  }
                */
            }

        } else {    		/* just send it along */
            ccpktToSend = *ccpkt;
        }
        ccspkt = MIDIPacketListAdd(&ccpktListToSend, sizeof(ccpktListToSend), ccspkt, ccpktToSend.timeStamp, ccpktToSend.length, &ccpktToSend.data[0]);
    dropthisccpacket:
        ccpkt = MIDIPacketNext(ccpkt);
    }
    /* make sure outPort is thread-synced */
    pthread_mutex_lock(&mtx);
    err = MIDISend(outPort, epSynthDest, &ccpktListToSend);
    pthread_mutex_unlock(&mtx);
    errCheck(err);
}

/* takes an uninitialized packetlist and fills it with usable
   conversion strings (strings meant for sendchannel, not recv) returns
   0 if there is nothing but already converted packets in the list */
MIDIPacket*
convCCKill(const MIDIPacketList *pktList, MIDIPacketList *newList)
{
    int count = 0;      	/* number of removed packets */
    for (unsigned int i = 0; i < pktList->numPackets; i++)
        if (pktList->packet[i].data[0] == CC_MSG_BYTE + (defs->recvchannel - 1))
            count++;
    int retval = pktList->numPackets - count; /* number of packets in the list - number of removed packets */
    if (retval != 0) {    	/* if there is a new packetlist to create */
        /* setup the new packet list */

        MIDIPacket *ppkt = MIDIPacketListInit(newList);
        if (ppkt == NULL) {
            printf("convCCKill could not initialize a packetlist.\n");
            exit(1);
        }

        for (unsigned int i = 0; i < pktList->numPackets; i++) {
            if (pktList->packet[i].data[0] != CC_MSG_BYTE + (defs->recvchannel - 1)) {
                /* if this is a cc packet but is from the conversion proc (sending to recv channel), don't add it */

                if (pktList->packet[i].length != 0 /*&& pktList->packet[i].length <= 3*/) {
                    ppkt->length = pktList->packet[i].length;
                    ppkt->timeStamp = pktList->packet[i].timeStamp;
                    for (int ii = 0; ii < pktList->packet[i].length; ii++) {
                        /* fill the packet data... */

                        ppkt->data[ii] = pktList->packet[i].data[ii];
                    }
                    ppkt = MIDIPacketListAdd(newList,
                                             sizeof(newList),
                                             ppkt,
                                             pktList->packet[i].timeStamp,
                                             pktList->packet[i].length,
                                             &pktList->packet[i].data[0]);
                }
                else break;
            } else {
                /* set this packet up and add it to the list */
            }
        }
        return &newList->packet[0];
    }
    else
        return NULL;
}

int
throttle_cc_faders(MIDIPacket *pkt, MIDIPacket *lastpkt)
{
    int retval = 1;
    return retval;
}

void
convReadProcSXHost(const MIDIPacketList *sxrpacketList, void *rpSXRefCon, void *rpSynthSrcRefCon)
{
    /* MIDIPacket *sxpkt = (MIDIPacket *)sxrpacketList->packet; */
    /* no throttling on this end... the juno takes care of that :D */
    /* filter out messages from the converter (that are on sendchannel) */
    MIDIPacketList clnsxPacketList;
    /* might have to mutex this... */
    MIDIPacket *sxpkt = convSXKill(sxrpacketList, &clnsxPacketList); /* filter out converted packets into clnsxPacketList */
    /* sxpkt now points to the first packet of the cleaned packetlist */
    if (sxpkt == NULL)
        /* return if there's only converted packets in this list */
        return;
    MIDIPacketList sxpktListToSend;
    MIDIPacket sxpktToSend;
    MIDIPacket *sxsppkt = MIDIPacketListInit(&sxpktListToSend);
    for (unsigned int i = 0; i < (clnsxPacketList.numPackets + 1); i++) {
        if (sxpkt->data[0] == SX_OPEN_BYTE
            && sxpkt->data[defs->sysex_channel_pos] == (defs->recvchannel - 1)) {
            /* if it's sysex & on the recvchannel (5), this is a conversion message */

            if (!convSX_CC_fader(sxpkt->data[defs->sysex_param_pos], sxpkt->data[defs->sysex_value_pos], &sxpktToSend)) {
                /* if it's not a fader param check buttons */

                if (!convSX_CC_btn(sxpkt->data[defs->sysex_param_pos], sxpkt->data[defs->sysex_value_pos], &sxpktToSend)) {
                    /* there's nothing to convert... probably shouldn't even send it along if it got this far and doesn't get used */
                    goto dropthissxpacket;
                }
            }
        } else if (sxpkt->data[0] == SX_OPEN_BYTE && sxpkt->data[defs->sysex_channel_pos] == (defs->sendchannel - 1)) {
            if (verbose)
                printf("A converted packet got into the SYSEX conversion proc somehow\n");
            goto dropthissxpacket;
        }
        sxsppkt = MIDIPacketListAdd(&sxpktListToSend, sizeof(sxpktListToSend), sxsppkt, sxpktToSend.timeStamp, sxpktToSend.length, &sxpktToSend.data[0]);
    dropthissxpacket:
        sxpkt = MIDIPacketNext(sxpkt);
    }
    pthread_mutex_lock(&mtx);
    err = MIDISend(outPort, epHostDest, &sxpktListToSend);
    pthread_mutex_unlock(&mtx);
    errCheck(err);
}

/* filters already converted messages out of the packetlist, returning 0 if it killed the whole packetlist, 1 if it didn't */
/* if it doesn't kill the whole packetlist, it will replace it. */
MIDIPacket*
convSXKill(const MIDIPacketList *pktList, MIDIPacketList *newList)
{
    int count = 0;      	/* number of removed packets */
    for (unsigned int i = 0; i < pktList->numPackets; i++)
        if (pktList->packet[i].data[0] == SX_OPEN_BYTE
            && pktList->packet[i].data[defs->sysex_channel_pos] == (defs->sendchannel - 1))
            count++;
    int retval = pktList->numPackets - count; /* number of packets in the list - number of removed packets */
    if (retval != 0) {    	/* if there is a new packetlist to create */
        /* setup the new packet list */

        MIDIPacket *ppkt = MIDIPacketListInit(newList);
        for (unsigned int i = 0; i < pktList->numPackets; i++) {
            /* if either it's not a sysex message at all or it is one not meant to be converted again */
            if (pktList->packet[i].data[0] != SX_OPEN_BYTE) {
                if (pktList->packet[i].length != 0) {		/* fill the packet and add it */
                    ppkt->length = pktList->packet[i].length;
                    ppkt->timeStamp = pktList->packet[i].timeStamp;

                    for (int ii = 0; ii < pktList->packet[i].length; i++) {
                        /* fill the packet data... */
                        ppkt->data[ii] = pktList->packet[i].data[ii];
                    }
                    ppkt = MIDIPacketListAdd(newList,
                                             sizeof(newList),
                                             ppkt,
                                             pktList->packet[i].timeStamp,
                                             pktList->packet[i].length,
                                             &pktList->packet[i].data[0]);
                }
                else break;
            }
            if (pktList->packet[i].data[0] == SX_OPEN_BYTE
                && pktList->packet[i].data[defs->sysex_channel_pos] != (defs->sendchannel - 1)) {
                if (pktList->packet[i].length != 0) {
                    ppkt->length = pktList->packet[i].length;
                    ppkt->timeStamp = pktList->packet[i].timeStamp;
                    for (int ii = 0; ii < pktList->packet[i].length; ii++)
                        /* fill the packet data... */
			{
			    ppkt->data[ii] = pktList->packet[i].data[ii];
			}
                    /* increment newlists numPackets? */
                    ppkt = MIDIPacketListAdd(newList,
                                             sizeof(newList),
                                             ppkt,
                                             pktList->packet[i].timeStamp,
                                             pktList->packet[i].length,
                                             pktList->packet[i].data);
                }
            }
        }
        return &newList->packet[0];
    }
    else
        return NULL;
}

void
convDumpState()
{
    /* first count how many packets we're putting in this packetlist */
    pthread_mutex_lock(&mtx);
    Byte packetcount = convFaderCountSaved() + convBtnCountSaved();
    if (!packetcount) {
        /* for some reason this isn't triggered */
        printf("no state saved yet\n");
        return;
    }
    /* now create a packetlist with the correct size */
    MIDIPacketList pktList;
    /* pktList.numPackets = packetcount; */
    /* unsigned long long timecount = mach_absolute_time() + 200000;            //how long is this? should be like 6ms ahead of now */
    int count = 0;

    /* init the packetlist */
    MIDIPacket *pkt = MIDIPacketListInit(&pktList);
    if (pkt == NULL) {
        printf("Error: couldn't initialize packetlist for state dump to host\n");
        pthread_mutex_unlock(&mtx);
        return;
    }

    /* start filling packets */
    pkt = convFaderDumpSaved(pkt, &pktList, &count);
    if (pkt == NULL) {
        printf("Error: no room in packetlist for state dump to host (faders)\n");
        pthread_mutex_unlock(&mtx);
        return;
    }
    /* create another packetlist for buttons, maybe it wont crash */
    MIDIPacketList BtnpktList;
    pkt = MIDIPacketListInit(&BtnpktList);
    pkt = convBtnDumpSaved(pkt, &BtnpktList, &count);
    if (pkt == NULL) {
        printf("Error: no room in packetlist for state dump to host (buttons)\n");
        pthread_mutex_unlock(&mtx);
        return;
    }

    /* schedule the packetlist in increments */
    /* send out the packetlist */
    err = MIDISend(outPort, epHostDest, &pktList);
    err = MIDISend(outPort, epHostDest, &BtnpktList);
    pthread_mutex_unlock(&mtx);
    errCheck(err);
    /* have it say it's done when the packets have been sent */
    fflush(stdout);
    struct timespec t0, t0ret;
    t0.tv_sec = 0;
    t0.tv_nsec = 6 NS_TO_MS;	/* this is how long the first packet should have been delayed */
    nanosleep(&t0, &t0ret);
    printf("Dumping %d parameters to CC host", count);
    for (int i = 0; i < packetcount; i++) {
        struct timespec t1, tret;
        t1.tv_sec = 0;
        t1.tv_nsec = 10 NS_TO_MS; /* 10 milliseconds */
        nanosleep(&t1, &tret);
        printf(".");
        fflush(stdout);
    }
    printf("done.\n");
}

void
midi_init(int choose)
{
    if (!choose) {
        if (!GetDefaultMIDIInterfaces(&HostID, &SynthID)) {
            midi_init(MIDI_INIT_NODEFAULTS);
            return;
        }

        if (MIDIObjectFindByUniqueID(HostID, (MIDIObjectRef *)&epHost, &oType) == kMIDIObjectNotFound
            || MIDIObjectFindByUniqueID(SynthID, (MIDIObjectRef *)&epSynth, &oType) == kMIDIObjectNotFound) {
            printf("Default device(s) not found...\n");
            midi_init(MIDI_INIT_NODEFAULTS);
            return;
        }
        /* load interfaces from defaults.txt */

    } else {
        /* load interfaces from menu */
        printf("\nChoose the MIDI CC Source:\n");
        HostID = choose_midi_device();
        printf("\nChoose the Synth/SYSEX Source:\n");
        SynthID = choose_midi_device();
        SetDefaultMIDIInterfaces(HostID, SynthID);
    }

    /* set HostID and SynthID */
    err = MIDIObjectFindByUniqueID(HostID, (MIDIObjectRef *)&epHost, NULL);
    errCheck(err);
    if (verbose) {
        CFStringRef HostName;
        char cHostName[64];
        err = MIDIObjectGetStringProperty(epHost, kMIDIPropertyName, &HostName);
        errCheck(err);
        CFStringGetCString(HostName, &cHostName[0], sizeof(cHostName), 0);
        printf("Creating CC host on %s.\n", cHostName);
        CFRelease(HostName);
    }

    err = MIDIObjectFindByUniqueID(SynthID, (MIDIObjectRef *)&epSynth, NULL);
    errCheck(err);
    if (verbose) {
        CFStringRef SynthName;
        char cSynthName[64];
        err = MIDIObjectGetStringProperty(epSynth, kMIDIPropertyName, &SynthName);
        errCheck(err);
        CFStringGetCString(SynthName, &cSynthName[0], sizeof(cSynthName), 0);
        printf("Creating SYSEX host on %s.\n", cSynthName);
        CFRelease(SynthName);
    }

    /* set up destination endpoints... */
    get_dests();
    /* create client, output port */
    err = MIDIClientCreate(CFSTR("Junoboss Client"), NULL, NULL, &client);
    errCheck(err);
    err = MIDIOutputPortCreate(client, CFSTR("Junoboss Output"), &outPort);
    errCheck(err);
    /* err = MIDIOutputPortCreate(client, CFSTR("Junoboss Output2"), &outPort); */
    /* errCheck(err); */
    /* create/connect input ports */
    /* first CC host */
    /* err = MIDIDestinationCreate(client, CFSTR("Junoboss CC"), rpHost, rpHostConRef, (MIDIEndpointRef *)&inPort); */
    err = MIDIInputPortCreate(client, CFSTR("Junoboss CC port"), convReadProcCCHost, NULL, &inPort);
    errCheck(err);
    err = MIDIInputPortCreate(client, CFSTR("Junoboss SYSEX port"), convReadProcSXHost, &convReadProcSXHost, &inPort2);
    errCheck(err);
    err = MIDIPortConnectSource(inPort, epHost, NULL);
    errCheck(err);
    err = MIDIPortConnectSource(inPort2, epSynth, NULL);
    errCheck(err);

    /* initialize mutex */
    int err = pthread_mutex_init(&mtx, NULL);
    if (err != 0)
        /* error */
	{
	    printf("Error: pthreads failed to initialize a mutex\n");
	    exit(1);
	}
}

void
get_dests()
{
    unsigned int numdests = (int)MIDIGetNumberOfDestinations();
    CFStringRef dname, sname;
    char cdname[64], csname[64];
    err = MIDIObjectGetStringProperty(epSynth, kMIDIPropertyName, &sname);
    errCheck(err);
    CFStringGetCString(sname, &csname[0], sizeof(csname), 0);
    for (unsigned int i = 0; i < numdests; i++) {
        MIDIEndpointRef ep = MIDIGetDestination(i);
        err = MIDIObjectGetStringProperty(ep, kMIDIPropertyName, &dname);
        errCheck(err);
        CFStringGetCString(dname, &cdname[0], sizeof(cdname), 0);
        if (!strncmp(&cdname[0], &csname[0], strlen(cdname))) {
            /* printf("found matching dest, %s\n", cdname); */
            epSynthDest = ep;
            break;
        }
    }
    err = MIDIObjectGetStringProperty(epHost, kMIDIPropertyName, &sname);
    errCheck(err);
    CFStringGetCString(sname, &csname[0], sizeof(csname), 0);
    for (i = 0; i < numdests; i++) {
        MIDIEndpointRef ep = MIDIGetDestination(i);
        err = MIDIObjectGetStringProperty(ep, kMIDIPropertyName, &dname);
        errCheck(err);
        CFStringGetCString(dname, &cdname[0], sizeof(cdname), 0);
        if (!strncmp(&cdname[0], &csname[0], strlen(cdname))) {
            /* printf("found matching dest, %s\n", cdname); */
            epHostDest = ep;
            CFRelease(dname);
            break;
        }
    }
    CFRelease(sname);
}

void
send_testnote()
{
    MIDIPacket* testnoteon = &pktListToSend.packet[0];
    pktListToSend.numPackets = 1;
    testnoteon->timeStamp = 0; /* send now */
    testnoteon->length = 3;
    /* testnoteon->data[3] = {0x90, 60, 64};			noteon, c, 64v -- fails */
    testnoteon->data[0] = 0x90 + (defs->sendchannel - 1); /* this is a note on message */
    testnoteon->data[1] = 60; /* C */
    testnoteon->data[2] = 64; /* half velocity */

    printf("Sending test note...\n");
    MIDISend(outPort, epSynthDest, &pktListToSend);
    MIDIPacket* testnoteoff = &pktListToSend.packet[0]; /* = MIDIPacketNext(testnoteoff); */
    testnoteoff->timeStamp = mach_absolute_time() + 2000000000;
    testnoteoff->length = 3;
    testnoteoff->data[0] = 0x80 + (defs->sendchannel - 1); /* note off */
    testnoteoff->data[1] = 60;
    testnoteoff->data[2] = 64;
    MIDISend(outPort, epSynthDest, &pktListToSend);
    sleep(2);
}

MIDIUniqueID
choose_midi_device()
{
    unsigned long devamt = MIDIGetNumberOfDestinations();
    MIDIUniqueID retID = 0;
    printf("---------------------------------------------------------------\n");
    printf("Choose MIDI Interface (0-%lu):\n", devamt - 1);
    printf("---------------------------------------------------------------\n");
    for (unsigned long i = 0; i < devamt; i++) {
        CFStringRef pname, pmanuf, pmodel;
        char name[64], manuf[64], model[64];
        MIDIEndpointRef ep = MIDIGetDestination(i);
        /* MIDIEndpointRef eps = MIDIGetSource(i); */
        MIDIEntityRef ent;
        MIDIDeviceRef dev;
        MIDIEndpointGetEntity(ep, &ent);
        MIDIEntityGetDevice(ent, &dev);
        err = MIDIObjectGetStringProperty(ep, kMIDIPropertyName, &pname);
        errCheck(err);
        err = MIDIObjectGetStringProperty(ep, kMIDIPropertyManufacturer, &pmanuf);
        errCheck(err);
        err =MIDIObjectGetStringProperty(dev, kMIDIPropertyName, &pmodel);
        errCheck(err);
        CFStringGetCString(pname, name, sizeof(name), 0);
        CFStringGetCString(pmanuf, manuf, sizeof(manuf), 0);
        CFStringGetCString(pmodel, model, sizeof(model), 0);
        CFRelease(pname);
        CFRelease(pmanuf);
        CFRelease(pmodel);
        printf("%lu) %s - %s - %s\n", i, name, manuf, model);
    }

    printf("Interface (0-%lu): ", devamt - 1);
    char c = 0;
    while (1) {
        c = getchar();
        unsigned long num = 0;
        if (c != '\0' || c != '\n') {
            num = strtol(&c, NULL, 10);
            if (num >= devamt) {
                printf("Incorrect interface number, silly :D\n");
                exit(0);
            } else {
                MIDIEndpointRef dev = MIDIGetSource(num);
                /* load retID */
                MIDIObjectGetIntegerProperty(dev, kMIDIPropertyUniqueID, &retID);
                getchar();
                goto retUniqueID;
            }
        }
    }
 retUniqueID:
    return retID;
}
