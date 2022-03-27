/*
 * src/tutorial/intset.c
 *
 ******************************************************************************
  This file contains routines that can be bound to a Postgres backend and
  called by the backend in the process of processing queries.  The calling
  format for these routines is dictated by Postgres architecture.
******************************************************************************/

#include "postgres.h"

#include "fmgr.h"
#include "libpq/pqformat.h"		/* needed for send/recv functions */
#include <ctype.h>
#include <string.h>

PG_MODULE_MAGIC;

typedef struct IntSet
{
        int32             vl_len_;
        int32             nelems;
        int32             elems[FLEXIBLE_ARRAY_MEMBER];
}			IntSet;

#define DatumGetIntsetP(d)      ((IntSet *) PG_DETOAST_DATUM(d))
#define PG_GETARG_INTSET_P(x)      DatumGetIntsetP(PG_GETARG_DATUM(x))
#define PG_RETURN_INTSET_P(x)   PG_RETURN_POINTER(x)

/*
 * Returns an intset containing the members elems which has nelems in it. 
 */
static IntSet *
intset_create(const int32 *elems, int32 nelems)
{
        int32 vl_len_;
        IntSet *intset;

        vl_len_ = VARHDRSZ + sizeof(vl_len_) + (sizeof(*elems) * nelems);
        intset = (IntSet *) palloc(vl_len_);
        SET_VARSIZE(intset, vl_len_);

        memcpy(intset->elems, elems, sizeof(*elems) * nelems);
        intset->nelems = nelems;

        return intset;
}

/*
 * Compares two int32 integers. Returns 0 if a and b are the same int32 value. 
 * Returns 1 if a is larger than b.
 * Returns -1 if b is larger than a.
 */
static int
intset_cmp(const void *a, const void *b) 
{
        return *((int32 *)a) - *((int32 *)b);
}


/*
 * Returns a pointer to the next non-whitespace character in str. 
 */
static char *
intset_nextchr(const char *str)
{
        for(; isspace(*str); str++);
        return (char *)str;
}

static void 
intset_exit_invalid_input(const char *input) 
{
        ereport(ERROR,
                (errcode(ERRCODE_INVALID_TEXT_REPRESENTATION),
                 errmsg("invalid input syntax for type %s: \"%s\"",
                        "intset", input)));
}


/*
 * Returns a pointer to the next non-whitespace character that matches the
 * expected character. Returns NULL if the next non-whitespace character is not
 * the expected character. 
 */
static char *
intset_match_nextchr(char expected, const char *str) 
{
        char *match = intset_nextchr(str);
        if (*match != expected)
                return NULL;

        return match;
}

/*
 * Counts the number of integers in an intset represented as a string. 
 * Returns -1 if the supplied string does not represent a syntactically valid intset. 
 * Returns a number greater or equal to 0 representing the number of integers
 * (including duplicate integers) found in str if the supplied string is a valid intset.
 */
static int32
intset_validate(const char *str) 
{
        int error = -1;
        int32 n_ints = 0;
        const char *tmp;
        const char *cur;

        cur = str;

        cur = intset_match_nextchr('{', cur);
        if (cur == NULL)
                return error;

        cur++;
        tmp = intset_match_nextchr('}', cur);
        if (tmp != NULL) 
        {
                cur = ++tmp;
                cur = intset_match_nextchr('\0', cur);
                if (cur == NULL)
                        return error;

                return n_ints;
        }


        /* The opening brace '{' should be followed by optional whitespace followed by a digit.*/
        cur = intset_nextchr(cur);
        if (!isdigit(*cur))
                return error;

        /* Count the first integer and loop past all digits that comprise it.*/
        n_ints = 1;
        for ( ; isdigit(*cur); cur++);

        cur = intset_nextchr(cur);

        /* Read through subsequent commas and integers if any. */
        while (*cur != '\0' && *cur != '}') 
        {
                cur = intset_match_nextchr(',', cur);
                if (cur == NULL)
                        return error;
                cur++;

                cur = intset_nextchr(cur);
                if (!isdigit(*cur))
                        return error;

                n_ints++;

                /* Iterate past subsequent digits making up the integer */
                for ( ; isdigit(*cur); cur++);

                cur = intset_nextchr(cur);
        }

        /* 
         * Check that the terminating intset character '}' is the last non-whitespace
         * character in the string.
         */
        cur = intset_match_nextchr('}', cur);
        if (cur != NULL) 
        {
                cur++;
                cur = intset_match_nextchr('\0', cur);
                if (cur == NULL)
                        return error;
        } 
        else
        {
                return error;
        }

        return n_ints;
}


/*
 * returns an array of n_ints integers found in the supplied input. 
 * returns NULL on error i.e. when the number of integers found in the supplied input does not
 * match n_ints. 
 */
