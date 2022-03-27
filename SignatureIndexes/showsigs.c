#include "defs.h"
#include "reln.h"
#include "tuple.h"
#include "page.h"
#include "tsig.h"
#include "bits.h"

int main(int argc, char **argv) 
{
        char *rname;
        Reln r;

        if (argc != 2) {
                fprintf(stderr, "usage: %s <rel>\n", argv[0]);
                return 1;
        }

        rname = argv[1];

        if ((r = openRelation(rname)) == NULL) {
                fprintf(stderr, "can't open relation: %s\n", rname);
                return 1;
        }

        // read tsig and data tuple from first page
        Page dp = getPage(dataFile(r), 0);
        for (int i = 0; i < nTuples(r) && i < maxTupsPP(r); i++) {
                Tuple t = getTupleFromPage(r, dp, i);
                printf("%s\t", t);
                Bits tsig = makeTupleSig(r, t);
                freeBits(tsig);
                free(t);
        }
        free(dp);

        printf("tsigs in file:\n");
        Page tp = getPage(tsigFile(r), 0);
        Bits tsig = newBits(tsigBits(r));
        for (int i = 0; i < nTsigs(r) && i < maxTsigsPP(r); i++) {
                unsetAllBits(tsig);
                getBits(tp, i, tsig);
                showHexBits(tsig);
                printf("\n");
        }
        free(tsig);
        free(tp);

}
