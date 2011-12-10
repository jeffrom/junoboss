/*  file_io.c
    junoboss

   Created by Jeffrey Martin on 3/14/11.
   Copyright 2011 __MyCompanyName__. All rights reserved.

   this file has string parsing methods for getting the information out of the config files using
   the right syntax
*/

#include <stdio.h>
#include <stddef.h>
#include <string.h>
#include <limits.h>

#include <CoreMIDI/CoreMIDI.h>
#include <CoreFoundation/CoreFoundation.h>

#include "main.h"


FILE *conversionfile;
FILE *defaultsfile;

extern Byte binstringtobyte(char *x);

char *token;
unsigned int tokencount = 0;
char line[1024];

/* global vars declared here */
size_t lastbitmasklen;

/* functions in this file */
int convGetNumberOfProperties(const char *section);
int convGetNextProperty(const char *section, void **paddy);
int GetDefaultMIDIInterfaces(MIDIUniqueID *HostID, MIDIUniqueID *SynthID);
int SetDefaultMIDIInterfaces(MIDIUniqueID HostID, MIDIUniqueID SynthID);
char *colon_blow(char *token, const char delims[MAX_DELIMS], size_t size);
int convGetDefaultSetting(const char *section, const char *setting, void **paddy);

int GetDefaultMIDIInterfaces(MIDIUniqueID *HostID, MIDIUniqueID *SynthID)
/* read default interfaces from file */
{
    defaultsfile = fopen("defaults.txt", "r");
    if (defaultsfile == NULL)
        return 0;
    fgets(&line[0], sizeof(line), defaultsfile);
    *HostID = (int)strtol(&line[0], NULL, 10);
    fgets(&line[0], sizeof(line), defaultsfile);
    *SynthID = (int)strtol(&line[0], NULL, 10);
    fclose(defaultsfile);
    return 1;
}

int SetDefaultMIDIInterfaces(MIDIUniqueID HostID, MIDIUniqueID SynthID)
/* write default interfaces to file */
{
    defaultsfile = fopen("defaults.txt", "w"); /* this will rewrite the file */
    fprintf(defaultsfile, "%d\n%d\n", (int)HostID, (int)SynthID);
    fclose(defaultsfile);
    return 1;
}

int convGetDefaultSetting(const char *section, const char *setting, void **paddy)
/* search for a default instrument setting by section/setting name */
/* returns pointer to int or char array depending on the setting */
{
    static int d_retInt;
    int retType = CONV_NEXTPROP_ERR;
    while (!feof(conversionfile))
        /* find section name then setting name */
    {
        if (!strncmp(&line[0], section, strlen(section)))
            {
                /* now find setting name */
                fgets(&line[0], sizeof(line), conversionfile);
                fgets(&line[0], sizeof(line), conversionfile);
                while (line[0] != '[')
                {
                    if (line[0] != '\n' && line[0] != '\0')
                    {
                        token = strtok(&line[0], DEFAULTS_DELIMS);
                        if (!strncmp(token, setting, strlen(setting)))
                        {
                            token = strtok(NULL, DEFAULTS_DELIMS);
                            /* check if it's a string or int */
                            if (token[0] == '\"')
                            {
                                retType = CONV_NEXTPROP_STR;
                                token = colon_blow(token, "\"\n", strlen(token));
                                *paddy = (char *)token;
                                goto retdefault;
                            }

                            else /* It's an int */
                            {
                                retType = CONV_NEXTPROP_INT;
                                d_retInt = (int)strtol(token, NULL, 10);
                                *paddy = (int *)&d_retInt;
                                goto retdefault;
                            }
                        }
                    }

                    fgets(&line[0], sizeof(line), conversionfile);
                }
            }
        fgets(&line[0], sizeof(line), conversionfile);
    }

retdefault:

    fseek(conversionfile, 0, SEEK_SET);	/* go to the beginning of the file */
    return retType;
}

int convGetNumberOfProperties(const char *section) /* figure out how many parameters in a section */
{
    int i = 0;
    /* char line[1024]; */
    /* fseek to the top if it's not already there */
    FILE *lastpos = conversionfile;
    fseek(conversionfile, 0, SEEK_SET);	/* go to the beginning of the file */

    while (!feof(conversionfile))
    {
        if (!strncmp(&line[0], section, sizeof(section)))
        {
            fgets(&line[0], sizeof(line), conversionfile);
            while (line[0] != '[')
            {
                if (feof(conversionfile))
                    /* error checking--it would be a fail if it reached eof before the closing '[' */
                {
                    printf("Error: Reached end of file before enclosing \"[]\" section closer in conversion.txt");
                    exit(1);
                }
                if (line[0] == '\n' || line[0] == '\0' || line[0] == ';')
                    /* if it's an empty line or a comment line skip it */
                {
                    fgets(&line[0], sizeof(line), conversionfile);
                }
                else
                {
                    fgets(&line[0], sizeof(line), conversionfile);
                    i++;
                }
            }

            fseek(conversionfile, ftell(lastpos), 0); /* put the reader back where it was before the function was called */
            return i;
        }
        else
            fgets(&line[0], sizeof(line), conversionfile);
    }
    /* reached eof without finding the parameter name */

    fseek(conversionfile, ftell(lastpos), 0); /* put the file pointer back where it was before the function was called */
    return 0;
}

