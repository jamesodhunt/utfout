/*---------------------------------------------------------------------
 * Description:
 *
 * Simple program to echo arbitrary characters to stdout/stderr
 * without the need for shell redirections.
 *
 * Similar to echo(1) and printf(1); more featureful than the
 * former yet less powerful than the latter.
 *
 * Date: 18 January 2012
 *
 * Author: James Hunt <james.hunt@ubuntu.com>
 *
 * Bugs: This code is messy (*), partly since we're not using
 *       flex+bison due to the inherant difficulties of handling
 *       UTF-8/wide characters with those tools.
 *
 *       (*) In particular, generate_chars() is an abomination.
 *
 * License: GPLv3. See below...
 *---------------------------------------------------------------------
 *
 * Copyright © 2012 James Hunt <james.hunt@ubuntu.com>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *---------------------------------------------------------------------
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <signal.h>
#include <paths.h>
#include <locale.h>
#include <wchar.h>
#include <wctype.h>
#include <errno.h>
#include <langinfo.h>
#include <time.h>
#include <ctype.h>
#include <getopt.h>
#include <assert.h>
#include <sys/time.h>
#include <stdarg.h>
#include <libintl.h>

#define _(string) gettext (string)

#include "config.h"

/* character to emit for '\e' escape */
#define ESCAPE_CHAR       0x1b

#define PROGRAM_LICENSE   \
    "GPL-3.0+: GNU GPL version 3 or later <http://gnu.org/licenses/gpl.html>.\n" \
"This is free software: you are free to change and redistribute it.\n" \
"There is NO WARRANTY, to the extent permitted by law.\n"
#define PROGRAM_AUTHORS   "James Hunt <james.hunt@ubuntu.com>"

/* default prefix */
wchar_t            escape_prefix = L'\\';

/* file descriptor of tty we're connected to */
int                tty_fd = -1;

struct sigaction   act;
struct sigaction   oldact;

/* last string that has been emitted */
char              *last_str = NULL;

/* true if all escapes are to be disabled */
int                disable_escapes = 0;


/* prototypes */
void      usage                    (void);
int       open_terminal            (void);
void      handle_string            (int fd, const char *str, const char *delay,
                                    int separator_specified, int separator);
void      signal_handler           (int signum);
void      handle_sleep             (const char *str);
void      wait_for_intr            (void);
int       generate_chars           (const wchar_t *range, wchar_t **expanded,
                                    size_t *len, size_t *consumed);
int       get_hex_char             (const wchar_t *str, wchar_t *character);
int       simple_escape_to_literal (int value);
wchar_t   get_random_char          (void);

/**
 * get_oct_char:
 *
 * Convert octal value in @str to single wide character value @character.
 *
 * @str: wide string containing octal value,
 * @character: wide character output value.
 **/
#define get_oct_char(str, character) \
    get_base_char (str, character, 8)

/**
 * get_hex_char:
 *
 * Convert hex value in @str to single wide character value @character.
 *
 * @str: wide string containing hex value,
 * @character: wide character output value.
 **/
#define get_hex_char(str, character) \
    get_base_char (str, character, 16)

/**
 * is_octal:
 *
 * @wc: wide character to check.
 *
 * Determine if @wc is an octal character.
 *
 * Returns: TRUE if @wc is an octal character, else FALSE.
 **/
#define is_octal(wc) \
    (wc && \
     iswctype (wc, wctype("digit")) && wc >= L'0' && wc <= L'7')

/**
 * is_hex:
 *
 * @wc: wide character to check.
 *
 * Determine if @wc is an hex character.
 *
 * Returns: TRUE if @wc is an hex character, else FALSE.
 **/
#define is_hex(wc) \
    (wc && iswctype (wc, wctype("xdigit")))

/**
 * die:
 *
 * @fmt: printf-style format and arguments.
 *
 * Display formatted string and exit.
 **/
