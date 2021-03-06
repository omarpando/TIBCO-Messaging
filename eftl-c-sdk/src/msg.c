/*
 * Copyright (c) 2001-$Date: 2017-10-30 16:57:04 -0500 (Mon, 30 Oct 2017) $ TIBCO Software Inc.
 * Licensed under a BSD-style license. Refer to [LICENSE]
 * For more information, please contact:
 * TIBCO Software Inc., Palo Alto, California, USA
 *
 * $Id: msg.c 97172 2017-10-30 21:57:04Z bpeterse $
 *
 */

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <math.h>

#include "hashmap.h"
#include "base64.h"
#include "eftl.h"
#include "msg.h"

#if defined(_WIN32)
#define strcasecmp  _stricmp
#endif

#if defined(_WIN32)
#include <float.h>
#define isNaN(x)    (_isnan(x))
#define isFinite(x) (_finite(x))
#define isPosInf(x) (!_finite(x) && x>0)
#define isNegInf(x) (!_finite(x) && x<0)
#else
#define isNaN(x)    (isnan(x))
#define isFinite(x) (isfinite(x))
#define isPosInf(x) (!isfinite(x) && x>0)
#define isNegInf(x) (!isfinite(x) && x<0)
#endif

typedef struct tibeftlCacheStruct
{
    tibeftlFieldType            type;
    void*                       value;
    int                         size;

} tibeftlCacheRec, *tibeftlCache;

struct tibeftlMessageStruct
{
    _cJSON*                     json;
    _cJSON*                     iter;
    hashmap_t*                  cache;
};

static void
_deleteCache(
    const char*                 key,
    void*                       value,
    void*                       context)
{
    tibeftlCache                cache = (tibeftlCache)value;
    int                         i;

    switch (cache->type)
    {
    case TIBEFTL_FIELD_TYPE_MESSAGE:
        tibeftlMessage_Destroy(NULL, (tibeftlMessage)cache->value);
        break;
    case TIBEFTL_FIELD_TYPE_MESSAGE_ARRAY:
        for (i = 0; i < cache->size; i++)
        {
            tibeftlMessage_Destroy(NULL, ((tibeftlMessage*)cache->value)[i]);
        }
        free(cache->value);
        break;
    default:
        free(cache->value);
        break;
    }

    free(cache);
}

static void
tibeftlMessage_clearCache(
    tibeftlMessage              msg)
{
    if (!msg->cache)
        return;

    hashmap_iterate(msg->cache, _deleteCache, NULL);
    hashmap_destroy(msg->cache);

    msg->cache = NULL;
}

static void
tibeftlMessage_removeCache(
    tibeftlMessage              msg,
    const char*                 field)
{
    tibeftlCache                cache;

    if (!msg->cache)
        return;

    cache = hashmap_remove(msg->cache, field);

    if (cache)
    	_deleteCache(field, cache, NULL);
}

static void
tibeftlMessage_addCache(
    tibeftlMessage              msg,
    tibeftlFieldType            type,
    const char*                 field,
    void*                       value,
    int                         size)
{
    tibeftlCache                cache, existing;

    if (!msg->cache)
        msg->cache = hashmap_create(8);

    cache = malloc(sizeof(struct tibeftlCacheStruct));

    cache->type = type;
    cache->value = value;
    cache->size = size;

    if ((existing = hashmap_put(msg->cache, field, cache)))
        _deleteCache(field, existing, NULL);
}

static void*
tibeftlMessage_getCache(
    tibeftlMessage              msg,
    tibeftlFieldType            type,
    const char*                 field,
    int*                        size)
{
    tibeftlCache                cache;

    if (!msg->cache)
        return NULL;

    cache = hashmap_get(msg->cache, field);

    if (!cache || cache->type != type)
        return NULL;

    if (size)
        *size = cache->size;

    return cache->value;
}

static void
_cJSON_AddDoubleToObject(
    _cJSON*                     obj,
    const char*                 str,
    double                      val)
{
    if (isNaN(val))
        _cJSON_AddStringToObject(obj, str, "NaN");
    else if (isPosInf(val))
        _cJSON_AddStringToObject(obj, str, "Infinity");
    else if (isNegInf(val))
        _cJSON_AddStringToObject(obj, str, "-Infinity");
    else
        _cJSON_AddNumberToObject(obj, str, val);
}

static _cJSON_bool
_cJSON_IsDouble(
    _cJSON*                     obj)
{
    if (_cJSON_IsNumber(obj))
    {
        return true;
    }
    else if (_cJSON_IsString(obj))
    {
        if (strcasecmp(obj->valuestring, "nan") == 0 ||
            strcasecmp(obj->valuestring, "infinity") == 0 ||
            strcasecmp(obj->valuestring, "-infinity") == 0)
        {
            return true;
        }
    }
    return false;
}

static double
_cJSON_GetDouble(
    _cJSON*                     obj)
{
    double                      val = 0;

    if (_cJSON_IsNumber(obj))
    {
        val = obj->valuedouble;
    }
    else if (_cJSON_IsString(obj))
    {
        if (strcasecmp(obj->valuestring, "nan") == 0)
        {
            val = NAN;
        }
        else if (strcasecmp(obj->valuestring, "infinity") == 0)
        {
            val = INFINITY;
        }
        else if (strcasecmp(obj->valuestring, "-infinity") == 0)
        {
            val = -INFINITY;
        }
    }
    return val;
}

