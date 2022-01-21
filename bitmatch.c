#include <stddef.h>
#include <stdint.h>
#include <limits.h>
#include <assert.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Change of the prime number used in hash function
   requires change of initializer below because they are related. */
#define PRIME_NUM 167U
/* 2 ** -1 mod 167.
   This initializer is chosen so we can compute 2 ** (nr_bits - 1) by
   applying shifting and modulo reduction operations in a loop. */
#define INIT_RNUM 84U

enum bitmatch_exit_codes {
    BM_FOUND        = 0,
    BM_NOT_FOUND    = 1,
    BM_OK           = 2,
    BM_USAGE_ERR    = 3,
    BM_INVALID_ARGS = 4,
    BM_NO_MEM       = 5,
    BM_IO_ERR       = 6,
};

static void print_usage(void)
{
    fprintf(stderr,
            "USAGE: bitmatch <pattern> <bits nr>\n"
            "where\n"
            "    <pattern> - sequence of hexadecimal digits\n"
            "    <bits nr> - non-negative number of "
            "significant bits in the bit pattern\n");
}

static void xfree(void *ptr);

/* Helper function which catches
   any memory allocation errors.
   Ensures only valid pointers are returned. */
static void *xmalloc(size_t size)
{
    void *ptr;

    if ((ptr = malloc(size)) == NULL) {
        fprintf(stderr,
                "Failed to allocate %zu bytes of memory\n",
                size);
        exit(BM_NO_MEM);
    }

    return ptr;
}

/* Helper function which ensures that
   memory is indeed reallocated.
   If underlying call fails, old memory is freed
   and process termination follows. */
static void *xrealloc(void *ptr, size_t size)
{
    void *new_ptr;

    if ((new_ptr = realloc(ptr, size)) == NULL) {
        fprintf(stderr,
                "Failed to re-allocate %zu bytes of memory\n",
                size);
        xfree(ptr);
        exit(BM_NO_MEM);
    }

    return new_ptr;
}

/* Bridge to free() std function.
   Used to complete eXtended memory allocation API. */
static void xfree(void *ptr)
{
    free(ptr);
}

/* Extracts @count bits starting at @offset.
   May traverse byte & word boundaries.
   In a byte, the most significant bit has the least offset. */
static unsigned int extract_bitfield(const unsigned char *buf,
                                     size_t offset,
                                     size_t count)
{
    unsigned int val = 0U;

    assert(0U < count && count <= 8U);

    while (count > 0U) {
        size_t nr_avail, nr_consumed, nr_left;
        unsigned int part, mask;

        /* How many bits can be consumed in the current byte? */
        nr_avail = 8U - (offset & 7U);
        /* And how many bits are we supposed to consume? */
        nr_consumed = count < nr_avail ? count : nr_avail;
        /* Amount of the least significant bits in the current byte
           which are next to consume. */
        nr_left = nr_avail - nr_consumed;

        part = buf[offset / 8U];
        mask = ~((-1U << nr_avail) | ~(-1U << nr_left));

        val = (val << nr_consumed) | ((part & mask) >> nr_left);

        count -= nr_consumed;
        offset += nr_consumed;
    }

    return val;
}

struct bit_pattern {
    /* Buffer holding particular bit pattern. */
    unsigned char *buf;
    /* Capacity of the allocated buffer. */
    size_t size;
    /* The amount of relevant bits in the buffer. */
    size_t nr_bits;
    /* Pre-computed hash value of the pattern. */
    unsigned int hash;
    /* Cancel the effect of top-most bit on hash value by adding this number to
       the current hash sum. */
    unsigned int rnum;
};

/* Unwrap command line arguments to binary data.
   Ensure sanity of the resulting values. */
static int get_pattern(char *hex_seq,
                       char *nr_bits_s,
                       struct bit_pattern *pat)
{
    struct bit_pattern lpat;
    unsigned char *next;
    size_t nr_bits;
    char *left;
    /* 0 - if current character of the hex sequence is an upper half
       of some byte;
       1 - otherwise. */
    int current_half = 0;

