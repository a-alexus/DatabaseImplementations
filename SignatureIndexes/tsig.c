// tsig.c ... functions on Tuple Signatures (tsig's)
// part of signature indexed files
// Written by John Shepherd, March 2019

#include <unistd.h>
#include <string.h>
#include <stdlib.h>
#include "defs.h"
#include "tsig.h"
#include "sig.h"
/*
#include "reln.h"
#include "bits.h"
#include "sig.h"*/


// make a tuple signature

Bits makeTupleSig(Reln r, Tuple t)
{
	assert(r != NULL && t != NULL);
        Bits tsig;
        switch(sigType(r)) {
        case 'c':
                tsig = catcSig(r, t, tsigBits(r), 1);
                break;
        case 's':
                tsig = simcSig(r, t, tsigBits(r));
                break;
        default: 
                tsig = newBits(tsigBits(r));
                assert(tsig != NULL);
                setAllBits(tsig);
                break;
        }
	return tsig;
}


// find "matching" pages using tuple signatures

void findPagesUsingTupSigs(Query q)
{
	assert(q != NULL);
        Bits qsig = makeTupleSig(q->rel, q->qstring);
        unsetAllBits(q->pages);

        Bits tsig = newBits(tsigBits(q->rel));
        assert(tsig != NULL);
        for (PageID tpid = 0; tpid < nTsigPages(q->rel); tpid++) {
               Page p = getPage(tsigFile(q->rel), tpid);
               q->nsigpages++;
               for(Count i = 0; i < pageNitems(p); i++) {
                       getBits(p, i, tsig);
                       if(isSubset(qsig, tsig)) {
                               PageID dpid = q->nsigs / maxTupsPP(q->rel);
                               setBit(q->pages, dpid);
                       }
                       q->nsigs++;
               }
               free(p);
        }

        freeBits(tsig);
        freeBits(qsig);

}