tibeftlMessage
tibeftlMessage_CreateWithJSON(
    _cJSON*                     json)
{
    tibeftlMessage              msg;

    msg = calloc(1, sizeof(struct tibeftlMessageStruct));
    if (!msg)
    {
        return NULL;
    }

    msg->json = (json ? json : _cJSON_CreateObject());
    if (!msg->json)
    {
        free(msg);
        return NULL;
    }

    return msg;
}

_cJSON*
tibeftlMessage_GetJSON(
    tibeftlMessage              msg)
{
    if (!msg)
        return NULL;

    return msg->json;
}

tibeftlMessage
tibeftlMessage_Create(
    tibeftlErr                  err)
{
    tibeftlMessage              msg;

    if (tibeftlErr_IsSet(err))
        return NULL;

    msg = calloc(1, sizeof(struct tibeftlMessageStruct));
    if (!msg)
    {
        tibeftlErr_Set(err, TIBEFTL_ERR_NO_MEMORY, "no memory");
        return NULL;
    }

    msg->json = _cJSON_CreateObject();
    if (!msg->json)
    {
        tibeftlErr_Set(err, TIBEFTL_ERR_NO_MEMORY, "no memory");
        free(msg);
        return NULL;
    }

    return msg;
}

void
tibeftlMessage_Destroy(
    tibeftlErr                  err,
    tibeftlMessage              msg)
{
    if (!msg)
        return;

    if (msg->json)
        _cJSON_Delete(msg->json);

    tibeftlMessage_clearCache(msg);

    free(msg);
}

static void
_advance(
    char**                      ptr,
    size_t*                     used,
    size_t*                     left,
    size_t                      amount)
{
    if (*ptr)
        *ptr = *ptr + amount;
    *used = *used + amount;
    if (*left > amount)
        *left = *left - amount;
    else
        *left = 0;
}

