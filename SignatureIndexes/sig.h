/*
 * Interface to create catc and simc signatures
 */

 #ifndef SIG_H
 #define SIG_H 1

#include "defs.h"
#include "reln.h"
#include "bits.h"

Bits catcSig(Reln r, Tuple t, Count siglen, Count nTup);
Bits simcSig(Reln r, Tuple t, Count siglen);

 #endif