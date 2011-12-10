/*
  bithex.h
  junoboss

  Created by Jeffrey Martin on 3/14/11.
  Copyright 2011 __MyCompanyName__. All rights reserved.

  this contains bit macros and stuff to determine and set button states, etc
*/

#define BOOL(x) (!(!(x)))

#define SET_BIT(word, pos) ((word) | (1L << (pos)))
#define CLR_BIT(word, pos) ((word) & ~(1L << (pos)))
#define FLP_BIT(word, pos) ((word) ^= (1L << (pos)))
#define GET_BIT(word, pos) BOOL((word) & (1L << (pos)))
