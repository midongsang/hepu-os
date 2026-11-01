/*
  Copyright (c) 2009-2017 Dave Gamble and cJSON contributors

  Permission is hereby granted, free of charge, to any person obtaining a copy
  of this software and associated documentation files (the "Software"), to deal
  in the Software without restriction, including without limitation the rights
  to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
  copies of the Software, and to permit persons to whom the Software is
  furnished to do so, subject to the following conditions:

  The above copyright notice and this permission notice shall be included in
  all copies or substantial portions of the Software.

  HepuOS adaptation: uses libhepu malloc/free, simplified implementation
*/

#include "cJSON.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <math.h>
#include <float.h>
#include <limits.h>
#include <ctype.h>

/* 全局错误指针 */
static const char *global_error_ptr = NULL;

/* 内部类型 */
typedef struct
{
    const unsigned char *content;
    size_t length;
    size_t offset;
    size_t depth;
} parse_buffer;

/* 前向声明 */
static cJSON *parse_value(parse_buffer *buffer, const unsigned char **return_parse_end);
static cJSON *parse_string(parse_buffer *buffer, const unsigned char **return_parse_end);
static cJSON *parse_number(parse_buffer *buffer, const unsigned char **return_parse_end);
static cJSON *parse_array(parse_buffer *buffer, const unsigned char **return_parse_end);
static cJSON *parse_object(parse_buffer *buffer, const unsigned char **return_parse_end);

/* 跳过空白字符 */
static void skip_whitespace(parse_buffer *buffer)
{
    while (buffer->offset < buffer->length)
    {
        unsigned char c = buffer->content[buffer->offset];
        if (c == ' ' || c == '\t' || c == '\n' || c == '\r')
        {
            buffer->offset++;
        }
        else
        {
            break;
        }
    }
}

/* 创建新节点 */
static cJSON *cJSON_New_Item(void)
{
    cJSON *node = (cJSON*)malloc(sizeof(cJSON));
    if (node)
    {
        memset(node, 0, sizeof(cJSON));
    }
    return node;
}

/* 删除节点 */
void cJSON_Delete(cJSON *item)
{
    cJSON *next;
    while (item)
    {
        next = item->next;
        if (!(item->type & cJSON_IsReference) && item->child)
        {
            cJSON_Delete(item->child);
        }
        if (!(item->type & cJSON_IsReference) && item->valuestring)
        {
            free(item->valuestring);
        }
        if (!(item->type & cJSON_StringIsConst) && item->string)
        {
            free(item->string);
        }
        free(item);
        item = next;
    }
}

/* 解析字符串 */
static cJSON *parse_string(parse_buffer *buffer, const unsigned char **return_parse_end)
{
    cJSON *item = NULL;
    const unsigned char *input = buffer->content + buffer->offset;
    const unsigned char *output = NULL;
    size_t len = 0;
    unsigned char *output_pointer = NULL;
    size_t output_length = 0;

    if (buffer->offset >= buffer->length || *input != '"')
    {
        if (return_parse_end) *return_parse_end = input;
        return NULL;
    }

    input++;
    /* 计算字符串长度 */
    while (input + len < buffer->content + buffer->length && input[len] != '"')
    {
        if (input[len] == '\\' && input + len + 1 < buffer->content + buffer->length)
        {
            len++;
        }
        len++;
    }

    if (input + len >= buffer->content + buffer->length)
    {
        if (return_parse_end) *return_parse_end = input + len;
        return NULL;
    }

    item = cJSON_New_Item();
    if (!item) return NULL;

    output = (const unsigned char*)malloc(len + 1);
    if (!output)
    {
        free(item);
        return NULL;
    }

    output_pointer = (unsigned char*)output;
    output_length = 0;

    while (buffer->content + buffer->offset + 1 < buffer->content + buffer->length &&
           buffer->content[buffer->offset + 1] != '"')
    {
        buffer->offset++;
        if (buffer->content[buffer->offset] == '\\')
        {
            buffer->offset++;
            if (buffer->offset >= buffer->length) break;

            switch (buffer->content[buffer->offset])
            {
                case 'b': output_pointer[output_length++] = '\b'; break;
                case 'f': output_pointer[output_length++] = '\f'; break;
                case 'n': output_pointer[output_length++] = '\n'; break;
                case 'r': output_pointer[output_length++] = '\r'; break;
                case 't': output_pointer[output_length++] = '\t'; break;
                case '"': output_pointer[output_length++] = '"'; break;
                case '\\': output_pointer[output_length++] = '\\'; break;
                case '/': output_pointer[output_length++] = '/'; break;
                default: output_pointer[output_length++] = buffer->content[buffer->offset]; break;
            }
        }
        else
        {
            output_pointer[output_length++] = buffer->content[buffer->offset];
        }
    }
    output_pointer[output_length] = '\0';

    buffer->offset++; /* 跳过结束引号 */

    item->type = cJSON_String;
    item->valuestring = (char*)output;

    if (return_parse_end) *return_parse_end = buffer->content + buffer->offset;
    return item;
}