size_t
_printFields(
    tibeftlMessage              msg,
    char*                       buffer,
    size_t                      size)
{
    char*                       ptr;
    size_t                      num, len = 0;
    const char*                 field;
    tibeftlFieldType            type;
    bool                        leadingComma = false;

    ptr = buffer;

    if (ptr)
    {
        num = snprintf(ptr, size, "%s", "{");
        _advance(&ptr, &len, &size, num);
    }
    else
    {
        len += strlen("{");
    }

    while (tibeftlMessage_NextField(NULL, msg, &field, &type))
    {
        if (leadingComma)
        {
            if (ptr)
            {
                num = snprintf(ptr, size, "%s", ", ");
                _advance(&ptr, &len, &size, num);
            }
            else
            {
                len += strlen(", ");
            }
        }
        else
        {
            leadingComma = true;
        }

        if (ptr)
        {
            num = snprintf(ptr, size, "%s:", field);
            _advance(&ptr, &len, &size, num);
        }
        else
        {
            len += strlen(field) + strlen(":");
        }

        switch (type)
        {
        case TIBEFTL_FIELD_TYPE_STRING:
        {
            const char* value;
            value = tibeftlMessage_GetString(NULL, msg, field);
            if (ptr)
            {
                num = snprintf(ptr, size, "%s", "string=\"");
                _advance(&ptr, &len, &size, num);
                num = snprintf(ptr, size, "%s", value);
                _advance(&ptr, &len, &size, num);
                num = snprintf(ptr, size, "%s", "\"");
                _advance(&ptr, &len, &size, num);
            }
            else
            {
                len += strlen("string=\"");
                len += strlen(value);
                len += strlen("\"");
            }
            break;
        }
        case TIBEFTL_FIELD_TYPE_LONG:
        {
            char data[32];
            int64_t value;
            value = tibeftlMessage_GetLong(NULL, msg, field);
            snprintf(data, sizeof(data), "long=%" PRId64, value);
            if (ptr)
            {
                num = snprintf(ptr, size, "%s", data);
                _advance(&ptr, &len, &size, num);
            }
            else
            {
                len += strlen(data);
            }
            break;
        }
        case TIBEFTL_FIELD_TYPE_DOUBLE:
        {
            char data[32];
            double value;
            value = tibeftlMessage_GetDouble(NULL, msg, field);
            snprintf(data, sizeof(data), "double=%f", value);
            if (ptr)
            {
                num = snprintf(ptr, size, "%s", data);
                _advance(&ptr, &len, &size, num);
            }
            else
            {
                len += strlen(data);
            }
            break;
        }
        case TIBEFTL_FIELD_TYPE_TIME:
        {
            char data[32];
            int64_t value;
            value = tibeftlMessage_GetTime(NULL, msg, field);
            snprintf(data, sizeof(data), "time=%" PRId64, value);
            if (ptr)
            {
                num = snprintf(ptr, size, "%s", data);
                _advance(&ptr, &len, &size, num);
            }
            else
            {
                len += strlen(data);
            }
            break;
        }
        case TIBEFTL_FIELD_TYPE_MESSAGE:
        {
            tibeftlMessage value;
            value = tibeftlMessage_GetMessage(NULL, msg, field);
            if (ptr)
            {
                num = snprintf(ptr, size, "%s", "message=");
                _advance(&ptr, &len, &size, num);
            }
            else
            {
                len += strlen("message=");
            }
            num = _printFields(value, ptr, size);
            _advance(&ptr, &len, &size, num);
            break;
        }
        case TIBEFTL_FIELD_TYPE_OPAQUE:
        {
            char data[32];
            size_t value = 0;
            tibeftlMessage_GetOpaque(NULL, msg, field, &value);
            snprintf(data, sizeof(data), "opaque=<%zu bytes>", value);
            if (ptr)
            {
                num = snprintf(ptr, size, "%s", data);
                _advance(&ptr, &len, &size, num);
            }
            else
            {
                len += strlen(data);
            }
            break;
        }
        case TIBEFTL_FIELD_TYPE_STRING_ARRAY:
        {
            char** value;
            int cnt, i;
            value = (char**)tibeftlMessage_GetArray(NULL, msg, TIBEFTL_FIELD_TYPE_STRING_ARRAY, field, &cnt);
            if (ptr)
            {
                num = snprintf(ptr, size, "%s", "string_array=[");
                _advance(&ptr, &len, &size, num);
            }
            else
            {
                len += strlen("string_array=[");
            }
            for (i = 0; i < cnt; i++)
            {
                if (ptr)
                {
                    if (i == 0)
                        num = snprintf(ptr, size, "\"%s\"", value[i]);
                    else
                        num = snprintf(ptr, size, ", \"%s\"", value[i]);

                    _advance(&ptr, &len, &size, num);
                }
                else
                {
                    len += strlen((i == 0 ? "\"\"" : ", \"\""));
                    len += strlen(value[i]);
                }
            }
            if (ptr)
            {
                num = snprintf(ptr, size, "%s", "]");
                _advance(&ptr, &len, &size, num);
            }
            else
            {
                len += strlen("]");
            }
            break;
        }
        case TIBEFTL_FIELD_TYPE_LONG_ARRAY:
        {
            int64_t* value;
            int cnt, i;
            value = (int64_t*)tibeftlMessage_GetArray(NULL, msg, TIBEFTL_FIELD_TYPE_LONG_ARRAY, field, &cnt);
            if (ptr)
            {
                num = snprintf(ptr, size, "%s", "long_array=[");
                _advance(&ptr, &len, &size, num);
            }
            else
            {
                len += strlen("long_array=[");
            }
            for (i = 0; i < cnt; i++)
            {
                char data[32];
                char* format = (i == 0 ? "%ld" : ", %ld");

                snprintf(data, sizeof(data), format, value[i]);

                if (ptr)
                {
                    num = snprintf(ptr, size, "%s", data);
                    _advance(&ptr, &len, &size, num);
                }
                else
                {
                    len += strlen(data);
                }
            }
            if (ptr)
            {
                num = snprintf(ptr, size, "%s", "]");
                _advance(&ptr, &len, &size, num);
            }
            else
            {
                len += strlen("]");
            }
            break;
        }
        case TIBEFTL_FIELD_TYPE_DOUBLE_ARRAY:
        {
            double* value;
            int cnt, i;
            value = (double*)tibeftlMessage_GetArray(NULL, msg, TIBEFTL_FIELD_TYPE_DOUBLE_ARRAY, field, &cnt);
            if (ptr)
            {
                num = snprintf(ptr, size, "%s", "double_array=[");
                _advance(&ptr, &len, &size, num);
            }
            else
            {
                len += strlen("double_array=[");
            }
            for (i = 0; i < cnt; i++)
            {
                char data[32];
                char* format = (i == 0 ? "%f" : ", %f");

                snprintf(data, sizeof(data), format, value[i]);

                if (ptr)
                {
                    num = snprintf(ptr, size, "%s", data);
                    _advance(&ptr, &len, &size, num);
                }
                else
                {
                    len += strlen(data);
                }
            }
            if (ptr)
            {
                num = snprintf(ptr, size, "%s", "]");
                _advance(&ptr, &len, &size, num);
            }
            else
            {
                len += strlen("]");
            }
            break;
        }
        case TIBEFTL_FIELD_TYPE_TIME_ARRAY:
        {
            int64_t* value;
            int cnt, i;
            value = (int64_t*)tibeftlMessage_GetArray(NULL, msg, TIBEFTL_FIELD_TYPE_TIME_ARRAY, field, &cnt);
            if (ptr)
            {
                num = snprintf(ptr, size, "%s", "time_array=[");
                _advance(&ptr, &len, &size, num);
            }
            else
            {
                len += strlen("time_array=[");
            }
            for (i = 0; i < cnt; i++)
            {
                char data[32];
                char* format = (i == 0 ? "%ld" : ", %ld");

                snprintf(data, sizeof(data), format, value[i]);

                if (ptr)
                {
                    num = snprintf(ptr, size, "%s", data);
                    _advance(&ptr, &len, &size, num);
                }
                else
                {
                    len += strlen(data);
                }
            }
            if (ptr)
            {
                num = snprintf(ptr, size, "%s", "]");
                _advance(&ptr, &len, &size, num);
            }
            else
            {
                len += strlen("]");
            }
            break;
        }
        case TIBEFTL_FIELD_TYPE_MESSAGE_ARRAY:
        {
            tibeftlMessage* value;
            int cnt, i;
            value = (tibeftlMessage*)tibeftlMessage_GetArray(NULL, msg, TIBEFTL_FIELD_TYPE_MESSAGE_ARRAY, field, &cnt);
            if (ptr)
            {
                num = snprintf(ptr, size, "%s", "message_array=[");
                _advance(&ptr, &len, &size, num);
            }
            else
            {
                len += strlen("message_array=[");
            }
            for (i = 0; i < cnt; i++)
            {
                if (i > 0)
                {
                    if (ptr)
                    {
                        num = snprintf(ptr, size, "%s", ", ");
                        _advance(&ptr, &len, &size, num);
                    }
                    else
                    {
                        len += strlen(", ");
                    }
                }

                num = _printFields(value[i], ptr, size);
                _advance(&ptr, &len, &size, num);
            }
            if (ptr)
            {
                num = snprintf(ptr, size, "%s", "]");
                _advance(&ptr, &len, &size, num);
            }
            else
            {
                len += strlen("]");
            }
            break;
        }
        default:
            break;
        }
    }

    if (ptr)
    {
        num = snprintf(ptr, size, "%s", "}");
        _advance(&ptr, &len, &size, num);
    }
    else
    {
        len += strlen("}");
    }

    return len;
}

