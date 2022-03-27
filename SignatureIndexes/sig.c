#include "hash.h"

/*
 * Returns a bit string of length m bits with k bits within it set to 1. The
 * k bits are randomly distributed over the least significant u bits of the bitstring.
 */
static Bits codeword(char *attr, Count u, Count k, Count m) 
{
        assert(u <= m);
        Bits b = newBits(m);
        if (isUnknownVal(attr)) {
                return b;
        }
        Count nbits = 0;
        srandom(hash_any(attr, strlen(attr)));
        while(nbits < k) {
                int i = random() % u;
                if (!bitIsSet(b, i)) {
                        setBit(b, i);
                        nbits++;
                }
        }

        return b;
}

Bits catcSig(Reln r, Tuple t, Count siglen, Count nTup) 
{
        Bits sig = newBits(siglen);
        assert(sig != NULL);
        unsetAllBits(sig);

        Count cwlen = (siglen / nAttrs(r));
        Count nBitsToSet = (cwlen / 2) / nTup;
        char **attrs = tupleVals(r, t);
        for (int i = nAttrs(r) - 1; i >= 1; i--) {
                Bits cw = codeword(attrs[i], cwlen, nBitsToSet, siglen);
                shiftBits(cw, (i * cwlen) + (siglen % nAttrs(r)));
                orBits(sig, cw);
                freeBits(cw);
        }

        cwlen += siglen % nAttrs(r);
        nBitsToSet = (cwlen / 2) / nTup;
        Bits cw = codeword(attrs[0], cwlen, nBitsToSet, siglen);
        orBits(sig, cw);
        freeBits(cw);
        freeVals(attrs, nAttrs(r));
        return sig;
}

Bits simcSig(Reln r, Tuple t, Count siglen)
{
        Bits sig = newBits(siglen);
        assert(sig != NULL);
        unsetAllBits(sig);
        char **attrs = tupleVals(r, t);
        for (int i = 0; i < nAttrs(r); i++) {
                Bits cw = codeword(attrs[i], siglen, codeBits(r), siglen);
                orBits(sig, cw);
                freeBits(cw);
        }

        freeVals(attrs, nAttrs(r));
        return sig;
}