/* 解析数字 */
static cJSON *parse_number(parse_buffer *buffer, const unsigned char **return_parse_end)
{
    cJSON *item = NULL;
    double number = 0;
    double sign = 1.0;
    double scale = 0.0;
    int subscale = 0;
    int signsubscale = 1;
    const unsigned char *start = buffer->content + buffer->offset;

    if (buffer->offset >= buffer->length) return NULL;

    if (*start == '-') { sign = -1; buffer->offset++; }
    if (buffer->offset >= buffer->length) return NULL;

    if (*(buffer->content + buffer->offset) == '0')
    {
        buffer->offset++;
    }
    else if (*(buffer->content + buffer->offset) >= '1' && *(buffer->content + buffer->offset) <= '9')
    {
        while (buffer->offset < buffer->length &&
               *(buffer->content + buffer->offset) >= '0' &&
               *(buffer->content + buffer->offset) <= '9')
        {
            number = number * 10.0 + (*(buffer->content + buffer->offset) - '0');
            buffer->offset++;
        }
    }
    else
    {
        if (return_parse_end) *return_parse_end = start;
        return NULL;
    }

    if (buffer->offset < buffer->length && *(buffer->content + buffer->offset) == '.')
    {
        buffer->offset++;
        scale = 1.0;
        while (buffer->offset < buffer->length &&
               *(buffer->content + buffer->offset) >= '0' &&
               *(buffer->content + buffer->offset) <= '9')
        {
            scale *= 0.1;
            number += (*(buffer->content + buffer->offset) - '0') * scale;
            buffer->offset++;
        }
    }

    if (buffer->offset < buffer->length &&
        (*(buffer->content + buffer->offset) == 'e' || *(buffer->content + buffer->offset) == 'E'))
    {
        buffer->offset++;
        if (buffer->offset < buffer->length && *(buffer->content + buffer->offset) == '+') buffer->offset++;
        else if (buffer->offset < buffer->length && *(buffer->content + buffer->offset) == '-')
        {
            signsubscale = -1;
            buffer->offset++;
        }
        while (buffer->offset < buffer->length &&
               *(buffer->content + buffer->offset) >= '0' &&
               *(buffer->content + buffer->offset) <= '9')
        {
            subscale = subscale * 10 + (*(buffer->content + buffer->offset) - '0');
            buffer->offset++;
        }
        number *= pow(10.0, signsubscale * subscale);
    }

    item = cJSON_New_Item();
    if (!item) return NULL;

    item->type = cJSON_Number;
    item->valuedouble = sign * number;
    item->valueint = (int)(sign * number);

    if (return_parse_end) *return_parse_end = buffer->content + buffer->offset;
    return item;
}