void
die (const char *fmt, ...)
{
    char     tag[] = "ERROR";
    va_list  ap;
    char     buffer[1024];
    wchar_t  wbuffer[1024];
    char    *p;
    size_t   len;
    size_t   wlen;
    int      ret;

    len = sizeof (buffer) / sizeof (wchar_t);

    buffer[len-1] = '\0';

    p = buffer;

    va_start (ap, fmt);

    ret = snprintf (p, len, "%s", tag);
    if (ret < 0)
        goto error;

    p += ret;

    ret = vsnprintf (p, len-ret, fmt, ap);
    if (ret < 0)
        goto error;

    va_end (ap);

    p += ret;

    ret = snprintf (p, len, "\n");
    if (ret < 0)
        goto error;

    p += ret;

    /* check for buffer overflow */
    assert (buffer[len-1] == '\0');

    /* convert MBS to wide-character string */
    wlen = mbsnrtowcs (wbuffer, (const char **)&p, len, len, NULL);
    if (wlen < 0)
        goto error;

    ret = fputws (wbuffer, stderr);

    if (ret < 0)
        goto error;

    exit (EXIT_FAILURE);

error:
    fwprintf (stderr, L"ERROR: failed to format error string\n");
    exit (EXIT_FAILURE);
}

/**
 * get_base_char:
 *
 * @str: string containing wide char that needs conversion,
 * @character: output value,
 * @base: numerical base.
 *
 * Convert string representation of number in @str to @character
 * in base @base.
 *
 * Returns: 0 on success, -1 on error.
 **/
int
get_base_char (const wchar_t  *str,
               wchar_t        *character,
               int             base)
{
    wchar_t  *endptr;
    size_t    tmp;

    assert (str);

    errno = 0;
    tmp = wcstoul (str, &endptr, base);
    if (errno || *endptr) {
        fwprintf (stderr,
                L"%s: %s\n",
                L"ERROR",
                L"failed to extract base %d character from string '%s'",
                base, str);
        return -1;
    }

    *character = (wchar_t)tmp;

    return 0;
}

/**
 * OUT_WCHAR:
 *
 * @fd: open file descriptor,
 * @wc: wide character to display.
 *
 * Convert wide character @wc back into multi-byte sequence and
 * write to file descriptor @fd.
 */
#define OUT_WCHAR(fd, wc, delay) \
{ \
    char    buffer[8]; \
    size_t  len; \
    \
    len = wcrtomb (buffer, wc, NULL); \
    if (len != (size_t)-1) { \
        ret = write (fd, buffer, len); \
        if (ret < 0) \
        die ("failed to display character %lx", wc); \
    } \
    \
    if (delay) \
    handle_sleep (delay); \
}

/**
 * usage:
 *
 * Display details to stdout of how this program can be used.
 **/
void
usage (void)
{
    printf (
            "Usage: %s [<options>]\n"
            "\n"
            "Description: Echo strings to specified output stream(s).\n"
            "\n"
            "Options:\n"
            "\n"
            "  -a, --intra-char=<char>    : Insert specified character between all\n"
            "                               output characters.\n"
            "  -b, --intra-pause=<delay>  : Pause between writing each character.\n"
            "  -e, --stderr               : Write subsequent strings to standard error\n"
            "                               (file descriptor %d).\n"
            "  -h, --help                 : This help text.\n"
            "  -i, --interpret            : Interpret escape characters.\n"
            "  -l, --literal              : Write literal strings only\n"
            "                               (disable escape characters)\n"
            "  -o, --stdout               : Write subsequent strings to standard output\n"
            "                               (file descriptor %d).\n"
            "  -p, --prefix=<prefix>      : Use <prefix> as escape prefix (default='%c')\n"
            "  -r, --repeat=<repeat>      : Repeat previous value <repeat> times.\n"
            "  -s, --sleep=<delay>        : Sleep for <delay> amount of time.\n"
            "  -t, --terminal             : Write subsequent strings directly to terminal.\n"
            "  -u, --file-descriptor=<fd> : Write to specified file descriptor.\n"
            "  -x, --exit=<num>           : Exit with value <num>.\n"
            "\n",
        PACKAGE_NAME,
        STDERR_FILENO,
        STDOUT_FILENO,
        escape_prefix);

    printf (
            "Escape Characters:\n"
            "\n"
            "  'a'         - alert (bell)\n"
            "  'b'         - backspace\n"
            "  'c'         - no further output\n"
            "  'e'         - escape character\n"
            "  'f'         - form feed\n"
            "  'g'         - generate pseudo-random printable character\n"
            "  'n'         - newline\n"
            "  'oNNN'      - 1-byte octal character (1-3 digits)\n"
            "  'r'         - carriage return\n"
            "  't'         - horizontal tab\n"
            "  'uNNNN'     - 4-byte unicode/UTF-8 character (4 hex digits)\n"
            "  'UNNNNNNNN' - 4-byte unicode/UTF-8 character (8 hex digits)\n"
            "  'v'         - vertical tab\n"
            "  'xNN'       - 1-byte hexadecimal character (1-2 digits)\n"
            "\n"
           );

    printf (
            "Extended Escape Characters:\n"
            "\n"
            "  '\\{N..N}'                 - Specify a range by two 1-chacter literal\n"
            "                              characters.\n"
            "  '\\{xNN..xNN}'             - Specify a range by two 2-digit hex values.\n"
            "  '\\{oNNN..oNNN}'           - Specify a range by two 3-digit octal values.\n"
            "  '\\{uNNNN..uNNNN}'         - Specify a range by two 4-digit unicode values.\n"
            "  '\\{UNNNNNNNN..UNNNNNNNN}' - Specify a range by two 8-digit unicode values.\n"
            "\n");

    printf (
            "Notes:\n"
            "\n"
            "  - Arguments are processed in order.\n"
            "  - With the exception of '-x', arguments may be repeated any number of times.\n"
            "  - If <str> is \"\", a nul byte will be displayed.\n"
            "  - If <repeat> is '-1', repeat forever.\n"
            "  - Replace the 'Z' in the range formats above with the appropriate characters.\n"
            "  - Ranges can be either ascending or descending.\n"
            "  - <delay> can take the following forms where <num> is a positive integer:\n"
            "\n"
            "      <num>ns : nano-seconds (1/1,000,000,000 second)\n"
            "      <num>us : micro-seconds (1/1,000,000 second)\n"
            "      <num>ms : milli-seconds (1/1,000 second)\n"
            "      <num>cs : centi-seconds (1/100 second)\n"
            "      <num>ds : deci-seconds (1/10 second)\n"
            "      <num>s  : seconds\n"
            "      <num>m  : minutes\n"
            "      <num>h  : hours\n"
            "      <num>h  : days\n"
            "      <num>   : seconds\n"
            "\n"
            "    If <num> is -1, wait until any signal is received.\n"
            "    if signal is SIGNUM continue, else exit immediately.\n"
            "  - Generated printable random characters will not display\n"
            "    unless you are using an appropriate font.\n"
            "\n");
}


