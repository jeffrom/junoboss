/*  bithex.c
    junoboss

    Created by Jeffrey Martin on 3/14/11.
    Copyright 2011 __MyCompanyName__. All rights reserved.

    this contains bit, nibble, and hex operations that are too complicated to do with a macro
*/

#include <CoreMIDI/CoreMIDI.h>

#include "bithex.h"


Byte binstringtobyte(char *x);
void display_bits(Byte x);
int convSet_bitMask(Byte bmsk, Byte *dest, int istart, int iend);
int convCmp_bitMask(Byte bmsk, Byte src, int istart, int iend);


int convSet_bitMask(Byte bmsk, Byte *dest, int istart, int iend) {
/* rets 1 if it wrote something, 0 if it didn't */
    int ret = 0;
    if (!iend) {
        /* if there's only one index pos to set */
        if (GET_BIT(bmsk, 0) != GET_BIT(*dest, istart)) {
            FLP_BIT(*dest, istart);
            ret = 1;
        }
    }

    else {		   /* otherwise iend is at least istart + 2 */
	int i, x;
        for (i = istart, x = 0; i < iend; i++, x++) {
            if (GET_BIT(bmsk, x) != GET_BIT(*dest, i)) {
		FLP_BIT(*dest, i);
		ret = 1;
            }
	}
    }
    return ret;
}


int convCmp_bitMask(Byte bmsk, Byte src, int istart, int iend) {
    /* returns 0 if bmsk and src dont match, 1 if they do */
    int ret = 1;
    if (!iend) {
        /* if there's only one index number to check (iend = 0) */
	if (GET_BIT(bmsk, 0) != GET_BIT(src, istart)) {
	    return 0;
	}
        else {
            return 1;
	}
    }
    /* if there's a group of index numbers to check */
    else {
	int i, x;
        for (i = istart, x = 0; i < iend; i++, x++) {
            if (GET_BIT(src, i) != GET_BIT(bmsk, x)) {
                ret = 0;
                break;
            }
        }
    }
    return ret;
}

void display_bits(Byte x) {
/* displays bits in little endian for a Byte */
    int i;
    for (i = (sizeof(Byte) * 8) - 1; i >= 0 ; i--) {
        printf("%d ", GET_BIT(x, i));
    }
    printf("\n");
}

Byte binstringtobyte(char *x) {
	Byte sum = 0;
	int n, b, m;
	size_t len;
	char p;
	len = strlen(x) - 1;

	int i;
	for (i = 0; i <= len; i++) {
	    if (x[i] == '\0')
		break;
	    p = x[i];		/* can also be like p = x[i]; */
	    /* printf("Char %c --> ", p); */
	    n = atoi(&p);		/* char to numeric value */
	    /* printf("%d\n", n); */
	    /*if (n > 1 || n < 0)
	      {
	      printf("Error: Input is %d, and neither a 0 nor a 1.\n", n);
	      exit(1);
	      }*/
	    for (b = 1, m = (int)len; m > i; m--) { /* typecasting b/c len is a size_t */
	    b *= 2;
	    }
	    /* sum it */
	    sum = sum + n * b;
	}
	return sum;
}