size_t
tibeftlMessage_ToString(
    tibeftlErr                  err,
    tibeftlMessage              msg,
    char*                       buffer,
    size_t                      size)
{
    char*                       ptr;
    size_t                      num, len = 0;

    if (tibeftlErr_IsSet(err))
        return 0;

    if (!msg)
    {
        tibeftlErr_Set(err, TIBEFTL_ERR_INVALID_ARG, "message is NULL");
        return 0;
    }

    ptr = buffer;

    num = _printFields(msg, ptr, size);
    _advance(&ptr, &len, &size, num);

    return len + 1;
}

bool
tibeftlMessage_IsFieldSet(
    tibeftlErr                  err,
    tibeftlMessage              msg,
    const char*                 field)
{
    if (tibeftlErr_IsSet(err))
        return false;

    if (!msg)
    {
        tibeftlErr_Set(err, TIBEFTL_ERR_INVALID_ARG, "message is NULL");
        return false;
    }

    return (_cJSON_GetObjectItem(msg->json, field) ? true : false);
}


static tibeftlFieldType
_fieldType(
    _cJSON*                     obj)
{
    tibeftlFieldType            type = TIBEFTL_FIELD_TYPE_UNKNOWN;

    if (obj)
    {
        switch (obj->type)
        {
        case _cJSON_Number:
            type = TIBEFTL_FIELD_TYPE_LONG;
            break;
        case _cJSON_String:
            type = TIBEFTL_FIELD_TYPE_STRING;
            break;
        case _cJSON_Array:
            switch (_fieldType(obj->child))
            {
            case TIBEFTL_FIELD_TYPE_LONG:
                type = TIBEFTL_FIELD_TYPE_LONG_ARRAY;
                break;
            case TIBEFTL_FIELD_TYPE_STRING:
                type = TIBEFTL_FIELD_TYPE_STRING_ARRAY;
                break;
            case TIBEFTL_FIELD_TYPE_DOUBLE:
                type = TIBEFTL_FIELD_TYPE_DOUBLE_ARRAY;
                break;
            case TIBEFTL_FIELD_TYPE_TIME:
                type = TIBEFTL_FIELD_TYPE_TIME_ARRAY;
                break;
            case TIBEFTL_FIELD_TYPE_MESSAGE:
                type = TIBEFTL_FIELD_TYPE_MESSAGE_ARRAY;
                break;
            default:
                break;
            }
            break;
        case _cJSON_Object:
            if (_cJSON_GetObjectItem(obj, "_d_"))
                type = TIBEFTL_FIELD_TYPE_DOUBLE;
            else if (_cJSON_GetObjectItem(obj, "_m_"))
                type = TIBEFTL_FIELD_TYPE_TIME;
            else if (_cJSON_GetObjectItem(obj, "_o_"))
                type = TIBEFTL_FIELD_TYPE_OPAQUE;
            else
                type = TIBEFTL_FIELD_TYPE_MESSAGE;
            break;
        default:
            break;
        }
    }

    return type;
}

tibeftlFieldType
tibeftlMessage_GetFieldType(
    tibeftlErr                  err,
    tibeftlMessage              msg,
    const char*                 field)
{
    tibeftlFieldType            type = TIBEFTL_FIELD_TYPE_UNKNOWN;

    if (tibeftlErr_IsSet(err))
        return TIBEFTL_FIELD_TYPE_UNKNOWN;

    if (!msg)
    {
        tibeftlErr_Set(err, TIBEFTL_ERR_INVALID_ARG, "message is NULL");
        return TIBEFTL_FIELD_TYPE_UNKNOWN;
    }

    if (!field)
    {
        tibeftlErr_Set(err, TIBEFTL_ERR_INVALID_ARG, "field is NULL");
        return TIBEFTL_FIELD_TYPE_UNKNOWN;
    }

    if (msg && field)
    {
        type = _fieldType(_cJSON_GetObjectItem(msg->json, field));
    }

    return type;
}