/* 解析数组 */
static cJSON *parse_array(parse_buffer *buffer, const unsigned char **return_parse_end)
{
    cJSON *item = NULL;
    cJSON *child = NULL;
    cJSON *prev = NULL;

    if (buffer->offset >= buffer->length || buffer->content[buffer->offset] != '[')
    {
        if (return_parse_end) *return_parse_end = buffer->content + buffer->offset;
        return NULL;
    }

    item = cJSON_New_Item();
    if (!item) return NULL;
    item->type = cJSON_Array;

    buffer->offset++;
    skip_whitespace(buffer);

    if (buffer->offset < buffer->length && buffer->content[buffer->offset] == ']')
    {
        buffer->offset++;
        if (return_parse_end) *return_parse_end = buffer->content + buffer->offset;
        return item;
    }

    while (buffer->offset < buffer->length)
    {
        child = parse_value(buffer, NULL);
        if (!child) break;

        if (!item->child)
        {
            item->child = child;
        }
        else
        {
            prev->next = child;
            child->prev = prev;
        }
        prev = child;

        skip_whitespace(buffer);

        if (buffer->offset < buffer->length && buffer->content[buffer->offset] == ',')
        {
            buffer->offset++;
            skip_whitespace(buffer);
            continue;
        }
        else if (buffer->offset < buffer->length && buffer->content[buffer->offset] == ']')
        {
            buffer->offset++;
            break;
        }
        else
        {
            break;
        }
    }

    if (return_parse_end) *return_parse_end = buffer->content + buffer->offset;
    return item;
}

/* 解析对象 */
static cJSON *parse_object(parse_buffer *buffer, const unsigned char **return_parse_end)
{
    cJSON *item = NULL;
    cJSON *child = NULL;
    cJSON *prev = NULL;
    cJSON *key = NULL;

    if (buffer->offset >= buffer->length || buffer->content[buffer->offset] != '{')
    {
        if (return_parse_end) *return_parse_end = buffer->content + buffer->offset;
        return NULL;
    }

    item = cJSON_New_Item();
    if (!item) return NULL;
    item->type = cJSON_Object;

    buffer->offset++;
    skip_whitespace(buffer);

    if (buffer->offset < buffer->length && buffer->content[buffer->offset] == '}')
    {
        buffer->offset++;
        if (return_parse_end) *return_parse_end = buffer->content + buffer->offset;
        return item;
    }

    while (buffer->offset < buffer->length)
    {
        key = parse_string(buffer, NULL);
        if (!key) break;

        skip_whitespace(buffer);

        if (buffer->offset >= buffer->length || buffer->content[buffer->offset] != ':')
        {
            cJSON_Delete(key);
            break;
        }
        buffer->offset++;
        skip_whitespace(buffer);

        child = parse_value(buffer, NULL);
        if (!child)
        {
            cJSON_Delete(key);
            break;
        }

        child->string = key->valuestring;
        key->valuestring = NULL;
        cJSON_Delete(key);

        if (!item->child)
        {
            item->child = child;
        }
        else
        {
            prev->next = child;
            child->prev = prev;
        }
        prev = child;

        skip_whitespace(buffer);

        if (buffer->offset < buffer->length && buffer->content[buffer->offset] == ',')
        {
            buffer->offset++;
            skip_whitespace(buffer);
            continue;
        }
        else if (buffer->offset < buffer->length && buffer->content[buffer->offset] == '}')
        {
            buffer->offset++;
            break;
        }
        else
        {
            break;
        }
    }

    if (return_parse_end) *return_parse_end = buffer->content + buffer->offset;
    return item;
}

