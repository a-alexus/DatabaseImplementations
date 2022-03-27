// bsig.c ... functions on Tuple Signatures (bsig's)
// part of signature indexed files
// Written by John Shepherd, March 2019

#include "defs.h"
#include "reln.h"
#include "query.h"
#include "bsig.h"
#include "psig.h"

void findPagesUsingBitSlices(Query q)
{
	assert(q != NULL);
        Bits qsig = makePageSig(q->rel, q->qstring);
        Bits bsig = newBits(bsigBits(q->rel));
        setAllBits(q->pages);
        Page bsigpage = NULL;
        PageID bsigpid = -1;
        for (Count i = 0; i < psigBits(q->rel); i++) {
                if (!bitIsSet(qsig, i)) continue;

                if (bsigpid != i / maxBsigsPP(q->rel)) {
                        if (bsigpage != NULL) 
                                free(bsigpage);
                        bsigpid = i / maxBsigsPP(q->rel);
                        bsigpage = getPage(bsigFile(q->rel), bsigpid);
                        q->nsigpages++;
                }

                q->nsigs++;
                getBits(bsigpage, i % maxBsigsPP(q->rel), bsig);
                for (Count j = 0; j < nPages(q->rel); j++) {
                        if(!bitIsSet(bsig, j)) {
                                unsetBit(q->pages, j);
                        }
                }
        }

        if (bsigpage != NULL) free(bsigpage);
        free(bsig);
        free(qsig);
}

