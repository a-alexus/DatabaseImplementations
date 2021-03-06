// query.c ... query scan functions
// part of signature indexed files
// Manage creating and using Query objects
// Written by John Shepherd, March 2019

#include "defs.h"
#include "query.h"
#include "reln.h"
#include "tuple.h"
#include "bits.h"
#include "tsig.h"
#include "psig.h"
#include "bsig.h"

// check whether a query is valid for a relation
// e.g. same number of attributes

int checkQuery(Reln r, char *q)
{
	if (*q == '\0') return 0;
	char *c;
	int nattr = 1;
	for (c = q; *c != '\0'; c++)
		if (*c == ',') nattr++;
	return (nattr == nAttrs(r));
}

// take a query string (e.g. "1234,?,abc,?")
// set up a QueryRep object for the scan

Query startQuery(Reln r, char *q, char sigs)
{
	Query new = malloc(sizeof(QueryRep));
	assert(new != NULL);
	if (!checkQuery(r,q)) return NULL;
	new->rel = r;
	new->qstring = q;
	new->nsigs = new->nsigpages = 0;
	new->ntuples = new->ntuppages = new->nfalse = 0;
	new->pages = newBits(nPages(r));
	switch (sigs) {
	case 't': findPagesUsingTupSigs(new); break;
	case 'p': findPagesUsingPageSigs(new); break;
	case 'b': findPagesUsingBitSlices(new); break;
	default:  setAllBits(new->pages); break;
	}
	new->curpage = 0;
	return new;
}

// scan through selected pages (q->pages)
// search for matching tuples and show each
// accumulate query stats

void scanAndDisplayMatchingTuples(Query q)
{
        //printf("k: %d\n m: %d\n", codeBits(q->rel), tsigBits(q->rel));
        Bits qpages = newBits(nPages(q->rel));

	assert(q != NULL);
        for (q->curpage = 0; q->curpage < nPages(q->rel); q->curpage++) {
                if(!bitIsSet(q->pages, q->curpage))
                        continue;

                Count nMatch = 0;
                Page p = getPage(dataFile(q->rel), q->curpage);
                q->ntuppages++;
                for (q->curtup = 0; q->curtup < pageNitems(p); q->curtup++) {
                        Tuple t = getTupleFromPage(q->rel, p, q->curtup);
                        q->ntuples++;
                        if (tupleMatch(q->rel, t, q->qstring)) {
                                setBit(qpages, q->curpage);
                                /*
                                printf("(p, op): (%d,%d)\t\t (tp, ot): (%d,%d)\t\t", 
                                        q->curpage,
                                        q->curtup,
                                        (q->ntuples / maxTsigsPP(q->rel)), 
                                        (q->ntuples % maxTsigsPP(q->rel)));*/

                                nMatch++;
                                showTuple(q->rel, t);
                        }
                        free(t);
                }

                free(p);
                if (nMatch == 0)
                        q->nfalse++;
        }

        //showHexBits(qpages); printf("\n");
        free(qpages);

}


// print statistics on query

void queryStats(Query q)
{
	printf("# sig pages read:    %d\n", q->nsigpages);
	printf("# signatures read:   %d\n", q->nsigs);
	printf("# data pages read:   %d\n", q->ntuppages);
	printf("# tuples examined:   %d\n", q->ntuples);
	printf("# false match pages: %d\n", q->nfalse);
}

// clean up a QueryRep object and associated data

void closeQuery(Query q)
{
	free(q->pages);
	free(q);
}