    left = NULL;
    errno = 0;
    nr_bits = strtoul(nr_bits_s, &left, 10);
    if (errno != 0) {
        perror("Failed to parse the number of bits");
        return BM_INVALID_ARGS;
    } else if (left == nr_bits_s) {
        fprintf(stderr,
                "Failed to parse the number of bits: "
                "No digits found\n");
        return BM_INVALID_ARGS;
    } else if (*left != '\0') {
        fprintf(stderr,
                "Failed to parse the number of bits: "
                "Extra characters at the end of the argument\n");
        return BM_INVALID_ARGS;
    } else if (nr_bits > SIZE_MAX - 7U) {
        /* This helps us to ensure no overflows happening later on */
        fprintf(stderr,
                "Failed to parse the number of bits: "
                "The number exceeds imposed limit\n");
        return BM_INVALID_ARGS;
    } else if (nr_bits == 0U) {
        /* Empty bit pattern matches any data */
        return BM_FOUND;
    }

    /* Do we have enough hex digits to satisfy bit requirement? */
    if ((nr_bits + 3U) / 4U > strlen(hex_seq)) {
        fprintf(stderr,
                "Failed to parse the bit sequence: "
                "Can\'t obtain %zu bits from the sequence\n",
                nr_bits);
        return BM_INVALID_ARGS;
    }

    memset(&lpat, 0, sizeof(lpat));
    lpat.rnum = INIT_RNUM;
    lpat.hash = 0U;
    lpat.nr_bits = nr_bits;
    lpat.size = (lpat.nr_bits + 7U) / 8U;
    next = lpat.buf = xmalloc(lpat.size);

    while (nr_bits > 0U) {
        size_t count;
        unsigned int val;

        val = *hex_seq++;

        if (val >= '0' && val <= '9')
            val -= '0';
        else if (val >= 'A' && val <= 'F')
            val -= 'A' - 10;
        else if (val >= 'a' && val <= 'f')
            val -= 'a' - 10;
        else {
            fprintf(stderr,
                    "Failed to parse the bit sequence: "
                    "Invalid character at the position %zu\n",
                    (size_t) (next - lpat.buf) * 2U + (size_t) current_half);
            xfree(lpat.buf);
            return BM_INVALID_ARGS;
        }

        count = nr_bits < 4U ? nr_bits : 4U;

        lpat.hash = ((lpat.hash << count) + (val >> (4U - count))) % PRIME_NUM;
        lpat.rnum = (lpat.rnum << count) % PRIME_NUM;

        if (!current_half)
            *next = val << 4U;
        else
            *next++ |= val;

        current_half = !current_half;
        nr_bits -= count;
    }

    lpat.rnum = PRIME_NUM - lpat.rnum;

    *pat = lpat;
    return BM_OK;
}

/* Reads the whole data from stdin to allocated buffer. */
static int consume_stdin(unsigned char **pbuf, size_t *pbufsz)
{
    unsigned char scratch_mem[1024], *buf = NULL;
    size_t bufsz = 0U;

    while (1) {
        ssize_t nr_read = 0, nr_all_read = 0;
        size_t new_bufsz, count = sizeof(scratch_mem);

        while (count > 0U) {
            errno = 0;
            nr_read = read(STDIN_FILENO,
                           scratch_mem + nr_all_read,
                           count);

            if (nr_read < 0 && errno == EINTR)
                continue;

            if (nr_read <= 0)
                break;

            if ((size_t) nr_read > count) {
                /* Treat this unlikely condition as out-of-range error.
                   Discard any possible data obtained from the last read. */
                errno = ERANGE;
                nr_read = -1;
                break;
            }

            count -= (size_t) nr_read;
            nr_all_read += nr_read;
        }

        if (nr_all_read == 0) {
            /* We don't expect any errors.
               Note: if we've managed to receive some data, discard any errors
               from the last read. Assume that previous reads give us valid data. */
            if (bufsz == 0U && nr_read == -1) {
                perror("I/O error");
                return BM_IO_ERR;
            }

            break;
        }

        new_bufsz = bufsz + (size_t) nr_all_read;
        if (new_bufsz <= bufsz) {
            fprintf(stderr,
                    "I/O error: "
                    "Overflow detected while re-allocating buffer\n");
            xfree(buf);
            return BM_IO_ERR;
        }

        buf = xrealloc(buf, new_bufsz);
        memcpy(buf + bufsz,
               scratch_mem,
               (size_t) nr_all_read);
        bufsz = new_bufsz;
    }

    *pbuf = buf;
    *pbufsz = bufsz;
    return BM_OK;
}