/**
 * open_terminal:
 *
 * Open terminal device.
 *
 * Returns: -1 on error, or fd associated with terminal.
 **/
int
open_terminal (void)
{
    return open (_PATH_TTY, O_RDWR);
}

/**
 * handle_string:
 *
 * @fd: file descriptor to write output to,
 * @str: string to process,
 * @delay: inter-chracter delay,
 * @separator_specified: TRUE if a separator value has been specified,
 * @separator: separator to use.
 *
 * Process string @str and write results to @fd, delaying output by
 * @delay between each character emitted. Characters will be
 * interspersed by @separator if @separator_specified is set.
 *
 * Note that @separator_specified is required since a null byte
 * separtor is valid.
 **/
void
handle_string (int          fd,
        const char  *str,
        const char  *delay,
        int          separator_specified,
        int          separator)
{
    wchar_t      c;
    size_t       i;
    ssize_t      ret;

    /* number of characters consumed */
    size_t       consumed = 0;

    /* XXX: _number_ of wide characters, *NOT* length in bytes!! */
    size_t       len;

    wchar_t     *wstr = NULL;
    const char  *p;
    wchar_t     *wc;

    /* If -1, not in escape mode, else set to the position in the string the
     * escape char seen at.
     */
    int          escape = -1;

    /* special case nul string */
    if (*str == '\0') {
        OUT_WCHAR (fd, L'\0', delay);
        return;
    }

    /* convert multi-byte (UTF-8) string into wide
     * character string for reasier internal handling.
     */
    len = mbsrtowcs (NULL, &str, 0, NULL);

    if (len == (size_t)-1) {
        fwprintf (stderr,
                L"%ls: %ls\n",
                L"ERROR",
                L"failed to determine length of wide string");
        exit (EXIT_FAILURE);
    }

    /* include space for terminator */
    wstr = calloc (len + 1, sizeof (wchar_t));
    if (! wstr) {
        fwprintf (stderr,
                L"%s: %s: %s\n",
                L"ERROR",
                L"Failed to allocate space for wide string",
                str);
        exit (EXIT_FAILURE);
    }

    p = str;

    /* mbsrtowcs () doesn't include the terminator
     * in the return value
     */
    assert (mbsrtowcs (wstr, &p, len, NULL) == len);

    if (len == (size_t)-1) {
        fwprintf (stderr,
                L"%s: %s\n",
                L"ERROR",
                L"Failed to convert string to wide string",
                str);
        exit (EXIT_FAILURE);
    }

    /* ensure it's terminated */
    wstr[len] = L'\0';

    for (i=0; i < len; ++i) {

        c = wstr[i];

        if (disable_escapes)
            goto out;

        if (c == escape_prefix) {
            /* Record position of escape character.
             *
             * This will collapse any number of contiguous escape chars
             * into a single one.
             */
            escape = i;
            continue;
        }

        /* Consider the next character after the escape character */
        if (escape != -1 && (escape+1) == i) {
            switch (c) {

                case L'o': /* 1-3 byte octal value */
                    {
                        wchar_t  buffer[3+1];
                        size_t   offset = i+1;

                        consumed = 0;

                        wmemset (buffer, L'\0', sizeof (buffer) / sizeof (wchar_t));

                        /* handle 1st octal char */
                        if (is_octal (wstr[offset])) {
                            buffer[0] = wstr[offset];
                            consumed++;
                            offset++;
                        }

                        /* handle optional 2nd octal char */
                        if (is_octal (wstr[offset])) {
                            buffer[1] = wstr[offset];
                            consumed++;
                            offset++;
                        }

                        /* handle optional 3rd octal char */
                        if (is_octal (wstr[offset])) {
                            buffer[2] = wstr[offset];
                            consumed++;
                            offset++;
                        }

                        if (get_oct_char (buffer, &c) < 0) {
                            escape = -1;
                            goto not_an_escape;
                        }

                        i += consumed;

                        escape = -1;
                    }
                    break;

                case L'u': /* 2-byte unicode/UTF-8 character */
                case L'U': /* 4-byte unicode/UTF-8 character */
                    {
                        wchar_t  buffer[8+1];
                        size_t   offset = i+1;
                        int      b;

                        consumed = 0;

                        wmemset (buffer, L'\0', sizeof (buffer) / sizeof (wchar_t));

                        for (b = 0; b < 8; b++) {
                            if (is_hex (wstr[offset])) {
                                buffer[b] = wstr[offset];
                                consumed++;
                                offset++;
                                if (c == L'u' && b == 3)
                                    break;
                            } else
                                break;

                        }

                        if (get_hex_char (buffer, &c) < 0) {
                            escape = -1;
                            goto not_an_escape;
                        }

                        i += consumed;

                        escape = -1;
                    }

                    break;

                case L'x': /* 1 or 2-byte hexadecimal value */
                    {
                        wchar_t  buffer[2+1];
                        size_t   offset = i+1;

                        consumed = 0;

                        wmemset (buffer, L'\0', sizeof (buffer) / sizeof (wchar_t));

                        /* handle first hex char */
                        if (is_hex (wstr[offset])) {
                            buffer[0] = wstr[offset];
                            consumed++;
                            offset++;
                        }

                        /* handle optional 2nd hex char */
                        if (is_hex (wstr[offset])) {
                            buffer[1] = wstr[offset];
                            consumed++;
                            offset++;
                        }

                        if (get_hex_char (buffer, &c) < 0) {
                            escape = -1;
                            goto not_an_escape;
                        }

                        i += consumed;

                        escape = -1;
                    }
                    break;

                case L'{': /* range */
                    {
                        wchar_t  *expanded_range;
                        size_t    expanded_len;

                        if (generate_chars (wstr+i, &expanded_range,
                                    &expanded_len, &consumed) < 0)
                            goto not_an_escape;

                        wc = expanded_range;
                        while (expanded_len) {
                            OUT_WCHAR (fd, *wc, delay);
                            if (separator_specified && expanded_len > 1)
                                OUT_WCHAR (fd, separator, 0);
                            wc++;
                            expanded_len--;
                        }
                        free (expanded_range);

                        escape = -1;

                        /* jump over the already-handled pattern */
                        i += (consumed - 1);

                        /* don't output any chars - we've already
                         * handled that!
                         */
                        continue;
                    }
                    break;

not_an_escape:
                default:
                    {
                        int tmp;

                        tmp = simple_escape_to_literal (c);

                        if (tmp == -1) {
                            /* invalid escape, so display escape (and
                             * subsequent char) uninterpreted.
                             */
                            OUT_WCHAR (fd, escape_prefix, delay);
                        } else {
                            c = tmp;

                            escape = -1;
                            break;
                        }

                        if (c != escape_prefix) {
                            OUT_WCHAR (fd, c, delay);
                        }
                    }
            }
        }

out:
        OUT_WCHAR(fd, c, delay);
        if (separator_specified && len > 1)
            OUT_WCHAR (fd, separator, 0);
    }

    if (wstr)
        free (wstr);
}


