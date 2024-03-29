/**
 * The contents of this file are subject to the terms of the Common Development and
 * Distribution License (the License). You may not use this file except in compliance with the
 * License.
 *
 * You can obtain a copy of the License at legal/CDDLv1.0.txt. See the License for the
 * specific language governing permission and limitations under the License.
 *
 * When distributing Covered Software, include this CDDL Header Notice in each file and include
 * the License file at legal/CDDLv1.0.txt. If applicable, add the following below the CDDL
 * Header, with the fields enclosed by brackets [] replaced by your own identifying
 * information: "Portions copyright [year] [name of copyright owner]".
 *
 * Copyright 2014 - 2016 ForgeRock AS.
 */
/**
 * Portions Copyrighted 2022 OGIS-RI Co., Ltd.
 */

#include "platform.h"
#include "am.h"
#include "utility.h"
#include "error.h"
#include "pcre.h"
#include "zlib.h"  
#include "list.h" 
#include "thread.h"

#define AM_STRERROR_GEN(name, msg) case AM_ ## name: return msg;

const char *am_strerror(int err) {
    switch (err) {
            AM_ERRNO_MAP(AM_STRERROR_GEN)
        default:
            return "unknown system error";
    }
}
#undef AM_STRERROR_GEN

const char *request_method_str[] = {
    "UNKNOWN",
    "GET",
    "POST",
    "HEAD",
    "PUT",
    "DELETE",
    "TRACE",
    "OPTIONS",
    "CONNECT",
    "COPY",
    "INVALID",
    "LOCK",
    "UNLOCK",
    "MKCOL",
    "MOVE",
    "PATCH",
    "PROPFIND",
    "PROPPATCH",
    "VERSION_CONTROL",
    "CHECKOUT",
    "UNCHECKOUT",
    "CHECKIN",
    "UPDATE",
    "LABEL",
    "REPORT",
    "MKWORKSPACE",
    "MKACTIVITY",
    "BASELINE_CONTROL",
    "MERGE",
    "CONFIG",
    "ENABLE-APP",
    "DISABLE-APP",
    "STOP-APP",
    "STOP-APP-RSP",
    "REMOVE-APP",
    "STATUS",
    "STATUS-RSP",
    "INFO",
    "INFO-RSP",
    "DUMP",
    "DUMP-RSP",
    "PING",
    "PING-RSP"
};

#define AM_XSTR(s) AM_STR(s)
#define AM_STR(s) #s

#define URI_HTTP "%"AM_XSTR(AM_PROTO_SIZE)"[HTPShtps]"
#define URI_HOST "%"AM_XSTR(AM_HOST_SIZE)"[-_.abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789]"
#define URI_PORT "%6d"
#define URI_PATH "%"AM_XSTR(AM_URI_SIZE)"s"
#define HD1 URI_HTTP "://" URI_HOST ":" URI_PORT "/" URI_PATH
#define HD2 URI_HTTP "://" URI_HOST "/" URI_PATH
#define HD3 URI_HTTP "://" URI_HOST ":" URI_PORT
#define HD4 URI_HTTP "://" URI_HOST

struct query_attribute {
    char *key;
    char *key_value;
};

enum {
    AM_TIMER_INACTIVE = 0,
    AM_TIMER_ACTIVE = 1 << 0,
    AM_TIMER_PAUSED = 1 << 1
};

#define AM_TIMER_USEC_PER_SEC 1000000

#ifdef _WIN32 

struct dirent {
    long d_ino;
    off_t d_off;
    unsigned short d_reclen;
    unsigned char d_type;
    char d_name[AM_URI_SIZE + 1];
};

typedef struct {
    intptr_t handle;
    short offset;
    short finished;
    struct _finddata_t fileinfo;
    char *dir;
    struct dirent dent;
} DIR;

#endif

static am_timer_t am_timer_s = {0, 0, 0, 0};

static const char *hex_chars = "0123456789ABCDEF";

static const unsigned char base64_table[64] =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

static struct http_status http_status_list[] = {
#define HTTP_STATUS_CODE(c) c, AM_XSTR(c)
    {HTTP_STATUS_CODE(100), "Continue"},
    {HTTP_STATUS_CODE(101), "Switching Protocols"},
    {HTTP_STATUS_CODE(102), "Processing"},
    {HTTP_STATUS_CODE(200), "OK"},
    {HTTP_STATUS_CODE(201), "Created"},
    {HTTP_STATUS_CODE(202), "Accepted"},
    {HTTP_STATUS_CODE(203), "Non-Authoritative Information"},
    {HTTP_STATUS_CODE(204), "No Content"},
    {HTTP_STATUS_CODE(205), "Reset Content"},
    {HTTP_STATUS_CODE(206), "Partial Content"},
    {HTTP_STATUS_CODE(207), "Multi-Status"},
    {HTTP_STATUS_CODE(300), "Multiple Choices"},
    {HTTP_STATUS_CODE(301), "Moved Permanently"},
    {HTTP_STATUS_CODE(302), "Found"},
    {HTTP_STATUS_CODE(303), "See Other"},
    {HTTP_STATUS_CODE(304), "Not Modified"},
    {HTTP_STATUS_CODE(305), "Use Proxy"},
    {HTTP_STATUS_CODE(307), "Temporary Redirect"},
    {HTTP_STATUS_CODE(400), "Bad Request"},
    {HTTP_STATUS_CODE(401), "Unauthorized"},
    {HTTP_STATUS_CODE(402), "Payment Required"},
    {HTTP_STATUS_CODE(403), "Forbidden"},
    {HTTP_STATUS_CODE(404), "Not Found"},
    {HTTP_STATUS_CODE(405), "Method Not Allowed"},
    {HTTP_STATUS_CODE(406), "Not Acceptable"},
    {HTTP_STATUS_CODE(407), "Proxy Authentication Required"},
    {HTTP_STATUS_CODE(408), "Request Time-out"},
    {HTTP_STATUS_CODE(409), "Conflict"},
    {HTTP_STATUS_CODE(410), "Gone"},
    {HTTP_STATUS_CODE(411), "Length Required"},
    {HTTP_STATUS_CODE(412), "Precondition Failed"},
    {HTTP_STATUS_CODE(413), "Request Entity Too Large"},
    {HTTP_STATUS_CODE(414), "Request-URI Too Large"},
    {HTTP_STATUS_CODE(415), "Unsupported Media Type"},
    {HTTP_STATUS_CODE(416), "Requested range not satisfiable"},
    {HTTP_STATUS_CODE(417), "Expectation Failed"},
    {HTTP_STATUS_CODE(422), "Unprocessable Entity"},
    {HTTP_STATUS_CODE(423), "Locked"},
    {HTTP_STATUS_CODE(424), "Failed Dependency"},
    {HTTP_STATUS_CODE(426), "Upgrade Required"},
    {HTTP_STATUS_CODE(500), "Internal Server Error"},
    {HTTP_STATUS_CODE(501), "Not Implemented"},
    {HTTP_STATUS_CODE(502), "Bad Gateway"},
    {HTTP_STATUS_CODE(503), "Service Unavailable"},
    {HTTP_STATUS_CODE(504), "Gateway Time-out"},
    {HTTP_STATUS_CODE(505), "HTTP Version not supported"},
    {HTTP_STATUS_CODE(506), "Variant Also Negotiates"},
    {HTTP_STATUS_CODE(507), "Insufficient Storage"},
    {HTTP_STATUS_CODE(510), "HTTP Version not supported"}
#undef HTTP_STATUS_CODE
};

am_bool_t is_http_status(int status) {
    int i;
    for (i = 0; i < ARRAY_SIZE(http_status_list); i++) {
        struct http_status *s = &http_status_list[i];
        if (s->code == status)
            return AM_TRUE;
    }
    return AM_FALSE;
}

struct http_status *get_http_status(int status) {
    int i;
    for (i = 0; i < ARRAY_SIZE(http_status_list); i++) {
        struct http_status *s = &http_status_list[i];
        if (s->code == status)
            return s;
    }
    return &http_status_list[40]; /* HTTP-500 */
}

void am_free(void *ptr) {
    if (ptr != NULL) {
        free(ptr);
    }
}

void am_secure_zero_memory(void *v, size_t sz) {
#ifdef _WIN32
    SecureZeroMemory(v, sz);
#else
    size_t n = sz;
    volatile unsigned char *p = v;
    while (n--) *p++ = 0;
#endif
}

char is_big_endian() {

    union {
        uint32_t i;
        char c[4];
    } b = {0x01020304};

    return b.c[0] == 1;
}

/**
 * Match a subject against a pattern.
 * 
 * @param instance_id: the current instance id for debugging purposes
 * @param subject: the subject we're matching
 * @param pattern: the pattern we're using to match
 * 
 * @return AM_OK (0) if there is a match, or we pass in NULL for the subject and/or pattern
 *         AM_FAIL (1) if there is no match, or the pattern doesn't compile
 */
am_return_t match(unsigned long instance_id, const char *subject, const char *pattern) {
    pcre* x = NULL;
    const char* error;
    int erroroffset, rc = -1;
    int offsets[3];
    am_return_t result = AM_OK;

    if (subject == NULL || pattern == NULL) {
        return result;
    }
    x = pcre_compile(pattern, 0, &error, &erroroffset, NULL);
    if (x == NULL) {
        AM_LOG_DEBUG(instance_id, "match: pcre_compile failed on \"%s\" with error %s", pattern, (error == NULL) ? "unknown" : error);
        return AM_FAIL;
    }

    rc = pcre_exec(x, NULL, subject, (int) strlen(subject), 0, 0, offsets, 3);
    if (rc < 0) {
        AM_LOG_DEBUG(instance_id, "match(): '%s' does not match '%s'", subject, pattern);
        result = AM_FAIL;
    } else {
        AM_LOG_DEBUG(instance_id, "match(): '%s' matches '%s'", subject, pattern);
    }
    pcre_free(x);

    return result;
}

/**
 * Match groups specified in the compiled regular expression against the subject specified.
 * The matching groups are returned in bulk in the return value as a number of null separated strings.
 *
 * @param x: the compiled regular expression
 * @param capture_groups: the number of capture groups specified in the regular expression
 * @param subject: the string to be matched against the regular expression
 * @param len: initially set to the length of the subject, this is changed to be a count of the number
 *        of strings stored in the return value, separated by null strings.
 * 
 * @return null separated matching strings
 */
char *match_group(pcre *x, int capture_groups, const char *subject, size_t *len) {

    /* pcre itself needs space in the max_capture_groups */
    int max_capture_groups = (capture_groups + 1) * 3;
    size_t k = 0, slen = *len;
    int i, substring_len, rc, ret_len = 0;
    unsigned int offset = 0;
    char* result = NULL;
    int* ovector;

    if (x == NULL || subject == NULL) {
        return NULL;
    }
    if ((ovector = calloc(max_capture_groups, sizeof (int))) == NULL) {
        return NULL;
    }
    while (offset < slen && (rc = pcre_exec(x, 0, subject, (int) slen, offset, 0, ovector, max_capture_groups)) >= 0) {
        for (i = 1 /* skip the first pair: "identify the portion of the subject string matched by the entire pattern" */;
                i < rc; ++i) {
            char *rslt, *ret_tmp;
            if ((substring_len = pcre_get_substring(subject, ovector, rc, i, (const char **) &rslt)) > 0) {
                ret_tmp = realloc(result, ret_len + substring_len + 1);
                if (ret_tmp == NULL) {
                    am_free(result);
                    pcre_free_substring(rslt);
                    free(ovector);
                    return NULL;
                }
                result = ret_tmp;

                /* return value is stored as:
                 * key\0value\0...
                 */
                memcpy(result + ret_len, rslt, substring_len);
                result[ret_len + substring_len] = '\0';
                ret_len += substring_len + 1;
                k++;
            }
            pcre_free_substring(rslt);
        }
        offset = ovector[1];
    }
    *len = k;
    free(ovector);
    return result;
}