static int32 *
intset_parse(const char *input, int32 n_ints) 
{
        int32 *ints = (int32 *) palloc(sizeof(int32) * n_ints);
        const char *str = input;

        int32 i = 0;
        while (i < n_ints && *str != '\0') 
        {
                /* iterate past non-digits */
                for(; !isdigit(*str) && *str != '\0'; str++);
                
                if (sscanf(str, "%d", &ints[i]) != 1)
                {
                        pfree(ints);
                        return NULL;
                }       

                /* iterate past digits just scanned into ints */
                for(; isdigit(*str) && *str != '\0'; str++);

                i++;
        }

        if (i != n_ints)
        {
                pfree(ints);
                return NULL;
        }

        return ints;
}

/*
 * Creates an array of distinct integers contained within ints where ints contains
 * n_ints members. 
 * Returns the number of distinct integers and sets ret to point to the created
 * array of distinct integers. If n_ints is 0, then an array of size 0 is
 * alloacted and set to ret and 0 is returned. 
 */
static int32
intset_dedup(const int32 *ints, int32 n_ints, int32 **ret) 
{
        int32 n_distinct = 0;
        int32 *dedup = palloc(sizeof(int32) * n_ints);

        if (n_ints == 0)
        {
                *ret = dedup;
                return n_distinct;
        }

        n_distinct = 1;

        /* 
         * Propably could have altered the ints array rather than copying it
         * into a new array to improve performance. Seemed more error prone though.
         */ 
        memcpy(dedup, ints, sizeof(int32) * n_ints);

        if (n_ints == 1)
        {
                *ret = dedup;
                return n_distinct;
        }

        qsort(dedup, n_ints, sizeof(int32), intset_cmp);

        for (int32 i = 1, j = 0; i < n_ints; i++) 
        {
                if (dedup[i] != dedup[j]) 
                {
                        j++;
                        dedup[j] = dedup[i];
                        n_distinct = j + 1;
                }
        }

        /* reclaim space taken up by duplicate ints */
        *ret = repalloc(dedup, sizeof(int32) * n_distinct);
        return n_distinct; 
}

/*****************************************************************************
 * Input/Output functions
 *****************************************************************************/


PG_FUNCTION_INFO_V1(intset_in);

Datum
intset_in(PG_FUNCTION_ARGS)
{
        int32 *ints;
        int32 n_ints;
        int32 *distinct_ints;
        int32 n_distinct;
        char    *str;
        IntSet *intset;


        str =  PG_GETARG_CSTRING(0);

        n_ints = intset_validate(str); 
        if (n_ints == -1)
                intset_exit_invalid_input(str);


        ints = intset_parse(str, n_ints);
        if (ints == NULL)
                intset_exit_invalid_input(str);


        n_distinct = intset_dedup(ints, n_ints, &distinct_ints);

        intset = intset_create(distinct_ints, n_distinct);

        pfree(ints);
        pfree(distinct_ints);

	PG_RETURN_INTSET_P(intset);
}


PG_FUNCTION_INFO_V1(intset_out);

Datum
intset_out(PG_FUNCTION_ARGS)
{
        IntSet *intset = PG_GETARG_INTSET_P(0);
        char *s = "{";
        char *prev = NULL;
        char *res;

        if (intset->nelems > 0)
                res = psprintf("%s%d", s, intset->elems[0]);
        else 
                res = psprintf("%s", s);

        for (int32 i = 1; i < intset->nelems; i++) 
        {
                prev = res;
                res = psprintf("%s,%d", prev, intset->elems[i]);
                pfree(prev);
        }

        prev = res;
        res = psprintf("%s}", prev);
        pfree(prev);

        PG_FREE_IF_COPY(intset, 0);
        PG_RETURN_CSTRING(res);
}

/*****************************************************************************
 * Operations
 *****************************************************************************/

/*
 * compares the members of two IntSets. Returns 1 if every element in IntSet a's elems is 
 * also a member of IntSet b and vise versa. 
 */
static int
intset_eq_internal(const IntSet *a, const IntSet *b)
{
        int eq = 1;

        if (a->nelems != b->nelems)
                return 0;

        for(int32 i = 0; i < a->nelems; i++)
        {
                if (a->elems[i] != b->elems[i])
                {
                        eq = 0;
                        break;
                }
        }

        return eq;
}


/*
 * Returns 1 if every element in IntSet a's elems is also a member of IntSet b.
 */
static int
intset_subset_internal(const IntSet *a, const IntSet *b)
{
        int subset = 1;
        int32 i = 0, j = 0;

        if (a->nelems > b->nelems)
                return 0;


        while (i < a->nelems) 
        {
                if (b->elems[j] < a->elems[i])
                        j++;
                else if (b->elems[j] == a->elems[i])
                {
                        j++;
                        i++;
                } 
                else 
                {
                        subset = 0;
                        break;
                }
        }

        return subset;
}