bool
tibeftlMessage_NextField(
    tibeftlErr                  err,
    tibeftlMessage              msg,
    const char**                field,
    tibeftlFieldType*           type)
{
    if (tibeftlErr_IsSet(err))
        return false;

    if (!msg)
    {
        tibeftlErr_Set(err, TIBEFTL_ERR_INVALID_ARG, "message is NULL");
        return false;
    }

    if (!msg->iter)
        msg->iter = msg->json->child;
    else
        msg->iter = msg->iter->next;

    if (!msg->iter)
        return false;

    if (field)
        *field = msg->iter->string;

    if (type)
        *type = _fieldType(msg->iter);

    return true;
}

void
tibeftlMessage_ClearField(
    tibeftlErr                  err,
    tibeftlMessage              msg,
    const char*                 field)
{
    if (tibeftlErr_IsSet(err))
        return;

    if (!msg)
    {
        tibeftlErr_Set(err, TIBEFTL_ERR_INVALID_ARG, "message is NULL");
        return;
    }

    if (!field)
    {
        tibeftlErr_Set(err, TIBEFTL_ERR_INVALID_ARG, "field is NULL");
        return;
    }

    _cJSON_DeleteItemFromObject(msg->json, field);

    tibeftlMessage_removeCache(msg, field);
}

void
tibeftlMessage_ClearAllFields(
    tibeftlErr                  err,
    tibeftlMessage              msg)
{
    if (tibeftlErr_IsSet(err))
        return;

    if (!msg)
    {
        tibeftlErr_Set(err, TIBEFTL_ERR_INVALID_ARG, "message is NULL");
        return;
    }

    _cJSON_Delete(msg->json);

    msg->json = _cJSON_CreateObject();

    tibeftlMessage_clearCache(msg);

    tibeftlMessage_clearCache(msg);
}

void
tibeftlMessage_SetString(
    tibeftlErr                  err,
    tibeftlMessage              msg,
    const char*                 field,
    const char*                 value)
{
    if (tibeftlErr_IsSet(err))
        return;

    if (!msg)
    {
        tibeftlErr_Set(err, TIBEFTL_ERR_INVALID_ARG, "message is NULL");
        return;
    }

    if (!field)
    {
        tibeftlErr_Set(err, TIBEFTL_ERR_INVALID_ARG, "field is NULL");
        return;
    }

    if (!value)
    {
        tibeftlErr_Set(err, TIBEFTL_ERR_INVALID_ARG, "value is NULL");
        return;
    }

    _cJSON_DeleteItemFromObject(msg->json, field);
    _cJSON_AddStringToObject(msg->json, field, value);

    tibeftlMessage_removeCache(msg, field);
}

void
tibeftlMessage_SetLong(
    tibeftlErr                  err,
    tibeftlMessage              msg,
    const char*                 field,
    int64_t                     value)
{
    if (tibeftlErr_IsSet(err))
        return;

    if (!msg)
    {
        tibeftlErr_Set(err, TIBEFTL_ERR_INVALID_ARG, "message is NULL");
        return;
    }

    if (!field)
    {
        tibeftlErr_Set(err, TIBEFTL_ERR_INVALID_ARG, "field is NULL");
        return;
    }

    _cJSON_DeleteItemFromObject(msg->json, field);
    _cJSON_AddNumberToObject(msg->json, field, value);

    tibeftlMessage_removeCache(msg, field);
}

void
tibeftlMessage_SetDouble(
    tibeftlErr                  err,
    tibeftlMessage              msg,
    const char*                 field,
    double                      value)
{
    _cJSON*                     obj;

    if (tibeftlErr_IsSet(err))
        return;

    if (!msg)
    {
        tibeftlErr_Set(err, TIBEFTL_ERR_INVALID_ARG, "message is NULL");
        return;
    }

    if (!field)
    {
        tibeftlErr_Set(err, TIBEFTL_ERR_INVALID_ARG, "field is NULL");
        return;
    }

    obj = _cJSON_CreateObject();
    _cJSON_AddDoubleToObject(obj, "_d_", value);

    _cJSON_DeleteItemFromObject(msg->json, field);
    _cJSON_AddItemToObject(msg->json, field, obj);

    tibeftlMessage_removeCache(msg, field);
}

void
tibeftlMessage_SetTime(
    tibeftlErr                  err,
    tibeftlMessage              msg,
    const char*                 field,
    int64_t                     value)
{
    _cJSON*                     obj;

    if (tibeftlErr_IsSet(err))
        return;

    if (!msg)
    {
        tibeftlErr_Set(err, TIBEFTL_ERR_INVALID_ARG, "message is NULL");
        return;
    }

    if (!field)
    {
        tibeftlErr_Set(err, TIBEFTL_ERR_INVALID_ARG, "field is NULL");
        return;
    }

    obj = _cJSON_CreateObject();
    _cJSON_AddNumberToObject(obj, "_m_", value);

    _cJSON_DeleteItemFromObject(msg->json, field);
    _cJSON_AddItemToObject(msg->json, field, obj);

    tibeftlMessage_removeCache(msg, field);
}