static void uri_normalize(struct url *url, char *path) {

    char *s, *o, *p = path != NULL ? strdup(path) : NULL;
    int i, m = 0, list_sz = 0;
    char **segment_list = NULL, **segment_list_norm = NULL, **tmp;
    char u[AM_URI_SIZE + 1];

    if (p == NULL) {
        if (url != NULL) {
            url->error = path != NULL ? AM_ENOMEM : AM_EINVAL;
        }
        return;
    }
    o = p; /* preserve original pointer */

    /* split path into segments */
    while ((s = am_strsep(&p, "/")) != NULL) {
        if (strcmp(s, ".") == 0) {
            continue; /* remove (ignore) single dot segments */
        }
        tmp = (char **) realloc(segment_list, sizeof (char *) * (++list_sz));
        if (tmp == NULL) {
            AM_FREE(o, segment_list);
            url->error = AM_ENOMEM;
            return;
        }
        segment_list = tmp;
        segment_list[list_sz - 1] = s;
    }
    if (list_sz == 0) {
        /* nothing to do here */
        AM_FREE(o, segment_list);
        if (url != NULL) {
            url->error = AM_SUCCESS;
        }
        return;
    }

    /* create a list for normalized segment storage */
    segment_list_norm = (char **) calloc(list_sz, sizeof (char *));
    if (segment_list_norm == NULL) {
        AM_FREE(o, segment_list);
        if (url != NULL) {
            url->error = AM_ENOMEM;
        }
        return;
    }

    for (i = 0; i < list_sz; i++) {
        if (strcmp(segment_list[i], "..") == 0) {
            /* remove double dot segments */
            if (m-- <= 1) {
                m = 1;
                continue;
            }
            segment_list_norm[m] = NULL;
        } else {
            segment_list_norm[m++] = segment_list[i];
        }
    }

    memset(&u[0], 0, sizeof (u));
    /* join normalized segments */
    for (i = 0; i < list_sz; i++) {
        if (segment_list_norm[i] == NULL) {
            break;
        }
        if (i == 0) {
            strncpy(u, segment_list_norm[i], sizeof (u) - 1);
            if ((i + 1) < list_sz && segment_list_norm[i + 1] != NULL) {
                strncat(u, "/", sizeof (u) - 1);
            }
        } else {
            strncat(u, segment_list_norm[i], sizeof (u) - 1);
            if ((i + 1) < list_sz && segment_list_norm[i + 1] != NULL) {
                strncat(u, "/", sizeof (u) - 1);
            }
        }
    }
    memcpy(path, u, sizeof (u));

    AM_FREE(segment_list_norm, segment_list, o);

    if (url != NULL) {
        url->error = AM_SUCCESS;
    }
}

static int query_attribute_compare(const void *a, const void *b) {
    int status;
    struct query_attribute *ia = (struct query_attribute *) a;
    struct query_attribute *ib = (struct query_attribute *) b;
    status = strcmp(ia->key, ib->key);
    if (status == 0) {
        /* variable names (keys) are the same, we need to further compare the values */
        status = strcmp(ia->key_value, ib->key_value);
    }
    return status;
}

/**
 * Parse a URL into a struct url which contains members broken out into protocol,
 * host, path, etc.
 *
 * @param u The url to break out
 * @param url The broken out url structure to break out into
 * @return AM_SUCCESS if all goes well, AM_ERROR if it does not.
 */
int parse_url(const char *u, struct url *url) {
    int i = 0, port = 0, last = 0;
    char *d, *p, uri[AM_URI_SIZE + 1];

    if (url == NULL) {
        return AM_ERROR;
    }
    if (u == NULL) {
        url->error = AM_EINVAL;
        return AM_ERROR;
    }
    if (strlen(u) > (AM_PROTO_SIZE + AM_HOST_SIZE + 6 + AM_URI_SIZE /* max size of all sscanf format limits */)) {
        url->error = AM_E2BIG;
        return AM_ERROR;
    }

    url->error = url->ssl = url->port = 0;
    memset(&uri[0], 0, sizeof (uri));
    memset(&url->proto[0], 0, sizeof (url->proto));
    memset(&url->host[0], 0, sizeof (url->host));
    memset(&url->path[0], 0, sizeof (url->path));
    memset(&url->query[0], 0, sizeof (url->query));

    if (sscanf(u, HD1, url->proto, url->host, &port, url->path) == 4) {
        ;
    } else if (sscanf(u, HD2, url->proto, url->host, url->path) == 3) {
        ;
    } else if (sscanf(u, HD3, url->proto, url->host, &port) == 3) {
        ;
    } else if (sscanf(u, HD4, url->proto, url->host) == 2) {
        ;
    } else {
        url->error = AM_EOF;
        return AM_ERROR;
    }

    url->port = port < 0 ? -(port) : port;
    if (strcasecmp(url->proto, "https") == 0) {
        url->ssl = 1;
    } else {
        url->ssl = 0;
    }
    if (strcasecmp(url->proto, "https") == 0 && url->port == 0) {
        url->port = 443;
    } else if (strcasecmp(url->proto, "http") == 0 && url->port == 0) {
        url->port = 80;
    }
    if (!ISVALID(url->path)) {
        strcpy(url->path, "/");
    } else if (url->path[0] != '/') {
        size_t ul = strlen(url->path);
        if (ul < sizeof (url->path)) {
            memmove(url->path + 1, url->path, ul);
        }
        url->path[0] = '/';
    }

    /* split out a query string, if any and sort query parameters */
    p = strchr(url->path, '?');
    if (p != NULL) {
        char *token, *temp, query[AM_URI_SIZE + 1], *sep;
        struct query_attribute *list;
        int sep_count, sep_count_init, j;
        strncpy(url->query, p, sizeof (url->query) - 1);
        *p = 0;

        strncpy(query, url->query + 1 /* skip '?' */, sizeof (url->query) - 1);
        sep_count = char_count(query, '&', NULL);
        if (sep_count > 0) {
            list = (struct query_attribute *) calloc(++sep_count, sizeof (struct query_attribute));
            if (list == NULL) {
                url->error = AM_ENOMEM;
                return AM_ERROR;
            }
            sep_count_init = sep_count;
            sep_count = 0;

            for ((token = strtok_r(query, "&", &temp)); token; (token = strtok_r(NULL, "&", &temp))) {
                struct query_attribute *elm = &list[sep_count++];
                elm->key_value = token;
                elm->key = strdup(token);
                if (elm->key == NULL) {
                    for (j = 0; j < sep_count_init; j++) {
                        struct query_attribute *elm = &list[j];
                        am_free(elm->key);
                    }
                    free(list);
                    url->error = AM_ENOMEM;
                    return AM_ERROR;
                }
                sep = strchr(elm->key, '=');
                if (sep != NULL) {
                    *sep = '\0';
                }
            }

            qsort(list, sep_count, sizeof (struct query_attribute), query_attribute_compare);

            strncpy(url->query, "?", sizeof (url->query) - 1);
            for (j = 0; j < sep_count; j++) {
                struct query_attribute *elm = &list[j];
                if (j > 0) {
                    strcat(url->query, "&");
                }
                strcat(url->query, elm->key_value);
                free(elm->key);
            }
            free(list);
        }
    }

    d = url_decode(url->path);
    if (d == NULL) {
        url->error = AM_ENOMEM;
        return AM_ERROR;
    }
    
    p = d;
    /* replace all consecutive '/' with a single '/' */
    while (*p != '\0') {
        if (*p != '/' || (*p == '/' && last != '/')) {
            uri[i++] = *p;
        }
        last = *p;
        p++;
    }
    free(d);

    /* normalize path segments, RFC-2396, section-5.2 */
    uri_normalize(url, uri);

    strncpy(url->path, uri, sizeof (url->path) - 1);
    return AM_SUCCESS;
}

/**
 * Encode characters in a URL, copying the encoded URL into dynamic memory.
 *
 * @param str The string to be encoded
 * @return The encoded string copied into dynamic memory or NULL if NULL was originally
 * passed in.
 */