/*
 * Returns whether or not the integer elem is a member of the set intset.
 * Returns 1 if elem is a member of intset. Returns 0 otherwise. 
 */
PG_FUNCTION_INFO_V1(intset_member);
Datum
intset_member(PG_FUNCTION_ARGS)
{

        int32 elem =  PG_GETARG_INT32(0);
        IntSet *intset = PG_GETARG_INTSET_P(1);
        int32 mid;
        int32 hi = intset->nelems - 1;
        int32 lo = 0;
        int member = 0;

        if (intset->nelems == 0)
        {
                PG_FREE_IF_COPY(intset, 1);
                PG_RETURN_BOOL(member);
        }

        while (lo <= hi)
        {
                mid = lo + ((hi - lo) / 2);

                if (elem == intset->elems[mid])
                {
                        member = 1;
                        break;
                }
                else if (elem < intset->elems[mid])
                {
                        hi = mid - 1;
                }
                else
                {
                        lo = mid + 1;
                }

        }

        PG_FREE_IF_COPY(intset, 1);
        PG_RETURN_BOOL(member);
}


PG_FUNCTION_INFO_V1(intset_cardinality);
Datum
intset_cardinality(PG_FUNCTION_ARGS)
{
        IntSet *s = PG_GETARG_INTSET_P(0);
        int32 nelems = s->nelems;

        PG_FREE_IF_COPY(s, 0);
        PG_RETURN_INT32(nelems);
}


PG_FUNCTION_INFO_V1(intset_superset);
Datum
intset_superset(PG_FUNCTION_ARGS)
{
        IntSet  *a = PG_GETARG_INTSET_P(0);
        IntSet  *b = PG_GETARG_INTSET_P(1);
        int superset = intset_subset_internal(b, a);

        PG_FREE_IF_COPY(a, 0);
        PG_FREE_IF_COPY(b, 1);
        PG_RETURN_BOOL(superset);
}


PG_FUNCTION_INFO_V1(intset_subset);
Datum
intset_subset(PG_FUNCTION_ARGS)
{
        IntSet  *a = PG_GETARG_INTSET_P(0);
        IntSet  *b = PG_GETARG_INTSET_P(1);
        int subset = intset_subset_internal(a, b);

        PG_FREE_IF_COPY(a, 0);
        PG_FREE_IF_COPY(b, 1);
        PG_RETURN_BOOL(subset);
}


PG_FUNCTION_INFO_V1(intset_eq);
Datum
intset_eq(PG_FUNCTION_ARGS)
{
        IntSet  *a = PG_GETARG_INTSET_P(0);
        IntSet  *b = PG_GETARG_INTSET_P(1);
        int eq = intset_eq_internal(a, b);

        PG_FREE_IF_COPY(a, 0);
        PG_FREE_IF_COPY(b, 1);
        PG_RETURN_BOOL(eq);
}


PG_FUNCTION_INFO_V1(intset_neq);
Datum
intset_neq(PG_FUNCTION_ARGS)
{
        IntSet  *a = PG_GETARG_INTSET_P(0);
        IntSet  *b = PG_GETARG_INTSET_P(1);
        int neq = !intset_eq_internal(a, b);

        PG_FREE_IF_COPY(a, 0);
        PG_FREE_IF_COPY(b, 1);
        PG_RETURN_BOOL(neq);
}


PG_FUNCTION_INFO_V1(intset_intersection);
Datum
intset_intersection(PG_FUNCTION_ARGS)
{
        IntSet  *a = PG_GETARG_INTSET_P(0);
        IntSet  *b = PG_GETARG_INTSET_P(1);
        int32 nelems = 0;
        int32 i = 0, j = 0;
        IntSet *result;
        int32 *elems;

        if (a->nelems <= b->nelems)
                elems = (int32 *) palloc(sizeof(int32) * a->nelems);
        else
                elems = (int32 *) palloc(sizeof(int32) * b->nelems);

        while (i < a->nelems && j < b->nelems)
        {
                if (a->elems[i] < b->elems[j])
                {
                        i++;
                } 
                else if (a->elems[i] == b->elems[j])
                {
                        elems[nelems] = a->elems[i];
                        i++;
                        j++;
                        nelems++;
                } 
                else 
                {
                        j++;
                }

        }

        result = intset_create(elems, nelems);
        pfree(elems);

        PG_FREE_IF_COPY(a, 0);
        PG_FREE_IF_COPY(b, 1);
        PG_RETURN_INTSET_P(result);
}