void
tibeftlMessage_SetMessage(
    tibeftlErr                  err,
    tibeftlMessage              msg,
    const char*                 field,
    tibeftlMessage              value)
{
    if (tibeftlErr_IsSet(err))
        return;

    if (!msg)
    {
        tibeftlErr_Set(err, TIBEFTL_ERR_INVALID_ARG, "message is NULL");
        return;
    }

    if (!field)
    {
        tibeftlErr_Set(err, TIBEFTL_ERR_INVALID_ARG, "field is NULL");
        return;
    }

    if (!value)
    {
        tibeftlErr_Set(err, TIBEFTL_ERR_INVALID_ARG, "value is NULL");
        return;
    }

    _cJSON_DeleteItemFromObject(msg->json, field);
    _cJSON_AddItemToObject(msg->json, field, _cJSON_Duplicate(value->json, _cJSON_True));

    tibeftlMessage_removeCache(msg, field);
}

void
tibeftlMessage_SetOpaque(
    tibeftlErr                  err,
    tibeftlMessage              msg,
    const char*                 field,
    void*                       value,
    size_t                      size)
{
    _cJSON*                     obj;
    char*                       str;
    int                         len;

    if (tibeftlErr_IsSet(err))
        return;

    if (!msg)
    {
        tibeftlErr_Set(err, TIBEFTL_ERR_INVALID_ARG, "message is NULL");
        return;
    }

    if (!field)
    {
        tibeftlErr_Set(err, TIBEFTL_ERR_INVALID_ARG, "field is NULL");
        return;
    }

    if (!value)
    {
        tibeftlErr_Set(err, TIBEFTL_ERR_INVALID_ARG, "value is NULL");
        return;
    }

    len = BASE64_ENC_MAX_LEN(size);
    str = malloc(len);
    base64_encode(value, size, str, len);

    obj = _cJSON_CreateObject();
    _cJSON_AddStringToObject(obj, "_o_", str);

    _cJSON_DeleteItemFromObject(msg->json, field);
    _cJSON_AddItemToObject(msg->json, field, obj);

    tibeftlMessage_removeCache(msg, field);

    free(str);
}

void
tibeftlMessage_SetArray(
    tibeftlErr                  err,
    tibeftlMessage              msg,
    tibeftlFieldType            type,
    const char*                 field,
    const void* const           value,
    int                         size)
{
    _cJSON*                     arr;
    _cJSON*                     obj = NULL;
    int                         i;

    if (tibeftlErr_IsSet(err))
        return;

    if (!msg)
    {
        tibeftlErr_Set(err, TIBEFTL_ERR_INVALID_ARG, "message is NULL");
        return;
    }

    if (!field)
    {
        tibeftlErr_Set(err, TIBEFTL_ERR_INVALID_ARG, "field is NULL");
        return;
    }

    if (!value)
    {
        tibeftlErr_Set(err, TIBEFTL_ERR_INVALID_ARG, "value is NULL");
        return;
    }

    arr = _cJSON_CreateArray();

    for (i = 0; i < size; i++)
    {
        switch (type) {
        case TIBEFTL_FIELD_TYPE_STRING_ARRAY:
            obj = _cJSON_CreateString(((const char**)value)[i]);
            break;
        case TIBEFTL_FIELD_TYPE_LONG_ARRAY:
            obj = _cJSON_CreateNumber(((int64_t*)value)[i]);
            break;
        case TIBEFTL_FIELD_TYPE_DOUBLE_ARRAY:
            obj = _cJSON_CreateObject();
            if (obj)
                _cJSON_AddDoubleToObject(obj, "_d_", ((double*)value)[i]);
            break;
        case TIBEFTL_FIELD_TYPE_TIME_ARRAY:
            obj = _cJSON_CreateObject();
            if (obj)
                _cJSON_AddNumberToObject(obj, "_m_", ((int64_t*)value)[i]);
            break;
        case TIBEFTL_FIELD_TYPE_MESSAGE_ARRAY:
            obj = _cJSON_Duplicate((((tibeftlMessage*)value)[i])->json, _cJSON_True);
            break;
        default:
            _cJSON_Delete(obj);
            _cJSON_Delete(arr);
            tibeftlErr_Set(err, TIBEFTL_ERR_INVALID_TYPE, "invalid field type");
            return;
        }
        _cJSON_AddItemToArray(arr, obj);
    }

    _cJSON_DeleteItemFromObject(msg->json, field);
    _cJSON_AddItemToObject(msg->json, field, arr);

    tibeftlMessage_removeCache(msg, field);
}

const char*
tibeftlMessage_GetString(
    tibeftlErr                  err,
    tibeftlMessage              msg,
    const char*                 field)
{
    _cJSON*                     obj;

    if (tibeftlErr_IsSet(err))
        return NULL;

    if (!msg)
    {
        tibeftlErr_Set(err, TIBEFTL_ERR_INVALID_ARG, "message is NULL");
        return NULL;
    }

    if (!field)
    {
        tibeftlErr_Set(err, TIBEFTL_ERR_INVALID_ARG, "field is NULL");
        return NULL;
    }

    obj = _cJSON_GetObjectItem(msg->json, field);
    if (!obj)
    {
        tibeftlErr_Set(err, TIBEFTL_ERR_NOT_FOUND, "field not found");
        return NULL;
    }

    if (!_cJSON_IsString(obj))
    {
        tibeftlErr_Set(err, TIBEFTL_ERR_INVALID_TYPE, "field type is not a string");
        return NULL;
    }

    return obj->valuestring;
}