/* Tries to match pattern to bit substring starting
   at specific offset in memcmp-style. */
static int match(const struct bit_pattern *pat,
                 const unsigned char *buf,
                 size_t offset)
{
    size_t pat_offset, count;

    for (pat_offset = 0U;
         pat_offset < pat->nr_bits;
         offset += count, pat_offset += count) {
        size_t nr_remained;
        unsigned int b1, b2;

        nr_remained = pat->nr_bits - pat_offset;
        count = nr_remained < 8U ? nr_remained : 8U;

        b1 = extract_bitfield(buf,
                              offset,
                              count);
        b2 = extract_bitfield(pat->buf,
                              pat_offset,
                              count);

        if (b1 != b2)
            return BM_NOT_FOUND;
    }

    return BM_FOUND;
}

/* Locate the first occurrence of the pattern
   by using Rabin–Karp algorithm. Hashes are computed fast
   because we use rolling hash function.

   Let BkBk-1...B2B1B0 be the bit string.
   The hash function F is computed as follows:
     F = (Bk * (2 ** k) + ... + B2 * (2 ** 2) + B1 * 2 + B0) mod P
   where P is a prime number.
   In order to eliminate the most significant addend
   we use pre-computed value that is -(2 ** k) == P - (2 ** k) mod P.
   So if Bk == 0 we have nothing to do since the largest power
   is nullified. Else Bk == 1 and we add pre-computed value thus
   cancelling the effect of the largest exponent out.
*/
static int scan(const struct bit_pattern *pat,
                const unsigned char *buf,
                size_t bufsz)
{
    unsigned int hash = 0U;
    size_t offset, count;

    /* Compute hash value of the first K (=number of bits in the pattern)
       data bits. */
    for (offset = 0U;
         offset < pat->nr_bits;
         offset += count) {
        size_t nr_remained;

        nr_remained = pat->nr_bits - offset;
        count = nr_remained < 8U ? nr_remained : 8U;

        hash = ((hash << count) +
                extract_bitfield(buf, offset, count)) % PRIME_NUM;
    }

    for (bufsz *= 8U;
         offset < bufsz;
         offset++) {
        /* Try to match the current hash value. */
        if (hash == pat->hash &&
            match(pat, buf, offset - pat->nr_bits) == BM_FOUND)
            return BM_FOUND;

        /* Do we need to nullify the effect of the largest exponent
           on hash value? */
        if (extract_bitfield(buf, offset - pat->nr_bits, 1) == 1U)
            hash = (hash + pat->rnum) % PRIME_NUM;

        hash = ((hash << 1U) +
                extract_bitfield(buf, offset, 1)) % PRIME_NUM;
    }

    /* The last possible match. */
    if (hash == pat->hash &&
        match(pat, buf, offset - pat->nr_bits) == BM_FOUND)
        return BM_FOUND;

    return BM_NOT_FOUND;
}

int main(int argc, char *argv[])
{
    struct bit_pattern pat;
    unsigned char *buf;
    size_t bufsz;
    int ret_val;

    if (argc != 3) {
        print_usage();
        return BM_USAGE_ERR;
    }

    if ((ret_val = get_pattern(argv[1], argv[2], &pat)) != BM_OK) {
        return ret_val;
    }

    if ((ret_val = consume_stdin(&buf, &bufsz)) != BM_OK) {
        xfree(pat.buf);
        return ret_val;
    }

    if (bufsz > SIZE_MAX / 8U) {
        fprintf(stderr,
                "I/O error: "
                "Input buffer is too large\n");
        xfree(buf);
        xfree(pat.buf);
        return BM_IO_ERR;
    }

    /* Does scanning make sense? */
    if (bufsz * 8U >= pat.nr_bits)
        ret_val = scan(&pat, buf, bufsz);
    else
        ret_val = BM_NOT_FOUND;

    xfree(buf);
    xfree(pat.buf);

    return ret_val;
}