PG_FUNCTION_INFO_V1(intset_union);
Datum
intset_union(PG_FUNCTION_ARGS)
{

        IntSet  *a = PG_GETARG_INTSET_P(0);
        IntSet  *b = PG_GETARG_INTSET_P(1);
        int32 i = 0, j = 0;
        int32 nelems = 0;
        int32 *elems;
        IntSet *result;

        /* 
         * Don't care if a little too much memory is allocated here i.e. if a
         * and b share some elements. The array is free'd at the end of the
         * function. The number of distinct elements between a and b is held in
         * nelems and only this number of elements is allocated and copied into
         * the IntSet result.
         */
        elems = (int32 *) palloc(sizeof(int32) * (a->nelems + b->nelems));

        while (i < a->nelems && j < b->nelems)
        {
                if (a->elems[i] < b->elems[j])
                {
                        elems[nelems] = a->elems[i];
                        i++;
                } 
                else if (a->elems[i] == b->elems[j])
                {
                        elems[nelems] = a->elems[i];
                        i++;
                        j++;
                } 
                else 
                {
                        elems[nelems] = b->elems[j];
                        j++;
                }

                nelems++;
        }

        /* 
         * Copy over remaining elements in a or b if the loop didn't iterate
         * over all of them. 
         */
        if (i < a->nelems)
        {
                memcpy(elems + nelems, a->elems + i, sizeof(int32) * (a->nelems - i));
                nelems += a->nelems - i;
        } 
        else if (j < b->nelems)
        {
                memcpy(elems + nelems, b->elems + j, sizeof(int32) * (b->nelems - j));
                nelems += b->nelems - j;
        }

        result = intset_create(elems, nelems);
        pfree(elems);


        PG_FREE_IF_COPY(a, 0);
        PG_FREE_IF_COPY(b, 1);
        PG_RETURN_INTSET_P(result);
}



PG_FUNCTION_INFO_V1(intset_disjunction);
Datum
intset_disjunction(PG_FUNCTION_ARGS)
{

        IntSet  *a = PG_GETARG_INTSET_P(0);
        IntSet  *b = PG_GETARG_INTSET_P(1);
        int32 nelems = 0;
        int32 i = 0, j = 0;
        IntSet *result;
        int32 *elems;

        elems = (int32 *) palloc(sizeof(int32) * (a->nelems + b->nelems));

        while (i < a->nelems && j < b->nelems)
        {
                if (a->elems[i] < b->elems[j])
                {
                        elems[nelems] = a->elems[i];
                        nelems++;
                        i++;
                } 
                else if (a->elems[i] == b->elems[j])
                {
                        i++;
                        j++;
                } 
                else 
                {
                        elems[nelems] = b->elems[j];
                        nelems++;
                        j++;
                }

        }

        /* Copy over remaining elements in a or b if the loop didn't iterate over all of them. */
        if (i < a->nelems)
        {
                memcpy(elems + nelems, a->elems + i, sizeof(int32) * (a->nelems - i));
                nelems += a->nelems - i;
        } 
        else if (j < b->nelems)
        {
                memcpy(elems + nelems, b->elems + j, sizeof(int32) * (b->nelems - j));
                nelems += b->nelems - j;
        }

        result = intset_create(elems, nelems);
        pfree(elems);

        PG_FREE_IF_COPY(a, 0);
        PG_FREE_IF_COPY(b, 1);
        PG_RETURN_INTSET_P(result);
}



PG_FUNCTION_INFO_V1(intset_difference);
Datum
intset_difference(PG_FUNCTION_ARGS)
{

        IntSet  *a = PG_GETARG_INTSET_P(0);
        IntSet  *b = PG_GETARG_INTSET_P(1);
        int32 nelems = 0;
        int32 i = 0, j = 0;
        IntSet *result;
        int32 *elems;

        elems = (int32 *) palloc(sizeof(int32) * a->nelems);

        while (i < a->nelems && j < b->nelems)
        {
                if (a->elems[i] < b->elems[j])
                {
                        elems[nelems] = a->elems[i];
                        nelems++;
                        i++;
                } 
                else if (a->elems[i] == b->elems[j])
                {
                        i++;
                        j++;
                } 
                else 
                {
                        j++;
                }

        }

        /* Copy over remaining elements in a if the loop didn't iterate over all of them. */
        if (i < a->nelems)
        {
                memcpy(elems + nelems, a->elems + i, sizeof(int32) * (a->nelems - i));
                nelems += a->nelems - i;
        } 

        result = intset_create(elems, nelems);
        pfree(elems);


        PG_FREE_IF_COPY(a, 0);
        PG_FREE_IF_COPY(b, 1);
        PG_RETURN_INTSET_P(result);
}