int64_t
tibeftlMessage_GetLong(
    tibeftlErr                  err,
    tibeftlMessage              msg,
    const char*                 field)
{
    _cJSON*                     obj;

    if (tibeftlErr_IsSet(err))
        return 0;

    if (!msg)
    {
        tibeftlErr_Set(err, TIBEFTL_ERR_INVALID_ARG, "message is NULL");
        return 0;
    }

    if (!field)
    {
        tibeftlErr_Set(err, TIBEFTL_ERR_INVALID_ARG, "field is NULL");
        return 0;
    }

    obj = _cJSON_GetObjectItem(msg->json, field);
    if (!obj)
    {
        tibeftlErr_Set(err, TIBEFTL_ERR_NOT_FOUND, "field not found");
        return 0;
    }

    if (!_cJSON_IsNumber(obj))
    {
        tibeftlErr_Set(err, TIBEFTL_ERR_INVALID_TYPE, "field type is not a long");
        return 0;
    }

    return obj->valuedouble;
}

double
tibeftlMessage_GetDouble(
    tibeftlErr                  err,
    tibeftlMessage              msg,
    const char*                 field)
{
    _cJSON*                     obj;
    _cJSON*                     val;

    if (tibeftlErr_IsSet(err))
        return 0;

    if (!msg)
    {
        tibeftlErr_Set(err, TIBEFTL_ERR_INVALID_ARG, "message is NULL");
        return 0;
    }

    if (!field)
    {
        tibeftlErr_Set(err, TIBEFTL_ERR_INVALID_ARG, "field is NULL");
        return 0;
    }

    obj = _cJSON_GetObjectItem(msg->json, field);
    if (!obj)
    {
        tibeftlErr_Set(err, TIBEFTL_ERR_NOT_FOUND, "field not found");
        return 0;
    }

    if (!_cJSON_IsObject(obj))
    {
        tibeftlErr_Set(err, TIBEFTL_ERR_INVALID_TYPE, "field type is not a double");
        return 0;
    }

    val = _cJSON_GetObjectItem(obj, "_d_");
    if (!val || !_cJSON_IsDouble(val))
    {
        tibeftlErr_Set(err, TIBEFTL_ERR_INVALID_TYPE, "field type is not a double");
        return 0;
    }

    return _cJSON_GetDouble(val);
}

int64_t
tibeftlMessage_GetTime(
    tibeftlErr                  err,
    tibeftlMessage              msg,
    const char*                 field)
{
    _cJSON*                     obj;
    _cJSON*                     val;

    if (tibeftlErr_IsSet(err))
        return 0;

    if (!msg)
    {
        tibeftlErr_Set(err, TIBEFTL_ERR_INVALID_ARG, "message is NULL");
        return 0;
    }

    if (!field)
    {
        tibeftlErr_Set(err, TIBEFTL_ERR_INVALID_ARG, "field is NULL");
        return 0;
    }

    obj = _cJSON_GetObjectItem(msg->json, field);
    if (!obj)
    {
        tibeftlErr_Set(err, TIBEFTL_ERR_NOT_FOUND, "field not found");
        return 0;
    }

    if (!_cJSON_IsObject(obj))
    {
        tibeftlErr_Set(err, TIBEFTL_ERR_INVALID_TYPE, "field type is not a time");
        return 0;
    }

    val = _cJSON_GetObjectItem(obj, "_m_");
    if (!val || !_cJSON_IsNumber(val))
    {
        tibeftlErr_Set(err, TIBEFTL_ERR_INVALID_TYPE, "field type is not a time");
        return 0;
    }

    return val->valuedouble;
}

tibeftlMessage
tibeftlMessage_GetMessage(
    tibeftlErr                  err,
    tibeftlMessage              msg,
    const char*                 field)
{
    tibeftlMessage              sub;
    _cJSON*                     obj;

    if (tibeftlErr_IsSet(err))
        return NULL;

    if (!msg)
    {
        tibeftlErr_Set(err, TIBEFTL_ERR_INVALID_ARG, "message is NULL");
        return NULL;
    }

    if (!field)
    {
        tibeftlErr_Set(err, TIBEFTL_ERR_INVALID_ARG, "field is NULL");
        return NULL;
    }

    obj = _cJSON_GetObjectItem(msg->json, field);
    if (!obj)
    {
        tibeftlErr_Set(err, TIBEFTL_ERR_NOT_FOUND, "field not found");
        return NULL;
    }

    if (!_cJSON_IsObject(obj))
    {
        tibeftlErr_Set(err, TIBEFTL_ERR_INVALID_TYPE, "field type is not a message");
        return NULL;
    }

    sub = tibeftlMessage_getCache(msg, TIBEFTL_FIELD_TYPE_MESSAGE, field, NULL);

    if (!sub)
    {
        sub = tibeftlMessage_CreateWithJSON(_cJSON_Duplicate(obj, _cJSON_True));

        tibeftlMessage_addCache(msg, TIBEFTL_FIELD_TYPE_MESSAGE, field, sub, 0);
    }

    return sub;
}