/* 解析值 */
static cJSON *parse_value(parse_buffer *buffer, const unsigned char **return_parse_end)
{
    cJSON *item = NULL;

    skip_whitespace(buffer);

    if (buffer->offset >= buffer->length)
    {
        if (return_parse_end) *return_parse_end = buffer->content + buffer->offset;
        return NULL;
    }

    switch (buffer->content[buffer->offset])
    {
        case '"':
            item = parse_string(buffer, return_parse_end);
            break;
        case '{':
            item = parse_object(buffer, return_parse_end);
            break;
        case '[':
            item = parse_array(buffer, return_parse_end);
            break;
        case 't':
            if (buffer->offset + 3 < buffer->length &&
                strncmp((const char*)(buffer->content + buffer->offset), "true", 4) == 0)
            {
                item = cJSON_New_Item();
                if (item) item->type = cJSON_True;
                buffer->offset += 4;
            }
            break;
        case 'f':
            if (buffer->offset + 4 < buffer->length &&
                strncmp((const char*)(buffer->content + buffer->offset), "false", 5) == 0)
            {
                item = cJSON_New_Item();
                if (item) item->type = cJSON_False;
                buffer->offset += 5;
            }
            break;
        case 'n':
            if (buffer->offset + 3 < buffer->length &&
                strncmp((const char*)(buffer->content + buffer->offset), "null", 4) == 0)
            {
                item = cJSON_New_Item();
                if (item) item->type = cJSON_NULL;
                buffer->offset += 4;
            }
            break;
        default:
            item = parse_number(buffer, return_parse_end);
            break;
    }

    if (!item && return_parse_end) *return_parse_end = buffer->content + buffer->offset;
    return item;
}

/* 公共解析函数 */
cJSON *cJSON_Parse(const char *value)
{
    return cJSON_ParseWithOpts(value, NULL, 1);
}

cJSON *cJSON_ParseWithOpts(const char *value, const char **return_parse_end, cJSON_bool require_null_terminated)
{
    parse_buffer buffer;
    cJSON *item = NULL;

    if (!value) return NULL;

    buffer.content = (const unsigned char*)value;
    buffer.length = strlen(value);
    buffer.offset = 0;
    buffer.depth = 0;

    global_error_ptr = NULL;

    item = parse_value(&buffer, return_parse_end);

    if (!item)
    {
        global_error_ptr = (const char*)(buffer.content + buffer.offset);
    }

    return item;
}

cJSON *cJSON_ParseWithLength(const char *value, size_t buffer_length)
{
    return cJSON_ParseWithLengthOpts(value, buffer_length, NULL, 0);
}

cJSON *cJSON_ParseWithLengthOpts(const char *value, size_t buffer_length, const char **return_parse_end, cJSON_bool require_null_terminated)
{
    parse_buffer buffer;
    cJSON *item = NULL;

    if (!value || buffer_length == 0) return NULL;

    buffer.content = (const unsigned char*)value;
    buffer.length = buffer_length;
    buffer.offset = 0;
    buffer.depth = 0;

    global_error_ptr = NULL;

    item = parse_value(&buffer, return_parse_end);

    if (!item)
    {
        global_error_ptr = (const char*)(buffer.content + buffer.offset);
    }

    return item;
}

const char *cJSON_GetErrorPtr(void)
{
    return global_error_ptr;
}

/* 获取数组大小 */
int cJSON_GetArraySize(const cJSON *array)
{
    int count = 0;
    cJSON *child = NULL;

    if (!array || array->type != cJSON_Array) return 0;

    child = array->child;
    while (child)
    {
        count++;
        child = child->next;
    }

    return count;
}

/* 获取数组项 */
cJSON *cJSON_GetArrayItem(const cJSON *array, int index)
{
    cJSON *child = NULL;
    int i = 0;

    if (!array || array->type != cJSON_Array || index < 0) return NULL;

    child = array->child;
    while (child && i < index)
    {
        child = child->next;
        i++;
    }

    return child;
}

/* 获取对象项（大小写不敏感） */
cJSON *cJSON_GetObjectItem(const cJSON * const object, const char * const string)
{
    return cJSON_GetObjectItemCaseSensitive(object, string);
}