/**
 * signal_handler:
 *
 * @signum: signal number passed to this function.
 *
 * Handle interrupt signal by simply returning.
 **/
void
signal_handler (int signum)
{
    switch (signum)
    {
        case SIGINT:
            return;
            break;

        default:
            exit (EXIT_SUCCESS);
            break;
    }
}

/**
 * handle_sleep:
 *
 * @str: string representing time to sleep for.
 *
 * Sleep for time specified by @str.
 *
 * Recognised formats:
 *
 *     <num>ns : nano-seconds (1/1,000,000,000 second)
 *     <num>us : micro-seconds (1/1,000,000 second)
 *     <num>ms : milli-seconds (1/1,000 second)
 *     <num>cs : centi-seconds (1/100 second)
 *     <num>ds : deci-seconds (1/10 second)
 *     <num>s  : seconds
 *     <num>m  : minutes
 *     <num>h  : hours
 *     <num>h  : days
 *     <num>   : seconds
 *
 * if <num> is -1, sleep until any signal is received.
 **/
void
handle_sleep (const char *str)
{
    long              secs;
    unsigned int      sleep_time;
    size_t            len;
    unsigned int      multiplier = 0;
    struct timespec   ts;
    struct timespec   rem;
    int               ret;
    const char       *posn;

    len = strlen (str);
    secs = atol (str);

    posn = &str[len-2];

    if (strstr (str, "cs") == posn) {
        /* Centi-seconds:
         *
         * 10 ^ (-2)
         *
         * 1/100th of a second.
         * (1 hundredth of a second).
         */
        if (secs > 99)
            secs = 99;

        ts.tv_nsec = secs * 10000000;

    } else if (strstr (str, "ds") == posn) {
        /* Deci-seconds:
         *
         * 10 ^ (-1)
         *
         * 1/10th of a second.
         * (1 tenth of a second).
         */
        if (secs > 9)
            secs = 9;

        ts.tv_nsec = secs * 100000000;

    } else if (strstr (str, "ms") == posn) {
        /* Milli-seconds:
         *
         * 10 ^ (-3)
         *
         * 1/1000th of a second.
         * (1 thousandth of a second).
         */
        if (secs > 999)
            secs = 999;

        ts.tv_nsec = secs * 1000000;

    } else if (strstr (str, "ns") == posn) {
        /* Nano-seconds:
         *
         * 10 ^ (-9)
         *
         * 1/1,000,000,000 of a second
         * (1 billionth of a second)
         */
        if (secs > 999999999)
            secs = 999999999;
        ts.tv_nsec = secs;

    } else if (strstr (str, "us") == posn) {
        /* Micro-seconds:
         *
         * 10 ^ (-6)
         *
         * 1/1,000,000th of a second
         * (1 millionth of a second).
         */
        if (secs > 999999)
            secs = 999999;

        ts.tv_nsec = secs * 1000;

    } else {
        switch (str[len-1])
        {
            case 'd':
                /* Days */
                multiplier = (60 * 60 * 24);
                break;

            case 'h':
                /* Hours */
                multiplier = (60 * 60);
                break;

            case 'm':
                /* Minutes */
                multiplier = 60;
                break;

            case 's':
                /* Seconds */
                multiplier = 1;
                break;

            default:
                /* No suffix, so must be seconds */
                multiplier = 1;
                break;
        }
    }

    if (secs == -1) {
        wait_for_intr ();
        return;
    }

    if (multiplier) {
        sleep_time = (unsigned int)secs * multiplier;
        sleep (sleep_time);
        return;
    }

    ts.tv_sec = 0;

    do {
        ret = nanosleep (&ts, &rem);
        if (ret) {
            ts.tv_sec = rem.tv_sec;
            ts.tv_nsec = rem.tv_nsec;

            rem.tv_sec = 0;
            rem.tv_nsec = 0;
        }
    } while (ret);
}