void*
tibeftlMessage_GetOpaque(
    tibeftlErr                  err,
    tibeftlMessage              msg,
    const char*                 field,
    size_t*                     size)
{
    _cJSON*                     obj;
    _cJSON*                     val;
    void*                       buf;
    int                         len;

    if (tibeftlErr_IsSet(err))
        return NULL;

    if (!msg)
    {
        tibeftlErr_Set(err, TIBEFTL_ERR_INVALID_ARG, "message is NULL");
        return NULL;
    }

    if (!field)
    {
        tibeftlErr_Set(err, TIBEFTL_ERR_INVALID_ARG, "field is NULL");
        return NULL;
    }

    obj = _cJSON_GetObjectItem(msg->json, field);
    if (!obj)
    {
        tibeftlErr_Set(err, TIBEFTL_ERR_NOT_FOUND, "field not found");
        return NULL;
    }

    if (!_cJSON_IsObject(obj))
    {
        tibeftlErr_Set(err, TIBEFTL_ERR_INVALID_TYPE, "field type is not an opaque");
        return NULL;
    }

    val = _cJSON_GetObjectItem(obj, "_o_");
    if (!val || !_cJSON_IsString(val))
    {
        tibeftlErr_Set(err, TIBEFTL_ERR_INVALID_TYPE, "field type is not an opaque");
        return NULL;
    }

    buf = tibeftlMessage_getCache(msg, TIBEFTL_FIELD_TYPE_OPAQUE, field, &len);

    if (!buf)
    {
        len = BASE64_DEC_MAX_LEN(strlen(val->valuestring));
        buf = malloc(len+1);
        len = base64_decode(buf, len, val->valuestring, strlen(val->valuestring));

        tibeftlMessage_addCache(msg, TIBEFTL_FIELD_TYPE_OPAQUE, field, buf, len);
    }

    if (size)
        *size = len;

    return buf;
}

void*
tibeftlMessage_GetArray(
    tibeftlErr                  err,
    tibeftlMessage              msg,
    tibeftlFieldType            type,
    const char*                 field,
    int*                        size)
{
    _cJSON*                     obj;
    _cJSON*                     val;
    void*                       arr;
    int                         len = 0;
    int                         i;

    if (tibeftlErr_IsSet(err))
        return NULL;

    if (!msg)
    {
        tibeftlErr_Set(err, TIBEFTL_ERR_INVALID_ARG, "message is NULL");
        return NULL;
    }

    if (!field)
    {
        tibeftlErr_Set(err, TIBEFTL_ERR_INVALID_ARG, "field is NULL");
        return NULL;
    }

    obj = _cJSON_GetObjectItem(msg->json, field);
    if (!obj)
    {
        tibeftlErr_Set(err, TIBEFTL_ERR_NOT_FOUND, "field not found");
        return NULL;
    }

    if (!_cJSON_IsArray(obj))
    {
        tibeftlErr_Set(err, TIBEFTL_ERR_INVALID_TYPE, "field type is not an array");
        return NULL;
    }

    if (_fieldType(obj) != type)
    {
        tibeftlErr_Set(err, TIBEFTL_ERR_INVALID_TYPE, "invalid field type");
        return NULL;
    }

    arr = tibeftlMessage_getCache(msg, type, field, &len);

    if (!arr)
    {
        len = _cJSON_GetArraySize(obj);

        if (len > 0)
        {
            switch (type)
            {
            case TIBEFTL_FIELD_TYPE_STRING_ARRAY:
                arr = malloc(sizeof(char*) * len);
                for (i = 0; i < len; i++)
                {
                    val = _cJSON_GetArrayItem(obj, i);
                    if (val)
                        ((char**)arr)[i] = val->valuestring;
                }
                break;
            case TIBEFTL_FIELD_TYPE_LONG_ARRAY:
                arr = malloc(sizeof(int64_t) * len);
                for (i = 0; i < len; i++)
                {
                    val = _cJSON_GetArrayItem(obj, i);
                    if (val)
                        ((int64_t*)arr)[i] = val->valuedouble;
                }
                break;
            case TIBEFTL_FIELD_TYPE_DOUBLE_ARRAY:
                arr = malloc(sizeof(double) * len);
                for (i = 0; i < len; i++)
                {
                    val = _cJSON_GetArrayItem(obj, i);
                    if (val)
                    {
                        _cJSON* dbl = _cJSON_GetObjectItem(val, "_d_");
                        if (dbl)
                            ((double*)arr)[i] = _cJSON_GetDouble(dbl);
                    }
                }
                break;
            case TIBEFTL_FIELD_TYPE_TIME_ARRAY:
                arr = malloc(sizeof(int64_t) * len);
                for (i = 0; i < len; i++)
                {
                    val = _cJSON_GetArrayItem(obj, i);
                    if (val)
                    {
                        _cJSON* mil = _cJSON_GetObjectItem(val, "_m_");
                        if (mil)
                            ((int64_t*)arr)[i] = mil->valuedouble;
                    }
                }
                break;
            case TIBEFTL_FIELD_TYPE_MESSAGE_ARRAY:
                arr = malloc(sizeof(tibeftlMessage) * len);
                for (i = 0; i < len; i++)
                {
                    val = _cJSON_GetArrayItem(obj, i);
                    if (val)
                        ((tibeftlMessage*)arr)[i] = tibeftlMessage_CreateWithJSON(_cJSON_Duplicate(val, _cJSON_True));
                }
                break;
            default:
                tibeftlErr_Set(err, TIBEFTL_ERR_INVALID_TYPE, "invalid field type");
                return NULL;
            }

            tibeftlMessage_addCache(msg, type, field, arr, len);
        }
    }

    if (size)
        *size = len;

    return arr;
}

