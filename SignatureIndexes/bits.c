// bits.c ... functions on bit-strings
// part of signature indexed files
// Bit-strings are arbitrarily long byte arrays
// Least significant bits (LSB) are in array[0]
// Most significant bits (MSB) are in array[nbytes-1]

// Written by John Shepherd, March 2019

#include <assert.h>
#include "defs.h"
#include "bits.h"
#include "page.h"

#define BYTE_NBITS 8

typedef struct _BitsRep {
	Count  nbits;		  // how many bits
	Count  nbytes;		  // how many bytes in array
	Byte   bitstring[1];  // array of bytes to hold bits
	                      // actual array size is nbytes
} BitsRep;

static Byte *getByte(Bits b, int position) 
{
        return &b->bitstring[position / BYTE_NBITS];
}

/*
 * Returns the offset of a bit within a byte. 
 */
static int getOffset(int position) 
{
        return position % BYTE_NBITS;
}

// create a new Bits object

Bits newBits(int nbits)
{
	Count nbytes = iceil(nbits, BYTE_NBITS);
	Bits new = malloc(2*sizeof(Count) + nbytes);
	new->nbits = nbits;
	new->nbytes = nbytes;
	memset(&(new->bitstring[0]), 0, nbytes);
	return new;
}

// release memory associated with a Bits object

void freeBits(Bits b)
{
        free(b);
}

// check if the bit at position is 1

Bool bitIsSet(Bits b, int position)
{
	assert(b != NULL);
	assert(0 <= position && position < b->nbits);
	assert(b != NULL);
	assert(0 <= position && position < b->nbits);
        Byte *byte = getByte(b, position);
        Byte mask = 1 << getOffset(position);
	return (*byte & mask) != 0; 
}

// check whether one Bits b2 is a subset of Bits b1

Bool isSubset(Bits b1, Bits b2)
{
	assert(b1 != NULL && b2 != NULL);
	assert(b1->nbytes == b2->nbytes);

        Bool subset = TRUE;
        for(int i = 0; i < b2->nbytes; i++) 
        {
                if ((b1->bitstring[i] & b2->bitstring[i]) != b1->bitstring[i])
                {
                        subset = FALSE;
                        break;
                }
        }

        return subset;
}

// set the bit at position to 1

void setBit(Bits b, int position)
{
	assert(b != NULL);
	assert(0 <= position && position < b->nbits);
        Byte *byte = getByte(b, position);
        *byte |= (1 << getOffset(position));
}

// set all bits to 1

void setAllBits(Bits b)
{
	assert(b != NULL);
	assert(b != NULL);
        memset(b->bitstring, ~(*b->bitstring & 0), b->nbytes);
}

// set the bit at position to 0

void unsetBit(Bits b, int position)
{
	assert(b != NULL);
	assert(0 <= position && position < b->nbits);
        Byte *byte = getByte(b, position);
        Byte mask = ~(1 << getOffset(position));
        *byte &= mask;
}

// set all bits to 0

void unsetAllBits(Bits b)
{
	assert(b != NULL);
	memset(b->bitstring, 0, b->nbytes);
}

// bitwise AND ... b1 = b1 & b2

void andBits(Bits b1, Bits b2)
{
	assert(b1 != NULL && b2 != NULL);
	assert(b1->nbytes == b2->nbytes);
        for (int i = 0; i < b1->nbytes; i++) {
                b1->bitstring[i] &= b2->bitstring[i];
        }
}

// bitwise OR ... b1 = b1 | b2

void orBits(Bits b1, Bits b2)
{
	assert(b1 != NULL && b2 != NULL);
	assert(b1->nbytes == b2->nbytes);
        for (int i = 0; i < b1->nbytes; i++) {
                b1->bitstring[i] |= b2->bitstring[i];
        }
}