/**
 * wait_for_intr:
 *
 * Wait indefinately for an interrupt signal.
 **/
void
wait_for_intr (void)
{
    int ret;

    /* save any old signal handlers */
    ret = sigaction (SIGINT, NULL, &oldact);

    if (ret != 0) {
        fwprintf (stderr,
                L"%s: %s\n",
                L"ERROR",
                L"Unable to save old signal handler");
        exit (EXIT_FAILURE);
    }

    /* specify handler */
    act.sa_handler = signal_handler;

    /* register our own handler */
    ret = sigaction (SIGINT, &act, NULL);
    if (ret != 0) {
        fwprintf (stderr,
                L"%s: %s\n",
                L"ERROR",
                L"Unable to set new signal handler");
        exit (EXIT_FAILURE);
    }

    pause ();
}

/**
 * generate_chars:
 *
 * @range: range of characters to generate,
 * @expanded: newly-allocated expanded range of wide characters,
 * @len: length of @expanded,
 * @consumed: number of wide chars in range that have been processed by
 * this call.
 *
 * Returns: 0 on success, else -1.
 **/
int
generate_chars (const wchar_t  *range,
        wchar_t       **expanded,
        size_t         *len,
        size_t         *consumed)
{
    wchar_t   start;
    wchar_t   end;
    wchar_t   initial_value;
    size_t    l;
    int       direction;
    wchar_t  *wc;

    assert (range);
    assert (expanded);
    assert (len);
    assert (consumed);

    wctype_t digit;
    wctype_t xdigit;

    /* large enough to hold 'FFFFFFFF'-style pattern, without leading '\U',
     * but with a terminator.
     */
#define WCHAR_BUFSIZE (8+1)

    wchar_t  buffer[WCHAR_BUFSIZE];

    digit  = wctype ("digit");
    xdigit = wctype ("xdigit");

    /* range must _start_ with one of the following patterns:
     *
     *      L"{\uFFFFFFFF..\uFFFFFFFF}" (4 unicode characters)
     *      L"{\uFFFF..\uFFFF}"         (2 unicode characters)
     *      L"{\o777..\o777}"           (3 octal characters)
     *      L"{\xFF..\xFF}"             (2 hex characters)
     *      L"{?..?}"                   (2 literal characters)
     *
     * ...but note that it may contain further characters
     * that will be handled by other parts of the code.
     */
    if ((wcslen (range) >= 24
                && range[0] == L'{'
                && range[1] == L'\\'
                && range[2] == L'U'
                && iswctype (range[3], xdigit)
                && iswctype (range[4], xdigit)
                && iswctype (range[5], xdigit)
                && iswctype (range[6], xdigit)
                && iswctype (range[7], xdigit)
                && iswctype (range[8], xdigit)
                && iswctype (range[9], xdigit)
                && iswctype (range[10], xdigit)
                && range[11] == L'.'
                && range[12] == L'.'
                && range[13] == L'\\'
                && range[14] == L'U'
                && iswctype (range[15], xdigit)
                && iswctype (range[16], xdigit)
                && iswctype (range[17], xdigit)
                && iswctype (range[18], xdigit)
                && iswctype (range[19], xdigit)
                && iswctype (range[20], xdigit)
                && iswctype (range[21], xdigit)
                && iswctype (range[22], xdigit)
                && range[23] == L'}')) {

                    /* 4 unicode characters */

                    buffer[0] = range[3];
                    buffer[1] = range[4];
                    buffer[2] = range[5];
                    buffer[3] = range[6];
                    buffer[4] = range[7];
                    buffer[5] = range[8];
                    buffer[6] = range[9];
                    buffer[7] = range[10];
                    buffer[8] = L'\0';

                    if (get_hex_char (buffer, &start) < 0)
                        goto error;

                    buffer[0] = range[15];
                    buffer[1] = range[16];
                    buffer[2] = range[17];
                    buffer[3] = range[18];
                    buffer[4] = range[19];
                    buffer[5] = range[20];
                    buffer[6] = range[21];
                    buffer[7] = range[22];
                    buffer[8] = L'\0';

                    if (get_hex_char (buffer, &end) < 0)
                        goto error;

                    *consumed = 24;
                } else if ((wcslen (range) >= 16
                            && range[0] == L'{'
                            && range[1] == L'\\'
                            && range[2] == L'u'
                            && iswctype (range[3], xdigit)
                            && iswctype (range[4], xdigit)
                            && iswctype (range[5], xdigit)
                            && iswctype (range[6], xdigit)
                            && range[7] == L'.'
                            && range[8] == L'.'
                            && range[9] == L'\\'
                            && range[10] == L'u'
                            && iswctype (range[11], xdigit)
                            && iswctype (range[12], xdigit)
                            && iswctype (range[13], xdigit)
                            && iswctype (range[14], xdigit)
                            && range[15] == L'}')) {

                    /* 2 unicode characters */

                    buffer[0] = range[3];
                    buffer[1] = range[4];
                    buffer[2] = range[5];
                    buffer[3] = range[6];
                    buffer[4] = L'\0';

                    if (get_hex_char (buffer, &start) < 0)
                        goto error;

                    buffer[0] = range[11];
                    buffer[1] = range[12];
                    buffer[2] = range[13];
                    buffer[3] = range[14];
                    buffer[4] = L'\0';

                    if (get_hex_char (buffer, &end) < 0)
                        goto error;

                    *consumed = 16;

                } else if ((wcslen (range) >= 14
                            && range[0] == L'{'
                            && range[1] == L'\\'
                            && range[2] == L'o'
                            && iswctype (range[3], digit)
                            && iswctype (range[4], digit)
                            && iswctype (range[5], digit)
                            && range[6] == L'.'
                            && range[7] == L'.'
                            && range[8] == L'\\'
                            && range[9] == L'o'
                            && iswctype (range[10], digit)
                            && iswctype (range[11], digit)
                            && iswctype (range[12], digit)
                            && range[13] == L'}')) {

                    /* 2 octal characters */

                    buffer[0] = range[3];
                    buffer[1] = range[4];
                    buffer[2] = range[5];
                    buffer[3] = L'\0';

                    if (get_oct_char (buffer, &start) < 0)
                        goto error;

                    buffer[0] = range[10];
                    buffer[1] = range[11];
                    buffer[2] = range[12];
                    buffer[3] = L'\0';

                    if (get_oct_char (buffer, &end) < 0)
                        goto error;

                    *consumed = 14;

                } else if ((wcslen (range) >= 12
                            && range[0] == L'{'
                            && range[1] == L'\\'
                            && range[2] == L'x'
                            && iswctype (range[3], xdigit)
                            && iswctype (range[4], xdigit)
                            && range[5] == L'.'
                            && range[6] == L'.'
                            && range[7] == L'\\'
                            && range[8] == L'x'
                            && iswctype (range[9], xdigit)
                            && iswctype (range[10], xdigit)
                            && range[11] == L'}')) {

                    /* 2 hex characters */

                    buffer[0] = range[3];
                    buffer[1] = range[4];
                    buffer[2] = L'\0';

                    if (get_hex_char (buffer, &start) < 0)
                        goto error;

                    buffer[0] = range[9];
                    buffer[1] = range[10];
                    buffer[2] = L'\0';

                    if (get_hex_char (buffer, &end) < 0)
                        goto error;

                    *consumed = 12;

                } else if ((wcslen (range) >= 6
                            && range[0] == L'{'
                            && range[2] == L'.'
                            && range[3] == L'.'
                            && range[5] == L'}')) {

                    /* 2 literal characters */

                    start = range[1];
                    end   = range[4];
                    *consumed = 6;

                } else {
                    goto error;
                }

    initial_value = start;

    direction = (start < end) ? +1 : -1;

    /* calculate number of characters in sequence */
    l = (start < end) ? end - start : start - end;

    /* include space for terminator */
    l++;

    *expanded = calloc (l, sizeof (wchar_t));
    if (! *expanded)
        goto error;

    wc = *expanded;

    while (1) {
        *wc = initial_value;
        wc++;

        if (initial_value == end)
            break;

        initial_value += direction;
    }

    *len = l;

    return 0;

error:
    *len = 0;
    *consumed = 0;
    return -1;
}


