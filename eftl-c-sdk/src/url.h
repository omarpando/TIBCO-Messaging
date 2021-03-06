/*
 * Copyright (c) $Date: 2018-04-24 10:57:05 -0500 (Tue, 24 Apr 2018) $ TIBCO Software Inc.
 * Licensed under a BSD-style license. Refer to [LICENSE]
 * For more information, please contact:
 * TIBCO Software Inc., Palo Alto, California, USA
 *
 * $Id: url.h 100982 2018-04-24 15:57:05Z bpeterse $
 *
 */

#ifndef INCLUDED_TIBEFTL_URL_H
#define INCLUDED_TIBEFTL_URL_H

/*
 * A URL has the following syntax:
 *
 *     scheme://[username[:password]@]host[:port][?query]
 *
 * Where query can be key/value pairs ("key=value") or just a 
 * key. Queries are separated from one another by either a semicolon (;)
 * or ampersand (&).
 *
 * For example:
 *
 *     tcp://10.101.2.140:7222?listen&backlog_size=64mb
 */

typedef struct url_s url_t;

typedef struct url_query_s url_query_t;

url_t*
url_parse(
    const char* str);

void
url_destroy(
    url_t*      url);

const char*
url_scheme(
    url_t*      url);

const char*
url_host(
    url_t*      url);

const char*
url_port(
    url_t*      url);

const char*
url_path(
    url_t*      url);

const char*
url_username(
    url_t*      url);

const char*
url_password(
    url_t*      url);

int
url_is_secure(
    url_t*      url);

const char*
url_query(
    url_t*      url,
    const char* key);

url_query_t*
url_query_list(
    url_t*      url);

const char*
url_query_key(
    url_query_t *query);

const char*
url_query_value(
    url_query_t *query);

url_query_t*
url_query_next(
    url_query_t *query);

#endif /* INCLUDED_TIBEFTL_URL_H */
