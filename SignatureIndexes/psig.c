// psig.c ... functions on page signatures (psig's)
// part of signature indexed files
// Written by John Shepherd, March 2019

#include "defs.h"
#include "reln.h"
#include "query.h"
#include "psig.h"
#include "sig.h"

Bits makePageSig(Reln r, Tuple t)
{
	assert(r != NULL && t != NULL);
        Bits psig;
        switch(sigType(r)) {
        case 'c':
                psig = catcSig(r, t, psigBits(r), maxTupsPP(r));
                break;
        case 's':
                psig = simcSig(r, t, psigBits(r));
                break;
        default:
                psig = newBits(psigBits(r));
                assert(psig != NULL);
                setAllBits(psig);
                break;
        }
	return psig;
}

void findPagesUsingPageSigs(Query q)
{
	assert(q != NULL);
        Bits qsig = makePageSig(q->rel, q->qstring);
        unsetAllBits(q->pages);

        Bits psig = newBits(psigBits(q->rel));
        assert(psig != NULL);
        for (PageID ppid = 0; ppid < nPsigPages(q->rel); ppid++) {
                Page p = getPage(psigFile(q->rel), ppid);
                q->nsigpages++;

                for (Count i = 0; i < pageNitems(p); i++) {
                        getBits(p, i, psig);
                        if(isSubset(qsig, psig)) {
                                setBit(q->pages, q->nsigs);
                        }
                        q->nsigs++;
                }
                free(p);
        }
        freeBits(psig);
        freeBits(qsig);
}