/* 获取对象项（大小写敏感） */
cJSON *cJSON_GetObjectItemCaseSensitive(const cJSON * const object, const char * const string)
{
    cJSON *child = NULL;

    if (!object || object->type != cJSON_Object || !string) return NULL;

    child = object->child;
    while (child)
    {
        if (child->string && strcmp(child->string, string) == 0)
        {
            return child;
        }
        child = child->next;
    }

    return NULL;
}

cJSON_bool cJSON_HasObjectItem(const cJSON *object, const char *string)
{
    return cJSON_GetObjectItem(object, string) != NULL;
}

/* 类型检查 */
cJSON_bool cJSON_IsInvalid(const cJSON * const item) { return !item || (item->type & 0xFF) == cJSON_Invalid; }
cJSON_bool cJSON_IsFalse(const cJSON * const item) { return item ? (item->type & 0xFF) == cJSON_False : 0; }
cJSON_bool cJSON_IsTrue(const cJSON * const item) { return item ? (item->type & 0xFF) == cJSON_True : 0; }
cJSON_bool cJSON_IsBool(const cJSON * const item) { return item ? (item->type & (cJSON_True | cJSON_False)) != 0 : 0; }
cJSON_bool cJSON_IsNull(const cJSON * const item) { return item ? (item->type & 0xFF) == cJSON_NULL : 0; }
cJSON_bool cJSON_IsNumber(const cJSON * const item) { return item ? (item->type & 0xFF) == cJSON_Number : 0; }
cJSON_bool cJSON_IsString(const cJSON * const item) { return item ? (item->type & 0xFF) == cJSON_String : 0; }
cJSON_bool cJSON_IsArray(const cJSON * const item) { return item ? (item->type & 0xFF) == cJSON_Array : 0; }
cJSON_bool cJSON_IsObject(const cJSON * const item) { return item ? (item->type & 0xFF) == cJSON_Object : 0; }
cJSON_bool cJSON_IsRaw(const cJSON * const item) { return item ? (item->type & 0xFF) == cJSON_Raw : 0; }

/* 获取值 */
char *cJSON_GetStringValue(const cJSON * const item)
{
    if (!item || (item->type & 0xFF) != cJSON_String) return NULL;
    return item->valuestring;
}

double cJSON_GetNumberValue(const cJSON * const item)
{
    if (!item || (item->type & 0xFF) != cJSON_Number) return 0.0;
    return item->valuedouble;
}

/* 创建节点 */
cJSON *cJSON_CreateNull(void)
{
    cJSON *item = cJSON_New_Item();
    if (item) item->type = cJSON_NULL;
    return item;
}

cJSON *cJSON_CreateTrue(void)
{
    cJSON *item = cJSON_New_Item();
    if (item) item->type = cJSON_True;
    return item;
}

cJSON *cJSON_CreateFalse(void)
{
    cJSON *item = cJSON_New_Item();
    if (item) item->type = cJSON_False;
    return item;
}

cJSON *cJSON_CreateBool(cJSON_bool boolean)
{
    cJSON *item = cJSON_New_Item();
    if (item) item->type = boolean ? cJSON_True : cJSON_False;
    return item;
}

cJSON *cJSON_CreateNumber(double num)
{
    cJSON *item = cJSON_New_Item();
    if (item)
    {
        item->type = cJSON_Number;
        item->valuedouble = num;
        item->valueint = (int)num;
    }
    return item;
}

cJSON *cJSON_CreateString(const char *string)
{
    cJSON *item = cJSON_New_Item();
    if (item && string)
    {
        item->type = cJSON_String;
        item->valuestring = strdup(string);
        if (!item->valuestring)
        {
            free(item);
            return NULL;
        }
    }
    return item;
}

cJSON *cJSON_CreateArray(void)
{
    cJSON *item = cJSON_New_Item();
    if (item) item->type = cJSON_Array;
    return item;
}

cJSON *cJSON_CreateObject(void)
{
    cJSON *item = cJSON_New_Item();
    if (item) item->type = cJSON_Object;
    return item;
}