/**
 * simple_escape_to_literal:
 *
 * @value: value to check.
 *
 * Convert specified character @value (which is assumed to have
 * already been parsed as part of an escape sequence) into
 * its literal value.
 *
 * Returns: literal value, or -1 on error (denoting @value is not
 * actually a valid escape character).
 **/
int
simple_escape_to_literal (int value)
{
    int c = -1;

    switch (value) {

        case L'0': /* nul */
            c = L'\0';
            break;

        case L'a': /* alert (BEL) */
            c = L'\a';
            break;

        case L'b': /* backspace */
            c = L'\b';
            break;

        case L'c': /* no further output */
            exit (EXIT_SUCCESS);
            break;

        case L'e': /* emit escape character */
            c = ESCAPE_CHAR;
            break;

        case L'f': /* formfeed */
            c = L'\f';
            break;

        case L'g': /* generate a random char */
            c = (int)get_random_char ();
            break;

        case L'n': /* newline */
            c = L'\n';
            break;

        case L'r': /* carriage return */
            c = L'\r';
            break;

        case 't': /* horizontal tab */
            c = L'\t';
            break;

        case L'v': /* vertical tab */
            c = L'\v';
            break;
    }

    return c;
}

int
main (int argc, char *argv[])
{
    int    last_fd = STDOUT_FILENO;
    int    option;
    int    long_index;
    int    repeat = 0;
    char  *intra_char_delay = NULL;
    int    separator = '\0';
    int    separator_specified = 0;

    if (! setlocale (LC_ALL, ""))
        die ("Could not set locale");

    struct option long_options[] = {
        {"exit"            , required_argument , 0, 'x'},
        {"file-descriptor" , required_argument , 0, 'u'},
        {"help"            , no_argument       , 0, 'h'},
        {"interpret"       , required_argument , 0, 'i'},
        {"intra-char"      , required_argument , 0, 'a'},
        {"intra-pause"     , required_argument , 0, 'b'},
        {"literal"         , no_argument       , 0, 'l'},
        {"prefix"          , required_argument , 0, 'p'},
        {"repeat"          , required_argument , 0, 'r'},
        {"sleep"           , required_argument , 0, 's'},
        {"stderr"          , required_argument , 0, 'e'},
        {"stdout"          , required_argument , 0, 'o'},
        {"terminal"        , no_argument       , 0, 't'},
        {"version"         , no_argument       , 0, 'v'},

        /* terminator */
        {NULL, 0, 0, 0}
    };

    while ((option = getopt_long (argc, argv,
                    "-a:b:ehilop:r:s:tu:U:x:",
                    long_options, &long_index)) != -1) {
        switch (option)
        {
            /* a non-option, in other words a string */
            case 1:
                if (last_str)
                    free (last_str);
                last_str = strdup (optarg);
                handle_string (last_fd, last_str, intra_char_delay,
                        separator_specified, separator);
                break;

            case 'a':
                {
                    int tmp;

                    separator_specified = 1;

                    if (*optarg == escape_prefix) {
                        /* expand separator if possible */
                        tmp = simple_escape_to_literal (*(optarg+1));

                        separator = (tmp == -1 ? *optarg : tmp);
                    } else {
                        if (! *optarg)
                            /* user specified "-a ''" to cancel
                             * separator.
                             */
                            separator_specified = 0;
                        else
                            separator = *optarg;
                    }
                }
                break;

            case 'b':
                intra_char_delay = optarg;
                break;

            case 'e':
                last_fd = STDERR_FILENO;;
                break;

            case 'h':
                usage ();
                exit (EXIT_SUCCESS);

            case 'i':
                disable_escapes = 0;
                break;

            case 'l':
                disable_escapes = 1;
                break;

            case 'o':
                last_fd = STDOUT_FILENO;
                break;

            case 'p':
                escape_prefix = optarg[0];
                break;

            case 'r':
                if (! last_str)
                    break;

                repeat = atoi (optarg);

                while (1) {
                    handle_string (last_fd, last_str, intra_char_delay,
                            separator_specified, separator);

                    if (repeat != -1) {
                        repeat--;
                        if (! repeat)
                            break;
                    }
                }
                break;

            case 's':
                handle_sleep (optarg);
                break;

            case 't':
                tty_fd = open_terminal ();
                if (tty_fd < 0)
                    die ("failed to open terminal");
                last_fd = tty_fd;
                break;

            case 'u':
                last_fd = atoi (optarg);
                break;

            case 'v':
                wprintf (L"%s %s: %s\n", PACKAGE_NAME, _("version"), PACKAGE_VERSION);
                wprintf (L"%s: %s\n", _("License"), PROGRAM_LICENSE);
                wprintf (L"%s: %s\n", _("Written by"), PROGRAM_AUTHORS);
                exit (EXIT_SUCCESS);
                break;

            case 'x':
                exit (atoi (optarg));
                break;
        }
    }

    if (last_str)
        free (last_str);

    exit (EXIT_SUCCESS);
}

/**
 * generate_char:
 *
 * Generate a pseudo-random printable character.
 *
 * Returns: generated character.
 **/
wchar_t
get_random_char (void)
{
    wchar_t    wc;
    static int seeded = 0;

    if (! seeded) {
        struct timeval  tv;
        unsigned int    seed;

        gettimeofday (&tv, NULL);

        /* courtesy of bash */
        seed = (tv.tv_sec ^ tv.tv_usec ^ getpid ());

        srand (seed);
        seeded = 1;
    }

    while (1) {
        wc = (wchar_t)rand ();

        /* If the random character is not printable, bit-shift in the
         * home that the result might be.
         *
         * This significantly speeds up this function.
         */
        while (wc) {
            if (iswctype (wc, wctype ("print")))
                return wc;
            wc >>= 1;
        }
    }
}