int convGetNextProperty(const char *section, void **paddy)
/* this could stand to have some error checking subroutines */
{

    static Byte retInt;

 checktoken:
    /* first see what the first token is */

    while (line[0] == '\n' || line[0] == '\0' || line[0] == ';' || line[0] == ' ')
        fgets(&line[0], sizeof(line), conversionfile);
    if (line[0] != '\n' && line[0] != '\0' && line[0] != ';' && line[0] != ' ')
    {
        if (!tokencount)
        {
            token = strtok(&line[0], FADER_DELIMS); /* get the first token from the line */
            tokencount++;
        }
        else
        {
            if (token == NULL)
            {
                /* printf("token == NULL\n"); */
                fgets(&line[0], sizeof(line), conversionfile);
                tokencount = 0;
                /* now jump back up and start checking tokens again */
                goto checktoken;
            }

            if (*token != ' ')
                /* otherwise get the next token on this line if there is one */
            {
                token = strtok(NULL, FADER_DELIMS);
                tokencount++;
            }

            else                /* reached end of line */
            {
                /* nextline */
                fgets(&line[0], sizeof(line), conversionfile);
                tokencount = 0;
                /* now jump back up and start checking tokens again */
                goto checktoken;
            }
        }

        if (token != NULL)
            switch (*token)
        {
	case CONV_NEXTPROP_STRSYM:
                /* this is a string */
                /* remove quotes load the string, return */
                /* this should be done in a subroutine. GetNextProperty should be a void function pointer */
                /* that either runs a string or int capturing subroutine that returns a pointer to the int/char array */
                token = colon_blow(token, "\"", strlen(token));	/* zero terminate the "\""? */
                *paddy = (char *)token;	/* paddy now points to retstring */
                return CONV_NEXTPROP_STR;
                break;

	case CONV_NEXTPROP_BMASKSYM:
                /* remove colon, convert, and return int */
                token = colon_blow(token, ":", (strlen(token))); /* removes the ':' from */
                retInt = binstringtobyte(token);
                /* retInt = atoi(token); */
                *paddy = (Byte *)&retInt;
                return CONV_NEXTPROP_INT;
                break;
                /* this is a section. check if it's the one we're supposed to be in */

	case CONV_NEXTPROP_SECTIONSYM:
                if (!strncmp(token, section, strlen(section)))
                    /* if this is the section we're supposed to be in */
                {
                    /* skip a line and goto token parsing */
				/* dont have to skip bc it's at the top of the function */
		    fgets(&line[0], sizeof(line), conversionfile);
                    tokencount = 0;
                    goto checktoken;
                }
                else
                {
                    tokencount = 0;
                    goto startontop;
                }
                break;

	default:        	/* if it's an integer */
                /* atoi and load */
                retInt = strtol(token, NULL, 10);
                /* paddy = 0; */
                /*if (*paddy == 0)
                    {
                        printf("error - pointer to return UInt8 = NULL\n");
                        exit(1);
                    }*/
                *paddy = (Byte *)&retInt; /* (void *)(Byte *)retInt works too, but it doesnt solve the memleak.. */
                /*if (*paddy == 0)
                {
                    printf("error - pointer to return UInt8 = NULL\n");
                    exit(1);
                }*/
                return CONV_NEXTPROP_INT;
        }

        else 			/* the token pointer is NULL */
        {
            tokencount = 0;
            fgets(&line[0], sizeof(line), conversionfile);
            goto checktoken;
        }
    }

    else
        /* cycle through till eof looking for the section name, then start at the top and check once more */
    {
        tokencount = 0;
        while (!feof(conversionfile))
        {
            fgets(&line[0], sizeof(line), conversionfile);
            if (line[0] == '[')
            {
                token = strtok(&line[0], FADER_DELIMS);
                if (!strncmp(token, section, strlen(section)))
                {
                    fgets(&line[0], sizeof(line), conversionfile);
                    goto checktoken;
                }
            }
        }

        /* finally start at the top and look for the section name one last time */
    startontop:

        fseek(conversionfile, 0, SEEK_SET);
        while (!feof(conversionfile))
        {
            fgets(&line[0], sizeof(line), conversionfile);
            if (line[0] == '[')
            {
                token = strtok(&line[0], FADER_DELIMS);
                if (!strncmp(token, section, strlen(section)))
                {
                    fgets(&line[0], sizeof(line), conversionfile);
                    goto checktoken;
                }
            }
        }
    }
    /* if we're here it means there's nothing that matches sectionname in the file */
    return 0;
}

char *colon_blow(char *token, const char delims[MAX_DELIMS], size_t size)
/* removes colons, quotes, etc from strings, rets pointer to a new one */
{
    static char retString[CONV_MAX_STRLEN];
    /* clear retString */
    int x;
    for (x = 0; x < CONV_MAX_STRLEN; x++)
        retString[x] = '\0';
    int di = 0;			/* index number for delims */
    int ci = 0;			/* index number for copying */
    int rmv;			/* will be 0 if copy, 1 if not */
    lastbitmasklen = 0;
    int i;

    for (i = 0; i < size && *(token + i) != '\0'; i++)
    {
        rmv = 0;        	/* reset remove flag */
        di = 0;         	/* reset delims flag */
        /* first check if this char is one of the delimiters */
        while (delims[di] != '\0')
        {
            if (*(token + i) == delims[di]) /* *(token + i) == &token[i] */
            {
                rmv = 1;
                /* printf("X");                              these printfs were just for debugging */
            }

            di++;
        }
        /* check token + i, copying only the non-delim chars into retString */
        if (!rmv)
        {
            retString[ci] = *(token + i);
            /* printf("%c", retString[ci]); */
            ci++;
        }
    }
    /* printf("\n"); */

    /* zero terminate the last char? */
    retString[ci + 1] = '\0';
    if (delims[0] == CONV_NEXTPROP_BMASKSYM) /* set last bitmask so index range can be set properly */
        lastbitmasklen = strlen(retString);
    return &retString[0];
}