/* 添加到数组 */
void cJSON_AddItemToArray(cJSON *array, cJSON *item)
{
    cJSON *child = NULL;

    if (!array || !item) return;

    child = array->child;
    if (!child)
    {
        array->child = item;
    }
    else
    {
        while (child->next) child = child->next;
        child->next = item;
        item->prev = child;
    }
}

/* 添加到对象 */
void cJSON_AddItemToObject(cJSON *object, const char *string, cJSON *item)
{
    if (!object || !string || !item) return;

    if (item->string) free(item->string);
    item->string = strdup(string);

    cJSON_AddItemToArray(object, item);
}

/* 辅助函数 */
cJSON* cJSON_AddNullToObject(cJSON * const object, const char * const name)
{
    cJSON *item = cJSON_CreateNull();
    if (item) cJSON_AddItemToObject(object, name, item);
    return item;
}

cJSON* cJSON_AddTrueToObject(cJSON * const object, const char * const name)
{
    cJSON *item = cJSON_CreateTrue();
    if (item) cJSON_AddItemToObject(object, name, item);
    return item;
}

cJSON* cJSON_AddFalseToObject(cJSON * const object, const char * const name)
{
    cJSON *item = cJSON_CreateFalse();
    if (item) cJSON_AddItemToObject(object, name, item);
    return item;
}

cJSON* cJSON_AddBoolToObject(cJSON * const object, const char * const name, const cJSON_bool boolean)
{
    cJSON *item = cJSON_CreateBool(boolean);
    if (item) cJSON_AddItemToObject(object, name, item);
    return item;
}

cJSON* cJSON_AddNumberToObject(cJSON * const object, const char * const name, const double number)
{
    cJSON *item = cJSON_CreateNumber(number);
    if (item) cJSON_AddItemToObject(object, name, item);
    return item;
}

cJSON* cJSON_AddStringToObject(cJSON * const object, const char * const name, const char * const string)
{
    cJSON *item = cJSON_CreateString(string);
    if (item) cJSON_AddItemToObject(object, name, item);
    return item;
}

cJSON* cJSON_AddObjectToObject(cJSON * const object, const char * const name)
{
    cJSON *item = cJSON_CreateObject();
    if (item) cJSON_AddItemToObject(object, name, item);
    return item;
}

cJSON* cJSON_AddArrayToObject(cJSON * const object, const char * const name)
{
    cJSON *item = cJSON_CreateArray();
    if (item) cJSON_AddItemToObject(object, name, item);
    return item;
}

/* 设置数字值 */
double cJSON_SetNumberHelper(cJSON *object, double number)
{
    if (!object) return number;
    object->valuedouble = number;
    object->valueint = (int)number;
    return number;
}

/* 打印（简化版） */
static int print_string_ptr(const char *str, char *output, int offset, int max_len)
{
    const char *ptr = str;
    int len = 0;

    if (!str) return offset;

    if (offset < max_len - 1) output[offset++] = '"';

    while (*ptr && offset < max_len - 2)
    {
        if (*ptr == '"' || *ptr == '\\' || *ptr < 32)
        {
            if (offset < max_len - 3) output[offset++] = '\\';
            switch (*ptr)
            {
                case '\\': output[offset++] = '\\'; break;
                case '"': output[offset++] = '"'; break;
                case '\b': output[offset++] = 'b'; break;
                case '\f': output[offset++] = 'f'; break;
                case '\n': output[offset++] = 'n'; break;
                case '\r': output[offset++] = 'r'; break;
                case '\t': output[offset++] = 't'; break;
                default:
                    if (offset < max_len - 5)
                    {
                        sprintf(output + offset, "u%04x", (unsigned char)*ptr);
                        offset += 5;
                    }
                    break;
            }
        }
        else
        {
            output[offset++] = *ptr;
        }
        ptr++;
    }

    if (offset < max_len - 1) output[offset++] = '"';
    output[offset] = '\0';

    return offset;
}