char *url_encode(const char *str) {
    if (str != NULL) {
        unsigned char *pstr = (unsigned char *) str;
        char *buf = (char *) calloc(1, strlen(str) * 3 + 1);
        char *pbuf = buf;

        if (buf == NULL) {
            return NULL;
        }
        while (*pstr) {
#ifdef AM_URL_ENCODE_RFC3986
            /* This is the correct URL encoding according to RFC 3986 */
            if (isalnum(*pstr) || *pstr == '-' || *pstr == '_' || *pstr == '.' || *pstr == '~') {
#else
            /* This is the the encoding in prior versions of the agents */
            if (isalnum(*pstr) || *pstr == '-' || *pstr == '_' || *pstr == '.' || *pstr == '*') {
#endif
                *pbuf++ = *pstr;
            } else if (*pstr == ' ') {
                *pbuf++ = '%';
                *pbuf++ = '2';
                *pbuf++ = '0';
            } else {
                *pbuf++ = '%';
                *pbuf++ = hex_chars[((*pstr) >> 4) & 0xF];
                *pbuf++ = hex_chars[(*pstr) & 0xF];
            }
            pstr++;
        }
        *pbuf = '\0';
        return buf;
    }
    return NULL;
}

/**
 * Decode a URL encoded string, copying the result into dynamic memory.
 *
 * @param str The URL encoded string
 * @return The decoded string copied into dynamic memory or NULL if NULL was originally
 * passed in.
 */
char *url_decode(const char *str) {
    const char *c;
    char *ptr, *dest = NULL;

    if (str == NULL) {
        return NULL;
    }

    dest = ptr = strdup(str);
    if (dest == NULL) {
        return NULL;
    }

#define BASE16_TO_BASE10(x) (isdigit(x) ? ((x) - '0') : (toupper((x)) - 'A' + 10))

    for (c = str; *c; c++) {
        if (*c != '%' || !isxdigit(c[1]) || !isxdigit(c[2])) {
            *ptr++ = *c == '+' ? ' ' : *c;
        } else {
            *ptr++ = (BASE16_TO_BASE10(c[1]) * 16) + (BASE16_TO_BASE10(c[2]));
            c += 2;
        }
    }
    *ptr = 0;
    return dest;
}

#if defined(_MSC_VER) && _MSC_VER < 1900

int am_vsnprintf(char *s, size_t size, const char *format, va_list ap) {
    int count = -1;
    if (size != 0)
        count = _vsnprintf_s(s, size, _TRUNCATE, format, ap);
    if (count == -1)
        count = _vscprintf(format, ap);
    return count;
}

int am_snprintf(char *s, size_t n, const char *format, ...) {
    va_list ap;
    int ret;
    va_start(ap, format);
    ret = am_vsnprintf(s, n, format, ap);
    va_end(ap);
    return ret;
}

#endif

/**
 * am_vasprintf allocates sufficient dynamic memory to hold the arguments specified by the printf
 * style arguments and returns the number of bytes allocated.  If dynamic memory allocation fails,
 * a value of zero or less will be returned.
 *
 * @param buffer a pointer which will be changed to point to the dynamically allocated memory, or
 * NULL if the memory allocation fails
 * @param fmt the printf style format string
 * @param arg the start of the variadic arguments
 * @return the number of bytes allocated to "buffer"
 */
int am_vasprintf(char **buffer, const char *fmt, va_list arg) {
    int size;
    va_list ap;
    *buffer = NULL;
    va_copy(ap, arg);
    size = vsnprintf(NULL, 0, fmt, ap);
    if (size >= 0) {
        if ((*buffer = malloc(++size)) != NULL) {
            va_end(ap);
            va_copy(ap, arg);
            if ((size = vsnprintf(*buffer, size, fmt, ap)) < 0) {
                free(*buffer);
                *buffer = NULL;
            }
        }
    }
    va_end(ap);
    return size;
}

/**
 * am_asprintf is set up to concatenate printf style variadic values into a piece of
 * dynamically allocated memory, while freeing up intermediate values.  Here is an
 * example from do_cookie_set_generic with all the NULL checking removed:
 *
 * char* cookie = NULL;<br>
 * am_asprintf(&cookie, "%s", prefix);<br>
 * am_asprintf(&cookie, "%s%s", cookie, cookie_value);<br>
 * am_asprintf(&cookie, "%s%s", cookie, name);<br>
 *
 * DO NOT call this function with a first parameter referring to stack-based storage.
 *
 * @param buffer a pointer which will be changed to point to the dynamically allocated memory
 * @param fmt the printf style format string
 * @param varargs the variadic parameters start here
 * @return the number of bytes allocated
 */
int am_asprintf(char **buffer, const char *fmt, ...) {
    int size;
    char *tmp;
    va_list ap;
    va_start(ap, fmt);
    tmp = *buffer;
    size = am_vasprintf(buffer, fmt, ap);
    am_free(tmp);
    va_end(ap);
    return size;
}

int gzip_inflate(const char *compressed, size_t *compressed_sz, char **uncompressed) {
    size_t full_length, half_length, uncompLength;
    char *uncomp = NULL;
    z_stream strm;
    int done = 1;

    if (compressed == NULL || compressed_sz == NULL || *compressed_sz == 0) {
        return 1;
    }

    full_length = *compressed_sz;
    half_length = *compressed_sz / 2;
    uncompLength = full_length;

    uncomp = (char *) calloc(sizeof (char), uncompLength);
    if (uncomp == NULL) return 1;

    strm.next_in = (Bytef *) compressed;
    strm.avail_in = (uInt) * compressed_sz;
    strm.total_out = 0;
    strm.zalloc = Z_NULL;
    strm.zfree = Z_NULL;

    if (inflateInit2(&strm, (16 + MAX_WBITS)) != Z_OK) {
        free(uncomp);
        return 1;
    }

    while (done) {
        int err;

        if (strm.total_out >= uncompLength) {
            char *uncomp2 = (char *) calloc(sizeof (char), uncompLength + half_length);
            if (uncomp2 == NULL) {
                free(uncomp);
                return 1;
            }
            memcpy(uncomp2, uncomp, uncompLength);
            uncompLength += half_length;
            free(uncomp);
            uncomp = uncomp2;
        }

        strm.next_out = (Bytef *) (uncomp + strm.total_out);
        strm.avail_out = (uInt) uncompLength - strm.total_out;

        err = inflate(&strm, Z_SYNC_FLUSH);
        if (err == Z_STREAM_END) done = 0;
        else if (err != Z_OK) {
            break;
        }
    }

    if (inflateEnd(&strm) != Z_OK) {
        free(uncomp);
        return 1;
    }

    *uncompressed = uncomp;
    *compressed_sz = strm.total_out;
    return 0;
}

int gzip_deflate(const char *uncompressed, size_t *uncompressed_sz, char **compressed) {
    uLong comp_length, ucomp_length;
    char *comp = NULL;
    z_stream strm;
    int deflate_status;

    if (uncompressed == NULL || uncompressed_sz == NULL || *uncompressed_sz == 0) {
        return 1;
    }

    ucomp_length = (uLong) * uncompressed_sz;
    strm.zalloc = Z_NULL;
    strm.zfree = Z_NULL;
    strm.opaque = Z_NULL;
    strm.total_out = 0;
    strm.next_in = (Bytef *) uncompressed;
    strm.avail_in = ucomp_length;

    if (deflateInit2(&strm, Z_DEFAULT_COMPRESSION, Z_DEFLATED, (15 + 16),
            8, Z_DEFAULT_STRATEGY) != Z_OK) {
        free(comp);
        return 1;
    }

    comp_length = deflateBound(&strm, ucomp_length);
    comp = (char *) calloc(sizeof (char), comp_length);
    if (comp == NULL) {
        deflateEnd(&strm);
        return 1;
    }

    do {
        strm.next_out = (Bytef *) (comp + strm.total_out);
        strm.avail_out = comp_length - strm.total_out;
        deflate_status = deflate(&strm, Z_FINISH);
    } while (deflate_status == Z_OK);

    if (deflate_status != Z_STREAM_END) {
        free(comp);
        deflateEnd(&strm);
        return 1;
    }

    if (deflateEnd(&strm) != Z_OK) {
        free(comp);
        return 1;
    }

    *compressed = comp;
    *uncompressed_sz = strm.total_out;
    return 0;
}

unsigned long am_instance_id(const char *instance_id) {
    uLong crc = crc32(0L, Z_NULL, 0);
    if (instance_id == NULL) return 0;
    crc = crc32(crc, (const Bytef *) instance_id, (uInt) strlen(instance_id));
    return crc;
}

/**
 * Trim the string pointed to by "a", removing either space characters (as defined by
 * the isspace macro) or the specified character.
 *
 * @param str The incoming string to be trimmed
 * @param w The character to trim, or if null then trim whitespace
 */
void trim(char *str, char w) {
    char *in = str, *out = str;
    int i = 0;
#define TRIM_TEST(x,y) ((x == 0 || isspace(x)) ? isspace(y) : (y == x))

    if (ISINVALID(str)) return;

    for (in = str; *in && TRIM_TEST(w, *in); ++in)
        ;
    if (*in == '\0') {
        out[0] = 0;
        return;
    }
    if (str != in) {
        memmove(str, in, in - str);
    }
    while (*in) {
        out[i++] = *in++;
    }
    out[i] = 0;

    while (--i >= 0) {
        if (!TRIM_TEST(w, out[i])) break;
    }
    out[++i] = 0;
}

/**
 * This is similar to the "strtok" function, except not nearly so stupid.
 * This function finds a "delim" separator within the string pointed to by "*str".
 * It works best when "delim" is one character in length and less well in other
 * cases.  The returned value is the separated token, which is null terminated.
 * Since the original buffer is overwritten, there is no need to free the returned
 * result.
 *
 * @param str pointer to storage containing the original string - the pointer is
 * updated to point after the delimiter, if found
 * @param delim string containing single character delimiter
 * @return if the delimiter was found, a pointer to the null terminated token, or
 * NULL if it was not found
 */
char *am_strsep(char **str, const char *delim) {
    char *s, *t;
    const char *sp;
    int c, sc;

    if ((s = *str) == NULL) {
        return NULL;
    }
    for (t = s;;) {
        c = *s++;
        sp = delim;
        do {
            if ((sc = *sp++) == c) {
                if (c == 0) {
                    s = NULL;
                } else {
                    s[-1] = '\0';
                }
                *str = s;
                return t;
            }
        } while (sc != 0);
    }
}

/* Reverse function of strstr() */
char *am_strrstr(const char *str, const char *search) {
    char *last = NULL;
    char *ptr = (char *) str;
    while ((ptr = strstr(ptr, search))) {
        last = ptr++;
    }
    return last;
}

int compare_property(const char *line, const char *property) {
    if (ISVALID(line) && ISVALID(property)) {
        size_t l = strlen(property);
        if (strncmp(line, property, l) == 0 && line[l] != '\0' &&
                (line[l] == ' ' || line[l] == '=' || line[l] == '[')) {
            return AM_SUCCESS;
        }
    }
    return AM_NOT_FOUND;
}

int get_line(char **line, size_t *size, FILE *file) {
    int c, l = 0;
    unsigned int i = 0;

#define DEFAULT_LINE_LEN 256
    if (*line == NULL) {
        *line = malloc(DEFAULT_LINE_LEN);
        if (*line == NULL) {
            return -1;
        }
        *size = DEFAULT_LINE_LEN;
    }

    while (1) {
        c = getc(file);
        if (c < 0) {
            if (l == 0) {
                return -1; /* EOF */
            } else {
                /* make sure we are not missing the last line (one w/o newlines) */
                break;
            }
        }

        (*line)[i++] = (char) c;
        /* time to expand the buffer? */
        if (i >= *size) {
            size_t newsize = (*size) << 1;
            char *new = realloc(*line, newsize);
            if (new == NULL) {
                return -1;
            }
            *line = new;
            (*size) = newsize;
        }
        if (c == '\n' || c == '\r') {
            break;
        }
        l = 1;
    };

    (*line)[i] = 0;
    return i;
}

/**
 * Return the requested element of the request_method_str array, or the zeroth element
 * if the one we asked for was out of bounds.
 *
 * @param method The index into the array
 * @return
 */
const char *am_method_num_to_str(int method) {
    if (method >= 0 && method < ARRAY_SIZE(request_method_str)) {
        return request_method_str[(unsigned char) method];
    }
    return request_method_str[0];
}

/**
 * Caselessly search for the incoming method name within the request_method_str array and
 * if found, return the corresponding element.  If not found, return zero.
 *
 * @param method_str The string to caselessly search for within request_method_str
 * @return the corresponding index, or zero if not found.
 */
int am_method_str_to_num(const char *method_str) {
    unsigned char i;
    if (method_str != NULL) {
        for (i = 0; i < ARRAY_SIZE(request_method_str); i++) {
            if (!strcasecmp(method_str, request_method_str[i])) {
                return i;
            }
        }
    }
    return AM_REQUEST_UNKNOWN;
}

am_status_t get_cookie_value(am_request_t *rq, const char *separator, const char *cookie_name,
        const char *cookie_header_val, char **value) {
    size_t value_len = 0, ec = 0;
    am_status_t found = AM_NOT_FOUND;
    char *a, *b, *header_val = NULL, *c = NULL;

    if (ISINVALID(cookie_name)) {
        return AM_EINVAL;
    }
    if (ISINVALID(cookie_header_val)) {
        return AM_NOT_FOUND;
    }

    *value = NULL;
    header_val = strdup(cookie_header_val);
    if (header_val == NULL) {
        return AM_ENOMEM;
    }

    AM_LOG_DEBUG(rq->instance_id, "get_cookie_value(%s): parsing cookie header: %s",
            separator, cookie_header_val);

    for ((a = strtok_r(header_val, separator, &b)); a; (a = strtok_r(NULL, separator, &b))) {
        if (strcmp(separator, "=") == 0 || strcmp(separator, "~") == 0) {
            /* trim any leading/trailing whitespace */
            trim(a, ' ');
            if (found != AM_SUCCESS && strcmp(a, cookie_name) == 0) found = AM_SUCCESS;
            else if (found == AM_SUCCESS && a[0] != '\0') {
                value_len = strlen(a);
                if ((*value = strdup(a)) == NULL) {
                    found = AM_NOT_FOUND;
                } else {
                    (*value)[value_len] = '\0';
                    /* trim any leading/trailing double-quotes */
                    trim(*value, '"');
                }
            }
        } else {
            if (strstr(a, cookie_name) == NULL) continue;
            for (ec = 0, c = a; *c != '\0'; ++c) {
                if (*c == '=') ++ec;
            }
            if (ec > 1) {
                c = strchr(a, '=');
                *c = '~';
                if ((found = get_cookie_value(rq, "~", cookie_name, a, value)) == AM_SUCCESS) break;
            } else {
                if ((found = get_cookie_value(rq, "=", cookie_name, a, value)) == AM_SUCCESS) break;
            }
        }
    }
    free(header_val);
    return found;
}

am_status_t get_token_from_url(am_request_t *rq) {
    char *token, *tmp = ISVALID(rq->url.query) ?
            strdup(rq->url.query + 1) : NULL;
    char *query = NULL;
    int ql = 0;
    char *o = tmp;
    size_t cn_sz;

    if (tmp == NULL) return AM_ENOMEM;
    if (!ISVALID(rq->conf->cookie_name)) {
        free(tmp);
        return AM_EINVAL;
    }
    cn_sz = strlen(rq->conf->cookie_name);

    while ((token = am_strsep(&tmp, "&")) != NULL) {
        if (!ISVALID(rq->token) &&
                strncmp(token, rq->conf->cookie_name, cn_sz) == 0) {
            /* session token as a query parameter (cookie-less mode) */
            char *v = strstr(token, "=");
            if (v != NULL && *(v + 1) != '\n') {
                rq->token = strdup(v + 1);
            }
        } else if (!ISVALID(rq->token) &&
                rq->conf->cdsso_enable && strncmp(token, "LARES=", 6) == 0) {
            /* session token (LARES/SAML encoded) as a query parameter */
            size_t clear_sz = strlen(token) - 6;
            char *clear = clear_sz > 0 ? base64_decode(token + 6, &clear_sz) : NULL;
            if (clear != NULL) {
                struct am_namevalue *e, *t, *session_list;
                session_list = am_parse_session_saml(rq->instance_id, clear, clear_sz);

                AM_LIST_FOR_EACH(session_list, e, t) {
                    if (strcmp(e->n, "sid") == 0 && ISVALID(e->v)) {
                        rq->token = strdup(e->v);
                        break;
                    }
                }
                delete_am_namevalue_list(&session_list);
                free(clear);
            }
        } else {
            /* reconstruct query parameters w/o a session token(s) */
            if (query == NULL) {
                ql = am_asprintf(&query, "?%s&", token);
            } else {
                ql = am_asprintf(&query, "%s%s&", query, token);
            }
        }
    }

    if (ql > 0 && query[ql - 1] == '&') {
        query[ql - 1] = 0;
        strncpy(rq->url.query, query, sizeof (rq->url.query) - 1);
    } else if (ql == 0 && ISVALID(rq->token)) {
        /* token is the only query parameter - clear it */
        memset(rq->url.query, 0, sizeof (rq->url.query));
        /* TODO: should a question mark be left there even when token is the only parameter? */
    }
    AM_FREE(query, o);
    return ISVALID(rq->token) ? AM_SUCCESS : AM_NOT_FOUND;
}

int remove_cookie(am_request_t *rq, const char *cookie_name, char **cookie_hdr) {
    char *tmp, *tok, *last = NULL;
    size_t cookie_name_len;

    if (rq == NULL || rq->ctx == NULL || !ISVALID(cookie_name)) {
        return AM_EINVAL;
    }

    if (!ISVALID(rq->cookies)) {
        return AM_SUCCESS;
    }

    if (strstr(rq->cookies, cookie_name) == NULL) {
        return AM_NOT_FOUND;
    }

    tmp = strdup(rq->cookies);
    if (tmp == NULL) return AM_ENOMEM;

    cookie_name_len = strlen(cookie_name);

    tok = strtok_r(tmp, ";", &last);
    while (tok != NULL) {
        char match = AM_FALSE;
        char *equal_sign = strchr(tok, '=');
        /* trim space before the cookie name in the cookie header */
        while (isspace(*tok)) tok++;
        if (equal_sign != NULL && equal_sign != tok) {
            /* trim white space after the cookie name in the cookie header */
            while ((--equal_sign) >= tok && isspace(*equal_sign))
                ;
            equal_sign++;
            /* now compare the cookie names */
            if (equal_sign != tok && (equal_sign - tok) == cookie_name_len &&
                    !strncmp(tok, cookie_name, cookie_name_len)) {
                match = AM_TRUE;
            }
        }
        /* put cookie in a header only if it didn't match cookie name */
        if (!match) {
            am_asprintf(cookie_hdr, "%s%s%s",
                    *cookie_hdr == NULL ? "" : *cookie_hdr,
                    *cookie_hdr != NULL ? ";" : "",
                    tok);
        }
        tok = strtok_r(NULL, ";", &last);
    }

    free(tmp);
    return AM_SUCCESS;
}

char *load_file(const char *filepath, size_t *data_sz) {
    char *text = NULL;
    int fd;
    struct stat st;
    if (stat(filepath, &st) == -1) {
        return NULL;
    }
#ifdef _WIN32
    fd = _open(filepath, _O_BINARY | _O_RDONLY);
#else
    fd = open(filepath, O_RDONLY);
#endif
    if (fd == -1) {
        return NULL;
    }
    text = malloc(st.st_size + 1);
    if (text != NULL) {
        if (st.st_size != read(fd, text, st.st_size)) {
            close(fd);
            free(text);
            return NULL;
        }
        text[st.st_size] = '\0';
        if (data_sz) {
            *data_sz = st.st_size;
        }
    }
    close(fd);
    return text;
}

ssize_t write_file(const char *filepath, const void *data, size_t data_sz) {
    int fd;
    ssize_t wr = 0;
    if (data == NULL) return AM_EINVAL;
#ifdef _WIN32
    fd = _open(filepath, _O_CREAT | _O_WRONLY | _O_TRUNC | _O_BINARY, _S_IWRITE);
#else
    fd = open(filepath, O_CREAT | O_WRONLY | O_TRUNC, S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP);
#endif
    if (fd == -1) {
        return AM_EPERM;
    }
    if (data_sz != (size_t) (wr = write(fd, data,
#ifdef _WIN32
            (unsigned int)
#endif
            data_sz))) {
        close(fd);
        return AM_EOF;
    } else {
#ifdef _WIN32
        _commit(fd);
#else
        fsync(fd);
#endif
    }
    close(fd);
    return wr;
}

char file_exists(const char *fn) {
#ifdef _WIN32
    if (_access(fn, 6) == 0) {
        return AM_TRUE;
    }
#else
    struct stat sb;
    if (stat(fn, &sb) == 0) {
        if (S_ISREG(sb.st_mode) || S_ISDIR(sb.st_mode) || S_ISLNK(sb.st_mode)) {
            if (S_ISDIR(sb.st_mode)) {
                int mask = 0200 | 020;
                /* we need owner/group write permission on a directory */
                if (!(sb.st_mode & mask)) {
                    return AM_FALSE;
                }
            }
            return AM_TRUE;
        }
    }
#endif
    return AM_FALSE;
}

#if defined(_WIN32) || defined(__sun)

/**
 * A windows implementation of a mac function.
 * The strnlen() function returns either the same result as strlen() or maxlen, whichever
 * is smaller.
 */
#if defined(__sun) || (_MSC_VER < 1900)

size_t strnlen(const char *string, size_t maxlen) {
    const char *end = memchr(string, '\0', maxlen);
    return end ? end - string : maxlen;
}

#endif

/**
 * The strndup() function copies at most n characters from the string s1 always null 
 * terminating the copied string. If insufficient memory is available, NULL is returned.
 */
char *strndup(const char *s, size_t n) {
    size_t len = (s == NULL) ? 0 : strnlen(s, n);
    char *new = malloc(len + 1);
    if (new == NULL) {
        return NULL;
    }
    new[len] = '\0';
    return memcpy(new, s, len);
}
#endif

/**
 * Duplicate a string in dynamic memory, converting to lowercase.
 *
 * @param src String to duplicate into dynamic memory and convert to lowercase.
 * @return heap based storage containing the source string's contents converted to lowercase.
 */
char* am_strldup(const char* src) {
    char* result = NULL;
    char* dest;
    if (src == NULL) {
        return result;
    }
    result = malloc(strlen(src) + 1);
    if (result == NULL) {
        return NULL;
    }
    dest = result;

    /* avoid tolower(*src++) because, I believe, tolower is a macro and may have side effects */
    while ((*dest = tolower(*src)) != '\0') {
        dest++;
        src++;
    }
    return result;
}

/**
 * Returns a pointer to the first occurrence of str2 in str1 (in a case, insensitive manner),
 * or a null pointer if str2 is not part of str1.
 */
char* stristr(char* str1, char* str2) {

    char* dup1;
    char* dup2;
    char* p;
    char* result = NULL;

    if (str1 == NULL || str2 == NULL) {
        return NULL;
    }

    dup1 = am_strldup(str1);
    dup2 = am_strldup(str2);
    if (dup1 == NULL || dup2 == NULL) {
        AM_FREE(dup1, dup2);
        return NULL;
    }

    p = strstr(dup1, dup2);
    if (p != NULL) {
        result = str1 + (p - dup1);
    }
    AM_FREE(dup1, dup2);
    return result;
}

/**
 * Decode the string specified by "src" from base 64 into "clear text".  The decoded value is stored on the
 * heap and is not only null terminated, but has its size returned in what "sz" points to.  The incoming
 * value of sz is ignored.
 *
 * @param src The value to decode from base 64 text
 * @param sz The number of bytes in the clear text result
 * @return The decoded result, stored in dynamic memory and null terminated.
 */
char* base64_decode(const char* src, size_t* sz) {
#ifdef _WIN32
    DWORD ulBlobSz = 0, ulSkipped = 0, ulFmt = 0;
    BYTE *out = NULL;
    if (src == NULL || sz == NULL) return NULL;
    if (CryptStringToBinaryA(src, (DWORD) * sz, CRYPT_STRING_BASE64, NULL,
            &ulBlobSz, &ulSkipped, &ulFmt) == TRUE) {
        if ((out = malloc(ulBlobSz + 1)) != NULL) {
            memset(out, 0, ulBlobSz + 1);
            if (CryptStringToBinaryA(src, (DWORD) * sz, CRYPT_STRING_BASE64,
                    out, &ulBlobSz, &ulSkipped, &ulFmt) == TRUE) {
                out[ulBlobSz] = 0;
                *sz = ulBlobSz;
            }
        }
    }
#else
    unsigned char table[256];
    unsigned char* out;
    unsigned char* pos;
    unsigned char* in;
    size_t i, count;

    if (src == NULL || sz == NULL) {
        return NULL;
    }

    memset(table, 64, 256);
    for (i = 0; i < sizeof (base64_table); i++) {
        table[base64_table[i]] = i;
    }

    for (in = (unsigned char *) src; table[*in++] <= 63; /* void */)
        ;

    i = (in - (unsigned char *) src) - 1;
    count = (((i + 3) / 4) * 3) + 1;

    pos = out = malloc(count);
    if (out == NULL) {
        return NULL;
    }

    in = (unsigned char *) src;

    while (i > 4) {
        *pos++ = (unsigned char) (table[in[0]] << 2 | table[in[1]] >> 4);
        *pos++ = (unsigned char) (table[in[1]] << 4 | table[in[2]] >> 2);
        *pos++ = (unsigned char) (table[in[2]] << 6 | table[in[3]]);
        in += 4;
        i -= 4;
    }

    if (i > 1) {
        *pos++ = (unsigned char) (table[in[0]] << 2 | table[in[1]] >> 4);
    }
    if (i > 2) {
        *pos++ = (unsigned char) (table[in[1]] << 4 | table[in[2]] >> 2);
    }
    if (i > 3) {
        *pos++ = (unsigned char) (table[in[2]] << 6 | table[in[3]]);
    }

    *pos = '\0';
    count -= (4 - i) & 3;
    *sz = count - 1;
#endif
    return (char *) out;
}

/**
 * Encode the "clear text" string specified by "in" into base 64.  The encoded value is stored on the
 * heap and is not only null terminated, but has its size returned in what "sz" points to.  It is very
 * important to set what "sz" points to before calling this function.  Setting it to less than the
 * length of the string will result in only that amount of text being encoded.  Setting it to zero
 * will result in a serious crash.
 *
 * @param src The value to encode into base 64 text
 * @param sz The number of bytes to encode, then the number of bytes in the result
 * @return The encoded result, stored in dynamic memory and null terminated.
 */
char *base64_encode(const void *in, size_t *sz) {
    size_t i;
    char* p;
    char* out;
    const uint8_t *src = in;

    if (src == NULL || sz == NULL) {
        return NULL;
    }

    p = out = malloc(((*sz + 2) / 3 * 4) + 1);
    if (out == NULL) {
        return NULL;
    }

    for (i = 0; i < *sz - 2; i += 3) {
        *p++ = base64_table[(src[i] >> 2) & 0x3F];
        *p++ = base64_table[((src[i] & 0x3) << 4) | ((int) (src[i + 1] & 0xF0) >> 4)];
        *p++ = base64_table[((src[i + 1] & 0xF) << 2) | ((int) (src[i + 2] & 0xC0) >> 6)];
        *p++ = base64_table[src[i + 2] & 0x3F];
    }

    if (i < *sz) {
        *p++ = base64_table[(src[i] >> 2) & 0x3F];
        if (i == (*sz - 1)) {
            *p++ = base64_table[((src[i] & 0x3) << 4)];
            *p++ = '=';
        } else {
            *p++ = base64_table[((src[i] & 0x3) << 4) | ((int) (src[i + 1] & 0xF0) >> 4)];
            *p++ = base64_table[((src[i + 1] & 0xF) << 2)];
        }
        *p++ = '=';
    }

    *p = '\0';
    *sz = p - out;
    return out;
}

/**
 * Recursively delete all elements in a list of am_cookie structures.
 */
void delete_am_cookie_list(struct am_cookie** list) {
    struct am_cookie* t;

    if (list == NULL || *list == NULL) {
        return;
    }

    t = *list;
    if (t != NULL) {
        delete_am_cookie_list(&t->next);
        AM_FREE(t->name, t->value, t->domain, t->max_age, t->path);
        free(t);
    }
}

/**
 * Count the number of occurrences of the character "c" within "string", while setting
 * what "last" points to, to the last character of "string" if "last" is not NULL.
 *
 * @param string The incoming string, which may not be NULL
 * @param c The character to count the occurrences of
 * @param last If not null, what this points to is set to the last character of the string
 * @return The number of characters "c" in "string"
 */
int char_count(const char *string, int c, int *last) {
    int j, count;
    for (j = 0, count = 0; string[j] != '\0'; j++) {
        count += (string[j] == c);
    }
    if (last != NULL) {
        *last = (j > 0) ? string[j - 1] : 0;
    }
    return count;
}

/**
 * Concatenate two strings into dynamic memory, setting "str" to point to the newly
 * allocated storage and "str_sz" to the allocated storage length (if not NULL).
 *
 * @param str Pointer to dynamically allocated storage containing the first string
 * @param str_sz Pointer to number of characters in "*str" to copy or NULL to copy
 * all characters
 * @param s2 Pointer to second string, to be concatenated onto the end of the first
 * @param s2sz The number of characters in s2 to concatenate
 * @return AM_EINVAL if parameters are invalid, AM_ENOMEM if we run out of memory
 * or AM_SUCCESS if all goes well.
 */
am_status_t concat(char **str, size_t *str_sz, const char *s2, size_t s2sz) {
    size_t len = 0;
    char *str_tmp;
    if (str == NULL || s2 == NULL) {
        return AM_EINVAL;
    }
    if (*str != NULL) {
        len = str_sz == NULL ? strlen(*str) : *str_sz;
    }
    len += s2sz;
    str_tmp = realloc(*str, len + 1);
    if (str_tmp == NULL) {
        am_free(*str);
        return AM_ENOMEM;
    } else {
        *str = str_tmp;
    }
    (*str)[len - s2sz] = '\0';
    strncat(*str, s2, s2sz);
    if (str_sz != NULL) {
        *str_sz = len;
    }
    return AM_SUCCESS;
}

/**
 * Generate something that looks like a UUID.  It contains random values and has no guarantee
 * of uniqueness other than it is random.
 */
void uuid(char *buf, size_t buflen) {

    union {

        struct {
            uint32_t time_low;
            uint16_t time_mid;
            uint16_t time_hi_and_version;
            uint8_t clk_seq_hi_res;
            uint8_t clk_seq_low;
            uint8_t node[6];
            uint16_t node_low;
            uint32_t node_hi;
        } u;
        unsigned char __rnd[16];
    } uuid_data;

#ifdef _WIN32
    HCRYPTPROV hcp;
    if (CryptAcquireContextA(&hcp, NULL, NULL, PROV_RSA_FULL,
            CRYPT_VERIFYCONTEXT | CRYPT_SILENT)) {
        CryptGenRandom(hcp, sizeof (uuid_data), uuid_data.__rnd);
        CryptReleaseContext(hcp, 0);
    }
#else
    size_t sz;
    FILE *fp = fopen("/dev/urandom", "r");
    if (fp != NULL) {
        sz = fread(uuid_data.__rnd, 1, sizeof (uuid_data), fp);
        fclose(fp);
    }
#endif

    uuid_data.u.clk_seq_hi_res = (uuid_data.u.clk_seq_hi_res & ~0xC0) | 0x80;
    uuid_data.u.time_hi_and_version = htons((uuid_data.u.time_hi_and_version & ~0xF000) | 0x4000);

    snprintf(buf, buflen, "%08x-%04x-%04x-%02x%02x-%02x%02x%02x%02x%02x%02x",
            uuid_data.u.time_low, uuid_data.u.time_mid, uuid_data.u.time_hi_and_version,
            uuid_data.u.clk_seq_hi_res, uuid_data.u.clk_seq_low,
            uuid_data.u.node[0], uuid_data.u.node[1], uuid_data.u.node[2],
            uuid_data.u.node[3], uuid_data.u.node[4], uuid_data.u.node[5]);
}

int am_session_decode(am_request_t *r) {
    size_t tl, i;
    int nv = 0;
    char *begin, *end;

    enum {
        AM_NA, AM_SI, AM_SK, AM_S1
    } ty = AM_NA;

    char *token = (r != NULL && ISVALID(r->token)) ?
            strdup(r->token) : NULL;

    if (token == NULL) return AM_EINVAL;

    memset(&r->session_info, 0, sizeof (struct am_session_info));
    tl = strlen(token);

    if (strchr(token, '*') != NULL) {
        /* c66 decode */
        char first_star = AM_TRUE;
        for (i = 0; i < tl; i++) {
            if (token[i] == '-') {
                token[i] = '+';
            } else if (token[i] == '_') {
                token[i] = '/';
            } else if (token[i] == '.') {
                token[i] = '=';
            } else if (token[i] == '*') {
                if (first_star) {
                    first_star = AM_FALSE;
                    token[i] = '@';
                } else {
                    token[i] = '#';
                }
            }
        }
    }

    begin = strstr(token, "@");
    if (begin != NULL) {
        end = strstr(begin + 1, "#");
        if (end != NULL) {
            size_t ssz = end - begin - 1;
            unsigned char *c = ssz > 0 ?
                    (unsigned char *) base64_decode(begin + 1, &ssz) : NULL;
            if (c != NULL) {
                unsigned char *raw = c;
                size_t l = ssz;

                while (l > 0) {
                    uint16_t sz;
                    uint8_t len[2]; /* network byte order */

                    memcpy(len, raw, sizeof (len));
                    if (is_big_endian()) {
                        sz = (*((uint16_t *) len));
                    } else {
                        sz = len[1] | len[0] << 8;
                    }

                    l -= sizeof (len);
                    raw += sizeof (len);

                    if (nv % 2 == 0) {
                        if (memcmp(raw, "SI", 2) == 0) {
                            ty = AM_SI;
                        } else if (memcmp(raw, "SK", 2) == 0) {
                            ty = AM_SK;
                        } else if (memcmp(raw, "S1", 2) == 0) {
                            ty = AM_S1;
                        } else {
                            break;
                        }
                    } else {
                        if (ty == AM_SI) {
                            r->session_info.si = malloc(sz + 1);
                            if (r->session_info.si == NULL) {
                                r->session_info.error = AM_ENOMEM;
                                break;
                            }
                            memcpy(r->session_info.si, raw, sz);
                            r->session_info.si[sz] = 0;
                        } else if (ty == AM_SK) {
                            r->session_info.sk = malloc(sz + 1);
                            if (r->session_info.sk == NULL) {
                                r->session_info.error = AM_ENOMEM;
                                break;
                            }
                            memcpy(r->session_info.sk, raw, sz);
                            r->session_info.sk[sz] = 0;
                        } else if (ty == AM_S1) {
                            r->session_info.s1 = malloc(sz + 1);
                            if (r->session_info.s1 == NULL) {
                                r->session_info.error = AM_ENOMEM;
                                break;
                            }
                            memcpy(r->session_info.s1, raw, sz);
                            r->session_info.s1[sz] = 0;
                        }
                    }
                    l -= sz;
                    raw += sz;
                    nv += 1;
                }
                free(c);
            }
        }
    }

    free(token);
    return AM_SUCCESS;
}

const char *get_valid_openam_url(am_request_t *r) {
    const char *val = NULL;
    int valid_idx = get_valid_url_index(r->instance_id);
    /* find active OpenAM service URL */
    if (r->conf->naming_url_sz > 0) {
        val = valid_idx >= r->conf->naming_url_sz ?
                r->conf->naming_url[0] : r->conf->naming_url[valid_idx];
        AM_LOG_DEBUG(r->instance_id,
                "get_valid_openam_url(): active OpenAM service url: %s (%d)",
                val, valid_idx);
    }
    return val;
}

/**
 * Encode ampersand, single and double quotes, less than and greater than within
 * the incoming string.  The buffer containing the string must be large enough to
 * store the extra characters - there is no checking, caveat programmer.
 *
 * @param temp_str The incoming string, which is altered as it is encoded.
 * @param str_len The number of characters in the incoming string to convert.
 */
void xml_entity_escape(char *temp_str, size_t str_len) {
    int k, nshifts = 0;
    const char ec[6] = "&'\"><";
    const char * const est[] = {
        "&amp;", "&apos;", "&quot;", "&gt;", "&lt;"
    };
    size_t i, j, nref = 0, ecl = strlen(ec);

    for (i = 0; i < str_len; i++) {
        for (nref = 0; nref < ecl; nref++) {
            if (temp_str[i] == ec[nref]) {
                if ((nshifts = (int) strlen(est[nref]) - 1) > 0) {
                    memmove(temp_str + i + nshifts, temp_str + i, str_len - i + nshifts);
                    for (j = i, k = 0; k <= nshifts; j++, k++) {
                        temp_str[j] = est[nref][k];
                    }
                    str_len += nshifts;
                }
            }
        }
    }
    temp_str[str_len] = '\0';
}

/*********************************************************************************************
 * Timer functions
 *********************************************************************************************/

void am_timer(uint64_t *t) {
#ifdef _WIN32
    QueryPerformanceCounter((LARGE_INTEGER *) t);
#else
    struct timeval tv;
    gettimeofday(&tv, NULL); //TODO: gethrtime
    *t = ((uint64_t) tv.tv_sec * AM_TIMER_USEC_PER_SEC) + tv.tv_usec;
#endif
}

void am_timer_start(am_timer_t *t) {
    t = t ? t : &am_timer_s;
    t->state = AM_TIMER_ACTIVE;
#ifdef _WIN32
    t->freq = 0;
#else
    t->freq = AM_TIMER_USEC_PER_SEC;
#endif
    am_timer(&t->start);
}

void am_timer_stop(am_timer_t *t) {
    t = t ? t : &am_timer_s;
    am_timer(&t->stop);
    t->state = AM_TIMER_INACTIVE;
}

void am_timer_pause(am_timer_t *t) {
    t = t ? t : &am_timer_s;
    am_timer(&t->stop);
    t->state |= AM_TIMER_PAUSED;
}

void am_timer_resume(am_timer_t *t) {
    uint64_t now, d;
    t = t ? t : &am_timer_s;
    t->state &= ~AM_TIMER_PAUSED;
    am_timer(&now);
    d = now - t->stop;
    t->start += d;
#ifdef _WIN32
    t->freq = 0;
#else
    t->freq = AM_TIMER_USEC_PER_SEC;
#endif
}

double am_timer_elapsed(am_timer_t *t) {
    uint64_t d, s;
    t = t ? t : &am_timer_s;
#ifdef _WIN32
    QueryPerformanceFrequency((LARGE_INTEGER *) & t->freq);
#endif
    if (t->state != AM_TIMER_ACTIVE) {
        s = t->stop;
    } else {
        am_timer(&s);
    }
    d = s - t->start;
    return (double) d / (double) t->freq;
}

void am_timer_report(unsigned long instance_id, am_timer_t *t, const char *op) {
    AM_LOG_DEBUG(instance_id, "am_timer(): %s took %.0f seconds",
            NOTNULL(op), am_timer_elapsed(t));
}

/*********************************************************************************************
 */
static char *rc4(const char *input, size_t input_sz, const char *key, size_t key_sz) {
    int x, y, i, j = 0;
    int box[256];
    char *r = malloc(input_sz + 1);
    if (r == NULL) return NULL;
    for (i = 0; i < 256; i++) {
        box[i] = i;
    }
    for (i = 0; i < 256; i++) {
        j = (key[i % key_sz] + box[i] + j) % 256;
        x = box[i];
        box[i] = box[j];
        box[j] = x;
    }
    for (i = 0; i < (int) input_sz; i++) {
        y = (i + 1) % 256;
        j = (box[y] + j) % 256;
        x = box[y];
        box[y] = box[j];
        box[j] = x;
        r[i] = (char) (input[i] ^ box[(box[y] + box[j]) % 256]);
    }
    r[input_sz] = 0;
    return r;
}

/**
 * Call this function to "decrypt" text.  The text should be copied into dynamic memory (do NOT use
 * stack based storage!) and a pointer to that area passed in as the second parameter.  The storage
 * is freed and the pointer reset to yet more dynamically allocated storage containing the clear text.
 *
 * @param key The key with which to decrypt the password, this is the result of calling base64_encode
 * @param password A pointer to dynamically allocated storage containing the text to be decrypted
 * @return the number of characters in the clear text
 */
int decrypt_password(const char *key, char **password) {
    char *key_clear, *pass_clear;
    size_t key_sz, pass_sz;

    if (key == NULL || password == NULL || !ISVALID(*password)) {
        return AM_EINVAL;
    }

    key_sz = strlen(key);
    pass_sz = strlen(*password);
    if (pass_sz < 2) {
        return AM_EINVAL;
    }

    key_clear = base64_decode(key, &key_sz);
    if (key_clear == NULL) {
        return AM_ENOMEM;
    }

    pass_clear = base64_decode(*password, &pass_sz);
    if (pass_clear == NULL) {
        free(key_clear);
        return AM_ENOMEM;
    }
    free(*password);

    *password = rc4(pass_clear, pass_sz, key_clear, key_sz);
    if (*password == NULL) {
        free(key_clear);
        free(pass_clear);
        return AM_ENOMEM;
    }
    free(key_clear);
    free(pass_clear);
    return (int) pass_sz;
}

/**
 * Call this function to "encrypt" text.  The text should be copied into dynamic memory (do NOT use
 * stack based storage!) and a pointer to that area passed in as the second parameter.  The storage
 * is freed and the pointer reset to yet more dynamically allocated storage containing the encrypted
 * text.
 *
 * @param key The key with which to encrypt the password, this is the result of calling base64_encode
 * @param password A pointer to dynamically allocated storage containing the text to be encrypted
 * @return the number of characters in the encrypted text
 */
int encrypt_password(const char *key, char **password) {
    char *key_clear;
    char *pass_enc, *pass_enc_b64;
    size_t key_sz, pass_sz;

    if (key == NULL || password == NULL || !ISVALID(*password)) {
        return AM_EINVAL;
    }

    key_sz = strlen(key);
    pass_sz = strlen(*password);
    if (pass_sz < 2) {
        return AM_EINVAL;
    }

    key_clear = base64_decode(key, &key_sz);
    if (key_clear == NULL) {
        return AM_ENOMEM;
    }

    pass_enc = rc4(*password, pass_sz, key_clear, key_sz);
    if (pass_enc == NULL) {
        free(key_clear);
        return AM_ENOMEM;
    }

    pass_enc_b64 = base64_encode(pass_enc, &pass_sz);
    if (pass_enc_b64 == NULL) {
        free(key_clear);
        free(pass_enc);
        return AM_ENOMEM;
    }
    AM_FREE(key_clear, pass_enc, *password);
    *password = pass_enc_b64;
    return (int) pass_sz;
}

void decrypt_agent_passwords(am_config_t *r) {
    char *pass;
    int pass_sz;

    if (r == NULL || !ISVALID(r->key)) {
        return;
    }

    if (ISVALID(r->pass)) {
        pass = strdup(r->pass);
        if (pass != NULL && (pass_sz = decrypt_password(r->key, &pass)) > 0) {
            free(r->pass);
            r->pass = pass;
            r->pass_sz = pass_sz;
        } else {
            AM_LOG_WARNING(r->instance_id, "failed to decrypt agent password");
            am_free(pass);
        }
    }

    if (ISVALID(r->cert_key_pass)) {
        pass = strdup(r->cert_key_pass);
        if (pass != NULL && (pass_sz = decrypt_password(r->key, &pass)) > 0) {
            free(r->cert_key_pass);
            r->cert_key_pass = pass;
            r->cert_key_pass_sz = pass_sz;
        } else {
            AM_LOG_WARNING(r->instance_id, "failed to decrypt certificate key password");
            am_free(pass);
        }
    }
    
    if (ISVALID(r->proxy_password)) {
        pass = strdup(r->proxy_password);
        if (pass != NULL && (pass_sz = decrypt_password(r->key, &pass)) > 0) {
            free(r->proxy_password);
            r->proxy_password = pass;
            r->proxy_password_sz = pass_sz;
        } else {
            AM_LOG_WARNING(r->instance_id, "failed to decrypt proxy password");
            am_free(pass);
        }
    }
}

void am_request_free(am_request_t *r) {
    if (r != NULL) {
        AM_FREE(r->normalized_url, r->overridden_url, r->normalized_url_pathinfo,
                r->overridden_url_pathinfo, r->token, r->goto_url,
                r->client_ip, r->client_host, r->post_data, r->post_data_fn,
                r->session_info.s1, r->session_info.si, r->session_info.sk);
        delete_am_policy_result_list(&r->pattr);
        delete_am_namevalue_list(&r->sattr);
    }
}

size_t am_bin_path(char *buffer, size_t len) {
#ifdef _WIN32
    if (GetModuleFileNameA(NULL, buffer, (DWORD) len) != 0) {
        PathRemoveFileSpecA(buffer);
        strcat(buffer, FILE_PATH_SEP);
        return (int) strlen(buffer);
    }
    return AM_ERROR;
#else
    char *path_end;
#ifdef __APPLE__
    uint32_t size = (uint32_t) len;
    if (_NSGetExecutablePath(buffer, &size) != 0) {
        return AM_ENOMEM;
    }
#else
    char path[64];
    snprintf(path, sizeof (path),
#if defined(__sun)
            "/proc/%d/path/a.out"
#elif defined(LINUX)
            "/proc/%d/exe"
#elif defined(AIX)
            "/proc/%d/cwd"
#endif
            , getpid());
    int r = readlink(path, buffer, len);
    if (r <= 0) {
        fprintf(stderr, "readlink error %d\n", errno);
        return AM_ERROR;
    }
#endif
    path_end = strrchr(buffer, '/');
    if (path_end == NULL) {
        return AM_EINVAL;
    }
    ++path_end;
    *path_end = '\0';
    return (path_end - buffer);
#endif
}



#ifdef _WIN32 

int am_delete_directory(const char *path) {
    SHFILEOPSTRUCT file_op;
    int ret, len = (int) strlen(path) + 2; /* required by SHFileOperation */
    char *tempdir = (char *) calloc(1, len);
    if (tempdir == NULL) return AM_ENOMEM;
    strcpy(tempdir, path);

    file_op.hwnd = NULL;
    file_op.wFunc = FO_DELETE;
    file_op.pFrom = tempdir;
    file_op.pTo = NULL;
    file_op.fFlags = FOF_NOCONFIRMATION | FOF_NOERRORUI | FOF_SILENT;
    file_op.fAnyOperationsAborted = FALSE;
    file_op.hNameMappings = NULL;
    file_op.lpszProgressTitle = "";

    ret = SHFileOperation(&file_op);
    free(tempdir);
    return ret;
}
#else

static int delete_directory(const char *path, const struct stat *s, int flag, struct FTW *f) {
    int status;
    int (*rm_func)(const char *);
    switch (flag) {
        default: rm_func = unlink;
            break;
        case FTW_DP: rm_func = rmdir;
    }
    status = rm_func(path);
    return status;
}

int am_delete_directory(const char *path) {
    if (nftw(path, delete_directory, 32, FTW_DEPTH)) {
        return -1;
    }
    return 0;
}
#endif

int am_delete_file(const char *fn) {
    struct stat sb;
    if (stat(fn, &sb) == 0) {
#ifdef _WIN32 
        return am_delete_directory(fn);
#else
        if (S_ISREG(sb.st_mode) || S_ISLNK(sb.st_mode)) {
            return unlink(fn);
        } else if (S_ISDIR(sb.st_mode)) {
            return am_delete_directory(fn);
        }
#endif
    }
    return -1;
}

/**
 * Make a directory, owned by the specifed uid (if not NULL) and group owned
 * by the specified group id (if not NULL).  Note that if uid is not null, then
 * gid is not expected to be null (i.e. its value will also be used).  Note that
 * this is called from admin.c and therefore from the installer.
 *
 * @param path The directory path to create.
 * @param uid Pointer to the user id, null if not available.
 * @param gid Pointer to the group id, null if not available.
 * @param log Pointer to the agentadmin logger, null if not available.
 */
int am_make_path(const char* path, uid_t* uid, gid_t* gid, void (*log)(const char *, ...)) {
#ifdef _WIN32 
    int skip = 0, s = '\\';
    int nmode = _S_IREAD | _S_IWRITE;
#else
    int s = '/';
    int nmode = 0770;
#endif
    int rv;
    char *p = NULL;
    size_t len;
    char *tmp = strdup(path);
    struct stat st;

    if (tmp != NULL) {
        len = strlen(tmp);
        if (tmp[len - 1] == '/' || tmp[len - 1] == '\\') {
            tmp[len - 1] = 0;
        }
        for (p = tmp + 1; *p; p++) {
            if (*p == '/' || *p == '\\') {
                *p = 0;

                /* only do this if the entry does not exist in the file system */
                if (stat(tmp, &st) != 0
#ifdef _WIN32
                        && skip++ > 0  /* on Windows first segment is the drive name - skip it */
#endif           
                        ) {
                    rv = mkdir(tmp, nmode);
                    if (rv != 0 && log != NULL) {
                        log("failed to create directory %s (error: %d)", LOGEMPTY(tmp), errno);
                    }
#ifndef _WIN32
                    if (uid != NULL) {
                        rv = chown(tmp, *uid, *gid);
                        if (rv != 0 && log != NULL) {
                            log("failed to change directory %s owner to %d:%d (error: %d)",
                                    LOGEMPTY(tmp), *uid, *gid, errno);
                        }
                    }
#endif
                }
                *p = s;
            }
        }

        /* again, only do this if the entry does not exist */
        if (stat(tmp, &st) != 0) {
            rv = mkdir(tmp, nmode);
            if (rv != 0 && log != NULL) {
                log("failed to create directory %s (error: %d)", LOGEMPTY(tmp), errno);
            }
#ifndef _WIN32
            if (uid != NULL) {
                rv = chown(tmp, *uid, *gid);
                if (rv != 0 && log != NULL) {
                    log("failed to change directory %s owner to %d:%d (error: %d)",
                            LOGEMPTY(tmp), *uid, *gid, errno);
                }
            }
#endif
        }
        free(tmp);
    }
    return 0;
}

#ifdef _WIN32 

static DIR *opendir(const char *dir) {
    DIR *dp;
    char *filespec;
    intptr_t handle;
    int index;

    if (dir == NULL) return NULL;
    filespec = malloc(strlen(dir) + 2 + 1);
    if (filespec == NULL) return NULL;

    strcpy(filespec, dir);
    index = (int) strlen(filespec) - 1;
    if (index >= 0 && (filespec[index] == '/' || filespec[index] == '\\')) {
        filespec[index] = '\0';
    }
    strcat(filespec, "\\*");

    dp = (DIR *) malloc(sizeof (DIR));
    if (dp == NULL) {
        free(filespec);
        return NULL;
    }

    dp->offset = 0;
    dp->finished = 0;
    dp->dir = strdup(dir);
    if (dp->dir == NULL) {
        free(dp);
        free(filespec);
        return NULL;
    }

    if ((handle = _findfirst(filespec, &(dp->fileinfo))) < 0) {
        if (errno == ENOENT) {
            dp->finished = 1;
        } else {
            free(dp->dir);
            free(dp);
            free(filespec);
            return NULL;
        }
    }
    dp->handle = handle;
    free(filespec);
    return dp;
}

static int closedir(DIR *dp) {
    if (dp != NULL) {
        _findclose(dp->handle);
        am_free(dp->dir);
        free(dp);
    }
    return 0;
}

struct dirent *readdir(DIR *dp) {
    if (!dp || dp->finished)
        return NULL;

    if (dp->offset != 0) {
        if (_findnext(dp->handle, &(dp->fileinfo)) < 0) {
            dp->finished = 1;
            return NULL;
        }
    }
    dp->offset++;
    strncpy(dp->dent.d_name, dp->fileinfo.name, AM_URI_SIZE);
    dp->dent.d_type = 0;
    if (dp->fileinfo.attrib & _A_SUBDIR) {
        dp->dent.d_type = 1;
    }
    dp->dent.d_ino = 1;
    dp->dent.d_reclen = (unsigned short) strlen(dp->dent.d_name);
    dp->dent.d_off = dp->offset;
    return &(dp->dent);
}

static int readdir_r(DIR *dp, struct dirent *entry, struct dirent **result) {
    if (!dp || dp->finished) {
        *result = NULL;
        return 0;
    }

    if (dp->offset != 0) {
        if (_findnext(dp->handle, &(dp->fileinfo)) < 0) {
            dp->finished = 1;
            *result = NULL;
            return 0;
        }
    }
    dp->offset++;
    strncpy(dp->dent.d_name, dp->fileinfo.name, AM_URI_SIZE);
    dp->dent.d_type = 0;
    if (dp->fileinfo.attrib & _A_SUBDIR) {
        dp->dent.d_type = 1;
    }
    dp->dent.d_ino = 1;
    dp->dent.d_reclen = (unsigned short) strlen(dp->dent.d_name);
    dp->dent.d_off = dp->offset;
    memcpy(entry, &dp->dent, sizeof (*entry));
    *result = &dp->dent;
    return 0;
}

#endif /* _WIN32 */

static int am_alphasort(const struct dirent **_a, const struct dirent **_b) {
    struct dirent **a = (struct dirent **) _a;
    struct dirent **b = (struct dirent **) _b;
    int a_idx = atoi((*a)->d_name + 6); /* am_file_filter allows only 'agent_XYZ' file names here */
    int b_idx = atoi((*b)->d_name + 6);
    return a_idx == b_idx ? 0 : (a_idx > b_idx ? 1 : -1);
}

static int am_file_filter(const struct dirent *_a) {
    return (strncasecmp(_a->d_name, "agent_", 6) == 0);
}

static int am_scandir(const char *dirname, struct dirent ***ret_namelist,
        int (*select)(const struct dirent *),
        int (*compar)(const struct dirent **, const struct dirent **)) {
    int len, used, allocated, i;
    DIR *dir;
    struct dirent *ent, *ent2, *dirbuf;
    struct dirent **namelist = NULL;
    struct dirent **namelist_tmp;
    if ((dir = opendir(dirname)) == NULL) {
        return AM_EINVAL;
    }
    used = 0;
    allocated = 2;
    namelist = malloc(allocated * sizeof (struct dirent *));
    if (namelist == NULL) {
        closedir(dir);
        return AM_ENOMEM;
    }
    dirbuf = malloc(sizeof (struct dirent) + 255 + 1);
    if (dirbuf == NULL) {
        free(namelist);
        closedir(dir);
        return AM_ENOMEM;
    }
    while (readdir_r(dir, dirbuf, &ent) == 0 && ent) {
        if (strcmp(ent->d_name, ".") == 0 || strcmp(ent->d_name, "..") == 0)
            continue;
        if (select != NULL && !select(ent))
            continue;
        len = offsetof(struct dirent, d_name) + (int) strlen(ent->d_name) + 1;
        if ((ent2 = malloc(len)) == NULL) {
            for (i = 0; i < used; i++) {
                am_free(namelist[i]);
            }
            AM_FREE(namelist, dirbuf);
            closedir(dir);
            return AM_ENOMEM;
        }
        if (used >= allocated) {
            allocated *= 2;
            namelist_tmp = realloc(namelist, allocated * sizeof (struct dirent *));
            if (namelist_tmp == NULL) {
                for (i = 0; i < used; i++) {
                    am_free(namelist[i]);
                }
                AM_FREE(namelist, dirbuf);
                closedir(dir);
                return AM_ENOMEM;
            } else {
                namelist = namelist_tmp;
            }
        }
        memcpy(ent2, ent, len);
        namelist[used++] = ent2;
    }
    free(dirbuf);
    closedir(dir);
    if (compar) {
        qsort(namelist, used, sizeof (struct dirent *),
                (int (*)(const void *, const void *)) compar);
    }
    *ret_namelist = namelist;
    return used;
}

/**
 * Create a bunch of agent directories, chown them and chgrp them if we're given that info.
 *
 * @param sep
 * @param path
 * @param created_name
 * @param created_name_simple
 * @param uid The user id who will own the directory, if not NULL
 * @param gid The group id which will own the directory, if not NULL
 * @param log Pointer to the agentadmin logger, NULL if not available
 */
int am_create_agent_dir(const char* sep, const char* path,
        char** created_name, char** created_name_simple,
        uid_t* uid, gid_t* gid, void (*log)(const char *, ...)) {

    struct dirent** instlist = NULL;
    int i, n, result = AM_ERROR, idx = 0;
    char* p = NULL;

    if ((n = am_scandir(path, &instlist, am_file_filter, am_alphasort)) <= 0) {

        /* report back an agent instance path and a configuration name */
        if (created_name != NULL) {
            am_asprintf(created_name, "%s%sagent_1", path, sep);
        }
        if (created_name != NULL && *created_name == NULL) {
            return AM_ENOMEM;
        }
        if (created_name_simple != NULL) {
            am_asprintf(created_name_simple, "agent_1");
        }
        if (created_name_simple != NULL && *created_name_simple == NULL) {
            free(*created_name);
            *created_name = NULL;
            return AM_ENOMEM;
        }

        /* create directory structure */
        if (created_name != NULL) {
            result = am_make_path(*created_name, uid, gid, log);
        }
        am_asprintf(&p, "%s%sagent_1%sconfig", path, sep, sep);
        if (p == NULL) {
            AM_FREE(*created_name, *created_name_simple);
            *created_name = *created_name_simple = NULL;
            return AM_ENOMEM;
        }
        result = am_make_path(p, uid, gid, log);
        free(p);
        p = NULL;
        am_asprintf(&p, "%s%sagent_1%slogs%sdebug", path, sep, sep, sep);
        if (p == NULL) {
            AM_FREE(*created_name, *created_name_simple);
            *created_name = *created_name_simple = NULL;
            return AM_ENOMEM;
        }
        result = am_make_path(p, uid, gid, log);
        free(p);
        p = NULL;
        am_asprintf(&p, "%s%sagent_1%slogs%saudit", path, sep, sep, sep);
        if (p == NULL) {
            AM_FREE(*created_name, *created_name_simple);
            *created_name = *created_name_simple = NULL;
            return AM_ENOMEM;
        }
        result = am_make_path(p, uid, gid, log);
        free(p);
        am_free(instlist);

        return result;
    }

    /* the same as above, but there is an agent_x directory already */
    for (i = 0; i < n; i++) {
        if (i == n - 1) {
            char* id = strstr(instlist[i]->d_name, "_");
            if (id != NULL && (idx = atoi(id + 1)) > 0) {
                if (created_name != NULL) {
                    am_asprintf(created_name, "%s%sagent_%d", path, sep, idx + 1);
                }
                if (created_name != NULL && *created_name == NULL) {
                    return AM_ENOMEM;
                }
                if (created_name_simple != NULL) {
                    am_asprintf(created_name_simple, "agent_%d", idx + 1);
                }
                if (created_name_simple != NULL && *created_name_simple == NULL) {
                    free(*created_name);
                    *created_name = NULL;
                    return AM_ENOMEM;
                }
                if (created_name != NULL) {
                    result = am_make_path(*created_name, uid, gid, log);
                }
                am_asprintf(&p, "%s%sagent_%d%sconfig", path, sep, idx + 1, sep);
                if (p == NULL) {
                    AM_FREE(*created_name, *created_name_simple);
                    *created_name = *created_name_simple = NULL;
                    return AM_ENOMEM;
                }
                result = am_make_path(p, uid, gid, log);
                free(p);
                p = NULL;
                am_asprintf(&p, "%s%sagent_%d%slogs%sdebug", path, sep, idx + 1, sep, sep);
                if (p == NULL) {
                    AM_FREE(*created_name, *created_name_simple);
                    *created_name = *created_name_simple = NULL;
                    return AM_ENOMEM;
                }
                result = am_make_path(p, uid, gid, log);
                free(p);
                p = NULL;
                am_asprintf(&p, "%s%sagent_%d%slogs%saudit", path, sep, idx + 1, sep, sep);
                if (p == NULL) {
                    AM_FREE(*created_name, *created_name_simple);
                    *created_name = *created_name_simple = NULL;
                    return AM_ENOMEM;
                }
                result = am_make_path(p, uid, gid, log);
                free(p);
                p = NULL;
            }
        }
        free(instlist[i]);
    }
    free(instlist);

    return result;
}

int string_replace(char **original, const char *pattern, const char *replace, size_t *sz) {
    size_t pcnt = 0;
    size_t replace_sz, pattern_sz, new_sz, e;
    char *p, *new_str;
    
    if (original == NULL || *original == NULL || pattern == NULL
            || replace == NULL || sz == NULL) {
        return AM_EINVAL;
    }
    
    pattern_sz = strlen(pattern);
    replace_sz = strlen(replace);
    
    // strstr requires this and an empty pattern is nonsense
    if (pattern_sz == 0) {
        return AM_NOT_FOUND;
    }
    
    // count number of times pattern occurs
    for (p = * original; ( p = strstr(p, pattern) ); p += pattern_sz) {
        pcnt++;
    }
    if (pcnt == 0) {
        return AM_NOT_FOUND;
    }
    
    // reallocate only if replacement is larger than pattern
    new_sz = *sz + pcnt * (replace_sz - pattern_sz);
    if (*sz < new_sz) {
        new_str = realloc(*original, new_sz + 1);
        if (new_str == NULL) {
            free(*original);
            return AM_ENOMEM;
        }
    } else {
        new_str = *original;
    }
    
    // step through patterns, shifting from end of pattern and inserting replacement
    e = (*sz) + 1;
    for (p = new_str; ( p = strstr(p, pattern) ); p += replace_sz, e += (replace_sz - pattern_sz)) {
        char *src = p + pattern_sz, *dest = p + replace_sz;
        memmove(dest, src, e - (src - new_str));
        memcpy(p, replace, replace_sz);
    }
    
    // set return values
    *sz = new_sz;
    *original = new_str;
    
    return AM_SUCCESS;
}

int copy_file(const char *from, const char *to) {
    int rv = AM_FILE_ERROR;
    am_bool_t local_alloc = AM_FALSE;
    char *to_tmp = NULL;

    if (!ISVALID(from)) {
        return AM_EINVAL;
    }
    if (!ISVALID(to)) {
        /* 'to' is not provided - do a copy of 'from' with a timestamped name */
        char tm[64];
        struct tm now;
        time_t tv = time(NULL);
        localtime_r(&tv, &now);
        strftime(tm, sizeof (tm) - 1, "%Y%m%d%H%M%S", &now);
        am_asprintf(&to_tmp, "%s_amagent_%s", from, tm);
        if (to_tmp == NULL) {
            return AM_ENOMEM;
        }
        local_alloc = AM_TRUE;
    } else {
        to_tmp = (char *) to;
    }
#ifdef _WIN32
    if (CopyFileExA(from, to_tmp, NULL, NULL, FALSE, COPY_FILE_NO_BUFFERING) != 0) {
        rv = AM_SUCCESS;
    }
#else
    {
        size_t content_sz = 0;
        char *content = load_file(from, &content_sz);
        if (content == NULL) {
            rv = AM_FILE_ERROR;
        } else {
            ssize_t wr_status = write_file(to_tmp, content, content_sz);
            am_free(content);

            if (wr_status == content_sz)
                rv = AM_SUCCESS;
            else if (wr_status < 0)
                rv = wr_status;
            else
                rv = AM_FILE_ERROR;
        }
    }
#endif 
    if (local_alloc) {
        free(to_tmp);
    }
    return rv;
}

void read_directory(const char *path, struct am_namevalue **list) {
    DIR *d;
    char npath[AM_URI_SIZE];
    struct stat s;

    if ((d = opendir(path)) != NULL) {
        while (1) {
            struct dirent *e = readdir(d);
            if (e == NULL) {
                break;
            }
            snprintf(npath, sizeof (npath), "%s/%s", path, e->d_name);
            if (stat(npath, &s) == -1) {
                break;
            }

            if (strcmp(e->d_name, "..") != 0 && strcmp(e->d_name, ".") != 0) {
                struct am_namevalue *el = calloc(1, sizeof (struct am_namevalue));
                if (el == NULL) {
                    break;
                }
                el->ns = S_ISDIR(s.st_mode);
                am_asprintf(&el->n, el->ns ? "%s/%s/" : "%s/%s", path, e->d_name);
                el->v = NULL;
                el->next = NULL;
                AM_LIST_INSERT(*list, el);
            }

            if (S_ISDIR(s.st_mode) && strcmp(e->d_name, "..") != 0 && strcmp(e->d_name, ".") != 0) {
                read_directory(npath, list);
            }
        }
        closedir(d);
    } else {
        if (errno == ENOTDIR) {
            /* not a directory - add to the list as a file */
            struct am_namevalue *el = calloc(1, sizeof (struct am_namevalue));
            if (el != NULL) {
                el->ns = 0;
                el->n = strdup(path);
                el->v = NULL;
                el->next = NULL;
                AM_LIST_INSERT(*list, el);
            }
        }
    }
}

int get_ttl_value(struct am_namevalue *session, const char *name, int def, int value_in_minutes) {
    struct am_namevalue *element, *tmp;
    int result;

    AM_LIST_FOR_EACH(session, element, tmp) {
        if (strcmp(element->n, name) == 0) {
            errno = 0;
            result = (int) strtol(element->v, NULL, AM_BASE_TEN);
            if (result < 0 || errno == ERANGE) {
                break;
            }
            return value_in_minutes ? result * 60 : result;
        }
    }
    return def < 0 ? -(def) : def;
}

/**
 * Memcpy twice into the target destination INCLUDING null terminators.
 *
 * @param dest The destination buffer which MUST BE AT LEAST size1 + size2 + 2 bytes
 * @param source1 The first source string
 * @param size1 bytes to copy from source1
 * @param source2 The second source string
 * @param size2 bytes to copy from source2
 * @param source3 The third source string
 * @param size3 bytes to copy from source3
 * @return dest
 */
void* mem2cpy(void* dest, const void* source1, size_t size1, const void* source2, size_t size2) {

    char* d = dest;

    memcpy(d, source1, size1);
    d[size1] = '\0';

    memcpy(d + size1 + 1, source2, size2);
    d[size1 + size2 + 1] = '\0';

    return dest;
}

/**
 * Memcpy three times into the target destination, INCLUDING null terminators.
 *
 * @param dest The destination buffer which MUST BE AT LEAST size1 + size2 + size3 + 3 bytes
 * @param source1 The first source string
 * @param size1 bytes to copy from source1
 * @param source2 The second source string
 * @param size2 bytes to copy from source2
 * @param source3 The third source string
 * @param size3 bytes to copy from source3
 * @return dest
 */
void* mem3cpy(void* dest, const void* source1, size_t size1,
        const void* source2, size_t size2,
        const void* source3, size_t size3) {

    char* d = dest;

    memcpy(d, source1, size1);
    d[size1] = '\0';

    memcpy(d + size1 + 1, source2, size2);
    d[size1 + size2 + 1] = '\0';

    memcpy(d + size1 + size2 + 2, source3, size3);
    d[size1 + size2 + size3 + 2] = '\0';

    return dest;
}

char *am_json_escape(const char *str, size_t *escaped_sz) {
    char *data = NULL;
    const char *end;
    size_t len = 0;
    int err = 0;

    if (str == NULL) {
        return NULL;
    }

    err = concat(&data, &len, "\"", 1);
    if (err) {
        am_free(data);
        return NULL;
    }

    end = str;
    while (1) {
        const char *text;
        char seq[7];
        int length;

        while (*end && *end != '\\' && *end != '"' && *end != '/' &&
                (unsigned char) *end > 0x1F) {
            end++;
        }
        if (end != str) {
            err = concat(&data, &len, str, end - str);
            if (err) {
                am_free(data);
                return NULL;
            }
        }
        if (!*end) {
            break;
        }

        /* handle \, /, ", and control codes */
        length = 2;
        switch (*end) {
            case '/': text = "\\/";
                break;
            case '\\': text = "\\\\";
                break;
            case '\"': text = "\\\"";
                break;
            case '\b': text = "\\b";
                break;
            case '\f': text = "\\f";
                break;
            case '\n': text = "\\n";
                break;
            case '\r': text = "\\r";
                break;
            case '\t': text = "\\t";
                break;
            default:
            {
                snprintf(seq, sizeof (seq), "\\u%04X", *end);
                text = seq;
                length = 6;
                break;
            }
        }

        err = concat(&data, &len, text, length);
        if (err) {
            am_free(data);
            return NULL;
        }
        end++;
        str = end;
    }

    err = concat(&data, &len, "\"", 1);
    if (err) {
        am_free(data);
        return NULL;
    }

    if (escaped_sz) {
        *escaped_sz = len;
    }
    return data;
}

/**
 * Convert and update (to seconds) agent configuration parameter values 
 * set in minutes (legacy).
 *
 * @param conf The pointer to am_config_t instance
 */
void update_agent_configuration_ttl(am_config_t *conf) {
    if (conf == NULL) {
        return;
    }

#define UPDATE_VALUE_TO_SEC(v) (v) = (v) > 0 ? (v) * 60 : v

    /* com.sun.identity.agents.config.polling.interval */
    UPDATE_VALUE_TO_SEC(conf->config_valid);
    
    /* com.sun.identity.agents.config.policy.cache.polling.interval */
    UPDATE_VALUE_TO_SEC(conf->policy_cache_valid);
    
    /* com.sun.identity.agents.config.sso.cache.polling.interval */
    UPDATE_VALUE_TO_SEC(conf->token_cache_valid);
    
    /* com.sun.identity.agents.config.postcache.entry.lifetime */
    UPDATE_VALUE_TO_SEC(conf->pdp_cache_valid);
}

void update_agent_configuration_audit(am_config_t *conf) {
    int value;

    if (conf == NULL) {
        return;
    }

    if (AM_BITMASK_CHECK(conf->audit_level, (AM_LOG_LEVEL_AUDIT_ALLOW | AM_LOG_LEVEL_AUDIT_DENY))) {

        value = conf->audit_level;

        if (ISINVALID(conf->audit_file_disposition) || strcasecmp(conf->audit_file_disposition, "LOCAL") == 0) {
            value |= AM_LOG_LEVEL_AUDIT;
        } else if (strcasecmp(conf->audit_file_disposition, "REMOTE") == 0) {
            value |= AM_LOG_LEVEL_AUDIT_REMOTE;
        } else {
            value |= AM_LOG_LEVEL_AUDIT;
            value |= AM_LOG_LEVEL_AUDIT_REMOTE;
        }

        conf->audit_level = value;
    }
}

/*
 * change the value in a configuration mapping.
 * NOTE: the name and value are allocated in the same block, and only the name should be
 * freed.
 */
am_status_t remap_config_value(am_config_map_t * mapping, char *newvalue) {
    char *buffer = NULL;
    size_t name_sz;
    
    if (!ISVALID(mapping->name)) {
        return AM_EINVAL;
    }
    name_sz = strlen(mapping->name);
    buffer = realloc(mapping->name, name_sz + 1 + strlen(newvalue) + 1);
    if (!ISVALID(buffer)) {
        return AM_ENOMEM;
    }
    strcpy(buffer + name_sz + 1, newvalue);
    
    mapping->name = buffer;
    mapping->value = buffer + name_sz + 1;
    return AM_SUCCESS;
}

void update_agent_configuration_normalise_map_urls(am_config_t *conf) {
    static const char *thisfunc = "update_agent_configuration_normalise_map_urls()";

    int i;
    char *value, *newvalue;
    am_status_t remap_status;

    if (conf == NULL) {
        return;
    }
    
    /* normalise not enforced map values if they are not regular expressions */
    if (!conf->not_enforced_regex_enable) {
        for (i = 0; i < conf->not_enforced_map_sz; i++) {
            if ( (value = conf->not_enforced_map[i].value) ) {
                if ( (newvalue = am_normalize_pattern(value)) ) {
                    remap_status = remap_config_value(conf->not_enforced_map + i, newvalue);
                    am_free(newvalue);
                    if (remap_status != AM_SUCCESS)
                        AM_LOG_WARNING(conf->instance_id, "%s error normalising not enforced URL %s (%s)", thisfunc, value, am_strerror(remap_status));
                }
            }
        }
    }
    /* normalise not enforced extended map values if they are not regular expressions */
    if (!conf->not_enforced_ext_regex_enable) {
        for (i = 0; i < conf->not_enforced_ext_map_sz; i++) {
            if ( (value = conf->not_enforced_ext_map[i].value) ) {
                if ( (newvalue = am_normalize_pattern(value)) ) {
                    remap_status = remap_config_value(conf->not_enforced_ext_map + i, newvalue);
                    am_free(newvalue);
                    if (remap_status != AM_SUCCESS)
                        AM_LOG_WARNING(conf->instance_id, "%s error normalising extended not enforced URL %s (%s)", thisfunc, value, am_strerror(remap_status));
                }
            }
        }
    }
    /* normalise logout map values if they are not regular expressions */
    if (!conf->logout_regex_enable) {
        for (i = 0; i < conf->logout_map_sz; i++) {
            if ( (value = conf->logout_map[i].value) ) {
                if ( (newvalue = am_normalize_pattern(value)) ) {
                    remap_status = remap_config_value(conf->logout_map + i, newvalue);
                    am_free(newvalue);
                    if (remap_status != AM_SUCCESS)
                        AM_LOG_WARNING(conf->instance_id, "%s error normalising logout URL %s (%s)", thisfunc, value, am_strerror(remap_status));
                }
            }
        }
    }
    /* normalise json URL map values */
    for (i = 0; i < conf->json_url_map_sz; i++) {
        if ( (value = conf->json_url_map[i].value) ) {
            if ( (newvalue = am_normalize_pattern(value)) ) {
                remap_status = remap_config_value(conf->json_url_map + i, newvalue);
                am_free(newvalue);
                if (remap_status != AM_SUCCESS)
                    AM_LOG_WARNING(conf->instance_id, "%s error normalising JSON URL %s (%s)", thisfunc, value, am_strerror(remap_status));
            }
        }
    }
    /* normalise skip-post URL map values */
    for (i = 0; i < conf->skip_post_url_map_sz; i++) {
        if ( (value = conf->skip_post_url_map[i].value) ) {
            if ( (newvalue = am_normalize_pattern(value)) ) {
                remap_status = remap_config_value(conf->skip_post_url_map + i, newvalue);
                am_free(newvalue);
                if (remap_status != AM_SUCCESS)
                    AM_LOG_WARNING(conf->instance_id, "%s error normalising SKIP-POST URL %s (%s)", thisfunc, value, am_strerror(remap_status));
            }
        }
    }
}

static int config_map_name_compare(const void *a, const void *b) {
    int index_a = (int) strtol(((am_config_map_t *) a)->name, NULL, AM_BASE_TEN);
    int index_b = (int) strtol(((am_config_map_t *) b)->name, NULL, AM_BASE_TEN);
    return (index_a > index_b) - (index_a < index_b);
}

void update_agent_configuration_reorder_map_values(am_config_t *conf) {
    if (conf == NULL) {
        return;
    }
    if (conf->login_url_sz > 1 && conf->login_url != NULL) {
        qsort(conf->login_url, conf->login_url_sz,
                sizeof (am_config_map_t), config_map_name_compare);
    }
    if (conf->cdsso_login_map_sz > 1 && conf->cdsso_login_map != NULL) {
        qsort(conf->cdsso_login_map, conf->cdsso_login_map_sz,
                sizeof (am_config_map_t), config_map_name_compare);
    }
    if (conf->openam_logout_map_sz > 1 && conf->openam_logout_map != NULL) {
        qsort(conf->openam_logout_map, conf->openam_logout_map_sz,
                sizeof (am_config_map_t), config_map_name_compare);
    }
    if (conf->cond_login_url_sz > 1 && conf->cond_login_url != NULL) {
        qsort(conf->cond_login_url, conf->cond_login_url_sz,
                sizeof (am_config_map_t), config_map_name_compare);
    }
}

static uint32_t sdbm_hash(const void *s) {
    uint64_t hash = 0;
    int c;
    const unsigned char *str = (const unsigned char *) s;
    while ((c = *str++)) {
        hash = c + (hash << 6) + (hash << 16) - hash;
    }
    return (uint32_t) hash;
}

uint32_t am_hash(const void *k) {
    uint32_t i = sdbm_hash(k);
    i += ~(i << 9);
    i ^= ((i >> 14) | (i << 18));
    i += (i << 4);
    i ^= ((i >> 10) | (i << 22));
    return i;
}

uint32_t am_hash_buffer(const void *k, size_t sz) {
    void *tmp;
    uint32_t hash;
    if (k == NULL || sz == 0) {
        return 0;
    }
    tmp = calloc(1, sz + 1);
    if (tmp == NULL) {
        return 0;
    }
    memcpy(tmp, k, sz);
    hash = am_hash(tmp);
    free(tmp);
    return hash;
}


am_bool_t validate_directory_access(const char *path, int mask) {
    am_bool_t ret = AM_FALSE;
#ifdef _WIN32
    PRIVILEGE_SET privileges = {0};
    DWORD length = 0, granted_access = 0, privileges_length = sizeof (privileges);
    PSECURITY_DESCRIPTOR security = NULL;
    HANDLE token = NULL, imp_token = NULL;
    GENERIC_MAPPING mapping = {0xFFFFFFFF};
    BOOL result = FALSE;

    if (!GetFileSecurityA(path, OWNER_SECURITY_INFORMATION | GROUP_SECURITY_INFORMATION
            | DACL_SECURITY_INFORMATION, NULL, 0, &length) &&
            GetLastError() == ERROR_INSUFFICIENT_BUFFER) {
        security = (PSECURITY_DESCRIPTOR) LocalAlloc(LPTR, length);
    }

    if (security == NULL) {
        return AM_FALSE;
    }

    if (!GetFileSecurityA(path, OWNER_SECURITY_INFORMATION | GROUP_SECURITY_INFORMATION
            | DACL_SECURITY_INFORMATION, security, length, &length) ||
            !OpenProcessToken(GetCurrentProcess(), TOKEN_IMPERSONATE | TOKEN_QUERY |
            TOKEN_DUPLICATE | STANDARD_RIGHTS_READ, &token)) {
        LocalFree(security);
        return AM_FALSE;
    }

    if (!DuplicateToken(token, SecurityImpersonation, &imp_token)) {
        CloseHandle(token);
        LocalFree(security);
        return AM_FALSE;
    }

    mapping.GenericRead = FILE_GENERIC_READ;
    mapping.GenericWrite = FILE_GENERIC_WRITE;
    mapping.GenericExecute = FILE_GENERIC_EXECUTE;
    mapping.GenericAll = FILE_ALL_ACCESS;
    MapGenericMask(&mask, &mapping);

    if (AccessCheck(security, imp_token, mask,
            &mapping, &privileges, &privileges_length, &granted_access, &result)) {
        ret = (result == TRUE);
    }

    CloseHandle(imp_token);
    CloseHandle(token);
    LocalFree(security);
#endif
    return ret;
}
/**
 * Test if input value contains any ASCII control characters.
 *
 * @param string The incoming string.
 * @return AM_TRUE if string contains CTL, AM_FALSE otherwise.
 */
am_bool_t contains_ctl(const char *string) {
    int j;
    if (string != NULL) {
        for (j = 0; string[j] != '\0'; j++) {
            if (iscntrl(string[j])) {
                return AM_TRUE;
            }
        }
    }
    return AM_FALSE;
}