// left-shift ... b1 = b1 << n
static
void leftShiftBits(Bits b, int n)
{
        Byte mask;
        for (int i = b->nbytes - 1; i >= 0; i--) {
                int n_upper = n % BYTE_NBITS;
                if (n_upper == 0 && n > 0) {
                        n_upper = BYTE_NBITS;
                }
                /* shifted_byte is the resulting byte index in b->bitstring of
                 * the n_upper higher order bits after the shift. */
                int shifted_byte = iceil(n, BYTE_NBITS) + i;
                assert(shifted_byte > 0);
                if (shifted_byte < b->nbytes) {
                        /* If the shifted_byte does not overflow past the
                         * bitstring array, move the top n_upper bits to the
                         * shifted byte. */
                        mask = ~(mask & 0) << (BYTE_NBITS - n_upper);
                        b->bitstring[shifted_byte] |= (mask & b->bitstring[i]) >> (BYTE_NBITS - n_upper);
                }
                
                /* shift the remaining 8-n_upper lower order bits by n_upper and move them
                 * to the byte before the shifted byte. */
                if (shifted_byte-1 < b->nbytes) {
                        mask = ~(mask & 0) >> n_upper;
                        b->bitstring[shifted_byte - 1] = (mask & b->bitstring[i]) << n_upper;

                }

                if (n >= BYTE_NBITS) {
                        b->bitstring[i] = 0;
                }

        }

}

static 
void rightShiftBits(Bits b, int n) 
{
        Byte mask;
        for (int i = 0; i < b->nbytes; i++) {
                int n_lower = n % BYTE_NBITS;
                if (n_lower == 0 && n > 0) {
                        n_lower = BYTE_NBITS;
                }
                int shifted_byte = iceil(n, BYTE_NBITS) + i - b->nbytes;
                if (shifted_byte >= 0) {
                        mask = ~(mask & 0) >> (BYTE_NBITS - (n_lower));
                        b->bitstring[shifted_byte] |= (mask & b->bitstring[i] << (BYTE_NBITS - (n_lower)));
                }

                mask = ~(mask & 0) << (n_lower);
                b->bitstring[shifted_byte + 1] = (mask & b->bitstring[i]) >> (n_lower);
                if (n >= BYTE_NBITS) {
                        b->bitstring[i] = 0;
                }
        }
}

// left-shift ... b1 = b1 << n
// negative n gives right shift
void shiftBits(Bits b, int n)
{
        if (n == 0)
                return;
        
        if (n > 0)
                leftShiftBits(b, n);
        else
                rightShiftBits(b, -1 * n);

}

// get a bit-string (of length b->nbytes)
// from specified position in Page buffer
// and place it in a BitsRep structure

void getBits(Page p, Offset pos, Bits b)
{
        // gets the pos'th tuple in p
        Byte *src = addrInPage(p, pos, b->nbytes);
        memcpy(b->bitstring, src, sizeof(*b->bitstring) * b->nbytes);
}

// copy the bit-string array in a BitsRep
// structure to specified position in Page buffer

void putBits(Page p, Offset pos, Bits b)
{
        // gets the pos'th tuple in p
        Byte *dest = addrInPage(p, pos, b->nbytes);
        memcpy(dest, b->bitstring, sizeof(*b->bitstring) * b->nbytes);
}

// show Bits on stdout
// display in order MSB to LSB
// do not append '\n'

void showBits(Bits b)
{
	assert(b != NULL);
	for (int i = b->nbytes-1; i >= 0; i--) {
		for (int j = 7; j >= 0; j--) {
			Byte mask = (1 << j);
			if (b->bitstring[i] & mask)
				putchar('1');
			else
				putchar('0');
		}
	}
}


void showHexBits(Bits b)
{
        assert(b != NULL);
        for (int i = b->nbytes - 1; i >= 0; i--) {
                printf("%02x", b->bitstring[i]); 
        }

}


Count nBytes(Bits b) 
{
        return b->nbytes;
}

Count nBits(Bits b) 
{
        return b->nbits;
}