static int print_value(const cJSON *item, char *output, int offset, int max_len, int depth, int fmt)
{
    int i = 0;
    cJSON *child = NULL;

    if (!item || offset >= max_len - 1) return offset;

    switch (item->type & 0xFF)
    {
        case cJSON_NULL:
            offset += sprintf(output + offset, "null");
            break;
        case cJSON_True:
            offset += sprintf(output + offset, "true");
            break;
        case cJSON_False:
            offset += sprintf(output + offset, "false");
            break;
        case cJSON_Number:
            if (item->valuedouble == (double)item->valueint &&
                fabs(item->valuedouble) < 1e10)
            {
                offset += sprintf(output + offset, "%d", item->valueint);
            }
            else
            {
                offset += sprintf(output + offset, "%g", item->valuedouble);
            }
            break;
        case cJSON_String:
            offset = print_string_ptr(item->valuestring, output, offset, max_len);
            break;
        case cJSON_Array:
            if (offset < max_len - 1) output[offset++] = '[';
            child = item->child;
            while (child)
            {
                if (fmt)
                {
                    if (offset < max_len - 1) output[offset++] = '\n';
                    for (i = 0; i < depth + 1 && offset < max_len - 1; i++) output[offset++] = '\t';
                }
                offset = print_value(child, output, offset, max_len, depth + 1, fmt);
                child = child->next;
                if (child && offset < max_len - 1) output[offset++] = ',';
            }
            if (fmt && item->child)
            {
                if (offset < max_len - 1) output[offset++] = '\n';
                for (i = 0; i < depth && offset < max_len - 1; i++) output[offset++] = '\t';
            }
            if (offset < max_len - 1) output[offset++] = ']';
            break;
        case cJSON_Object:
            if (offset < max_len - 1) output[offset++] = '{';
            child = item->child;
            while (child)
            {
                if (fmt)
                {
                    if (offset < max_len - 1) output[offset++] = '\n';
                    for (i = 0; i < depth + 1 && offset < max_len - 1; i++) output[offset++] = '\t';
                }
                offset = print_string_ptr(child->string, output, offset, max_len);
                if (offset < max_len - 1) output[offset++] = ':';
                if (fmt && offset < max_len - 1) output[offset++] = ' ';
                offset = print_value(child, output, offset, max_len, depth + 1, fmt);
                child = child->next;
                if (child && offset < max_len - 1) output[offset++] = ',';
            }
            if (fmt && item->child)
            {
                if (offset < max_len - 1) output[offset++] = '\n';
                for (i = 0; i < depth && offset < max_len - 1; i++) output[offset++] = '\t';
            }
            if (offset < max_len - 1) output[offset++] = '}';
            break;
        default:
            break;
    }

    output[offset] = '\0';
    return offset;
}

char *cJSON_Print(const cJSON *item)
{
    char *output = (char*)malloc(4096);
    if (!output) return NULL;
    memset(output, 0, 4096);
    print_value(item, output, 0, 4096, 0, 1);
    return output;
}

char *cJSON_PrintUnformatted(const cJSON *item)
{
    char *output = (char*)malloc(4096);
    if (!output) return NULL;
    memset(output, 0, 4096);
    print_value(item, output, 0, 4096, 0, 0);
    return output;
}

/* 初始化钩子 */
void cJSON_InitHooks(cJSON_Hooks* hooks)
{
    /* HepuOS uses libhepu malloc/free by default */
}

void *cJSON_malloc(size_t size)
{
    return malloc(size);
}

void cJSON_free(void *object)
{
    free(object);
}

/* 简化版strdup */
static char *hepu_strdup(const char *s)
{
    size_t len = strlen(s) + 1;
    char *dup = (char*)malloc(len);
    if (dup) memcpy(dup, s, len);
    return dup;
}

/* 覆盖strdup使用 */
#define strdup hepu_strdup
