/**
  ******************************************************************************
  * @file    cJSON.c
  * @brief   轻量级超快速 ANSI C JSON 解析器与生成器实现
  *          为 STM32 架构深度调优，支持内存钩子挂接与健壮性内存释放
  ******************************************************************************
  */

#include "cJSON.h"
#include <string.h>
#include <stdio.h>
#include <math.h>
#include <stdlib.h>
#include <float.h>
#include <limits.h>
#include <ctype.h>

static const char *ep = NULL;

const char *cJSON_GetErrorPtr(void)
{
    return ep;
}

/* 默认使用系统 malloc 与 free，可由 cJSON_InitHooks 重定向至 FreeRTOS pvPortMalloc/vPortFree */
static void *(*cJSON_malloc)(size_t sz) = malloc;
static void (*cJSON_free)(void *ptr) = free;

static char* cJSON_strdup(const char* str)
{
    size_t len;
    char* copy;

    if (!str) return NULL;
    len = strlen(str) + 1;
    copy = (char*)cJSON_malloc(len);
    if (!copy) return NULL;
    memcpy(copy, str, len);
    return copy;
}

void cJSON_InitHooks(cJSON_Hooks* hooks)
{
    if (!hooks)
    {
        cJSON_malloc = malloc;
        cJSON_free = free;
        return;
    }
    cJSON_malloc = (hooks->malloc_fn) ? hooks->malloc_fn : malloc;
    cJSON_free = (hooks->free_fn) ? hooks->free_fn : free;
}

/* 内部构造基本节点 */
static cJSON *cJSON_New_Item(void)
{
    cJSON* node = (cJSON*)cJSON_malloc(sizeof(cJSON));
    if (node) memset(node, 0, sizeof(cJSON));
    return node;
}

/* 递归释放所有节点 */
void cJSON_Delete(cJSON *c)
{
    cJSON *next;
    while (c)
    {
        next = c->next;
        if (!(c->type & cJSON_IsReference) && c->child) cJSON_Delete(c->child);
        if (!(c->type & cJSON_IsReference) && c->valuestring) cJSON_free(c->valuestring);
        if (!(c->type & cJSON_StringIsConst) && c->string) cJSON_free(c->string);
        cJSON_free(c);
        c = next;
    }
}

/* 语法跳过空白字符 */
static const char *skip(const char *in)
{
    while (in && *in && ((unsigned char)*in <= 32)) in++;
    return in;
}

/* 解析数字 */
static const char *parse_number(cJSON *item, const char *num)
{
    double n = 0, sign = 1, scale = 0;
    int subscale = 0, signsubscale = 1;

    if (*num == '-') sign = -1, num++;
    if (*num == '0') num++;
    else if (*num >= '1' && *num <= '9')
    {
        do n = (n * 10.0) + (*num++ - '0');
        while (*num >= '0' && *num <= '9');
    }

    if (*num == '.' && num[1] >= '0' && num[1] <= '9')
    {
        num++;
        do
        {
            n = (n * 10.0) + (*num++ - '0');
            scale--;
        } while (*num >= '0' && *num <= '9');
    }

    if (*num == 'e' || *num == 'E')
    {
        num++;
        if (*num == '+') num++;
        else if (*num == '-') signsubscale = -1, num++;
        while (*num >= '0' && *num <= '9') subscale = (subscale * 10) + (*num++ - '0');
    }

    n = sign * n * pow(10.0, (scale + subscale * signsubscale));

    item->valuedouble = n;
    item->valueint = (int)n;
    item->type = cJSON_Number;
    return num;
}

/* 解析字符串 */
static const char *parse_string(cJSON *item, const char *str)
{
    const char *ptr = str + 1;
    char *ptr2;
    char *out;
    int len = 0;

    if (*str != '\"') { ep = str; return NULL; }

    while (*ptr != '\"' && *ptr && ++len)
    {
        if (*ptr++ == '\\') ptr++;
    }

    out = (char*)cJSON_malloc(len + 1);
    if (!out) return NULL;

    ptr = str + 1;
    ptr2 = out;
    while (*ptr != '\"' && *ptr)
    {
        if (*ptr != '\\') *ptr2++ = *ptr++;
        else
        {
            ptr++;
            switch (*ptr)
            {
                case 'b': *ptr2++ = '\b'; break;
                case 'f': *ptr2++ = '\f'; break;
                case 'n': *ptr2++ = '\n'; break;
                case 'r': *ptr2++ = '\r'; break;
                case 't': *ptr2++ = '\t'; break;
                case '\"':
                case '\\':
                case '/': *ptr2++ = *ptr; break;
                default: break;
            }
            ptr++;
        }
    }
    *ptr2 = 0;
    if (*ptr == '\"') ptr++;
    item->valuestring = out;
    item->type = cJSON_String;
    return ptr;
}

/* 前置声明 */
static const char *parse_value(cJSON *item, const char *value);
static const char *parse_array(cJSON *item, const char *value);
static const char *parse_object(cJSON *item, const char *value);

static const char *parse_value(cJSON *item, const char *value)
{
    if (!value) return NULL;
    if (!strncmp(value, "null", 4))  { item->type = cJSON_NULL; return value + 4; }
    if (!strncmp(value, "false", 5)) { item->type = cJSON_False; return value + 5; }
    if (!strncmp(value, "true", 4))  { item->type = cJSON_True; item->valueint = 1; return value + 4; }
    if (*value == '\"')              { return parse_string(item, value); }
    if (*value == '-' || (*value >= '0' && *value <= '9')) { return parse_number(item, value); }
    if (*value == '[')              { return parse_array(item, value); }
    if (*value == '{')              { return parse_object(item, value); }

    ep = value;
    return NULL;
}

static const char *parse_array(cJSON *item, const char *value)
{
    cJSON *child;
    if (*value != '[') { ep = value; return NULL; }

    item->type = cJSON_Array;
    value = skip(value + 1);
    if (*value == ']') return value + 1;

    item->child = child = cJSON_New_Item();
    if (!item->child) return NULL;
    value = skip(parse_value(child, skip(value)));
    if (!value) return NULL;

    while (*value == ',')
    {
        cJSON *new_item = cJSON_New_Item();
        if (!new_item) return NULL;
        child->next = new_item;
        new_item->prev = child;
        child = new_item;
        value = skip(parse_value(child, skip(value + 1)));
        if (!value) return NULL;
    }

    if (*value == ']') return value + 1;
    ep = value;
    return NULL;
}

static const char *parse_object(cJSON *item, const char *value)
{
    cJSON *child;
    if (*value != '{') { ep = value; return NULL; }

    item->type = cJSON_Object;
    value = skip(value + 1);
    if (*value == '}') return value + 1;

    item->child = child = cJSON_New_Item();
    if (!item->child) return NULL;
    value = skip(parse_string(child, skip(value)));
    if (!value) return NULL;
    child->string = child->valuestring;
    child->valuestring = NULL;
    if (*value != ':') { ep = value; return NULL; }
    value = skip(parse_value(child, skip(value + 1)));
    if (!value) return NULL;

    while (*value == ',')
    {
        cJSON *new_item = cJSON_New_Item();
        if (!new_item) return NULL;
        child->next = new_item;
        new_item->prev = child;
        child = new_item;
        value = skip(parse_string(child, skip(value + 1)));
        if (!value) return NULL;
        child->string = child->valuestring;
        child->valuestring = NULL;
        if (*value != ':') { ep = value; return NULL; }
        value = skip(parse_value(child, skip(value + 1)));
        if (!value) return NULL;
    }

    if (*value == '}') return value + 1;
    ep = value;
    return NULL;
}

cJSON *cJSON_Parse(const char *value)
{
    cJSON *c = cJSON_New_Item();
    ep = NULL;
    if (!c) return NULL;

    if (!parse_value(c, skip(value)))
    {
        cJSON_Delete(c);
        return NULL;
    }
    return c;
}

/* 序列化打印支持 */
typedef struct
{
    char *buffer;
    int length;
    int offset;
} printbuffer;

static char* ensure(printbuffer *p, int needed)
{
    char *newbuffer;
    int newsize;
    if (!p || !p->buffer) return NULL;
    needed += p->offset;
    if (needed <= p->length) return p->buffer + p->offset;

    newsize = p->length * 2;
    if (newsize < needed) newsize = needed + 64;
    newbuffer = (char*)cJSON_malloc(newsize);
    if (!newbuffer)
    {
        cJSON_free(p->buffer);
        p->length = 0;
        p->buffer = NULL;
        return NULL;
    }
    memcpy(newbuffer, p->buffer, p->length);
    cJSON_free(p->buffer);
    p->length = newsize;
    p->buffer = newbuffer;
    return newbuffer + p->offset;
}

static int print_value(const cJSON *item, printbuffer *p);

static int print_number(const cJSON *item, printbuffer *p)
{
    char *str;
    double d = item->valuedouble;
    if (fabs(((double)item->valueint) - d) <= DBL_EPSILON && d <= INT_MAX && d >= INT_MIN)
    {
        str = ensure(p, 21);
        if (str) sprintf(str, "%d", item->valueint);
    }
    else
    {
        str = ensure(p, 64);
        if (str)
        {
            if (fabs(floor(d) - d) <= DBL_EPSILON && fabs(d) < 1.0e60)
                sprintf(str, "%.0f", d);
            else if (fabs(d) < 1.0e-6 || fabs(d) > 1.0e9)
                sprintf(str, "%e", d);
            else
                sprintf(str, "%f", d);
        }
    }
    if (str) { p->offset += strlen(str); return 1; }
    return 0;
}

static int print_string_ptr(const char *str, printbuffer *p)
{
    const char *ptr;
    char *ptr2, *out;
    int len = 0;
    if (!str)
    {
        out = ensure(p, 3);
        if (!out) return 0;
        strcpy(out, "\"\"");
        p->offset += 2;
        return 1;
    }
    ptr = str;
    while (*ptr)
    {
        if ((unsigned char)*ptr < 32 || *ptr == '\"' || *ptr == '\\') len += 2;
        else len++;
        ptr++;
    }
    out = ensure(p, len + 3);
    if (!out) return 0;
    ptr2 = out;
    *ptr2++ = '\"';
    ptr = str;
    while (*ptr)
    {
        if ((unsigned char)*ptr > 31 && *ptr != '\"' && *ptr != '\\') *ptr2++ = *ptr++;
        else
        {
            *ptr2++ = '\\';
            switch (*ptr++)
            {
                case '\\': *ptr2++ = '\\'; break;
                case '\"': *ptr2++ = '\"'; break;
                case '\b': *ptr2++ = 'b'; break;
                case '\f': *ptr2++ = 'f'; break;
                case '\n': *ptr2++ = 'n'; break;
                case '\r': *ptr2++ = 'r'; break;
                case '\t': *ptr2++ = 't'; break;
                default: ptr2--; break;
            }
        }
    }
    *ptr2++ = '\"';
    *ptr2 = 0;
    p->offset += strlen(out);
    return 1;
}

static int print_array(const cJSON *item, printbuffer *p)
{
    char *ptr;
    cJSON *child = item->child;
    ptr = ensure(p, 2);
    if (!ptr) return 0;
    *ptr++ = '[';
    *ptr = 0;
    p->offset++;

    while (child)
    {
        if (!print_value(child, p)) return 0;
        if (child->next)
        {
            ptr = ensure(p, 2);
            if (!ptr) return 0;
            *ptr++ = ',';
            *ptr = 0;
            p->offset++;
        }
        child = child->next;
    }
    ptr = ensure(p, 2);
    if (!ptr) return 0;
    *ptr++ = ']';
    *ptr = 0;
    p->offset++;
    return 1;
}

static int print_object(const cJSON *item, printbuffer *p)
{
    char *ptr;
    cJSON *child = item->child;
    ptr = ensure(p, 2);
    if (!ptr) return 0;
    *ptr++ = '{';
    *ptr = 0;
    p->offset++;

    while (child)
    {
        if (!print_string_ptr(child->string, p)) return 0;
        ptr = ensure(p, 2);
        if (!ptr) return 0;
        *ptr++ = ':';
        *ptr = 0;
        p->offset++;

        if (!print_value(child, p)) return 0;
        if (child->next)
        {
            ptr = ensure(p, 2);
            if (!ptr) return 0;
            *ptr++ = ',';
            *ptr = 0;
            p->offset++;
        }
        child = child->next;
    }
    ptr = ensure(p, 2);
    if (!ptr) return 0;
    *ptr++ = '}';
    *ptr = 0;
    p->offset++;
    return 1;
}

static int print_value(const cJSON *item, printbuffer *p)
{
    char *out;
    if (!item) return 0;
    switch (item->type & 0xFF)
    {
        case cJSON_NULL:
            out = ensure(p, 5);
            if (!out) return 0;
            strcpy(out, "null");
            p->offset += 4;
            return 1;
        case cJSON_False:
            out = ensure(p, 6);
            if (!out) return 0;
            strcpy(out, "false");
            p->offset += 5;
            return 1;
        case cJSON_True:
            out = ensure(p, 5);
            if (!out) return 0;
            strcpy(out, "true");
            p->offset += 4;
            return 1;
        case cJSON_Number:
            return print_number(item, p);
        case cJSON_String:
            return print_string_ptr(item->valuestring, p);
        case cJSON_Array:
            return print_array(item, p);
        case cJSON_Object:
            return print_object(item, p);
        default:
            return 0;
    }
}

char *cJSON_PrintUnformatted(const cJSON *item)
{
    printbuffer p;
    p.buffer = (char*)cJSON_malloc(256);
    if (!p.buffer) return NULL;
    p.length = 256;
    p.offset = 0;
    p.buffer[0] = 0;

    if (!print_value(item, &p))
    {
        cJSON_free(p.buffer);
        return NULL;
    }
    return p.buffer;
}

char *cJSON_Print(const cJSON *item)
{
    return cJSON_PrintUnformatted(item);
}

/* 节点快速创建与属性查询 */
cJSON *cJSON_CreateNull(void)   { cJSON *item = cJSON_New_Item(); if (item) item->type = cJSON_NULL; return item; }
cJSON *cJSON_CreateTrue(void)   { cJSON *item = cJSON_New_Item(); if (item) { item->type = cJSON_True; item->valueint = 1; } return item; }
cJSON *cJSON_CreateFalse(void)  { cJSON *item = cJSON_New_Item(); if (item) item->type = cJSON_False; return item; }
cJSON *cJSON_CreateBool(int b)  { return b ? cJSON_CreateTrue() : cJSON_CreateFalse(); }
cJSON *cJSON_CreateNumber(double num)
{
    cJSON *item = cJSON_New_Item();
    if (item) { item->type = cJSON_Number; item->valuedouble = num; item->valueint = (int)num; }
    return item;
}
cJSON *cJSON_CreateString(const char *string)
{
    cJSON *item = cJSON_New_Item();
    if (item) { item->type = cJSON_String; item->valuestring = cJSON_strdup(string); }
    return item;
}
cJSON *cJSON_CreateArray(void)  { cJSON *item = cJSON_New_Item(); if (item) item->type = cJSON_Array; return item; }
cJSON *cJSON_CreateObject(void) { cJSON *item = cJSON_New_Item(); if (item) item->type = cJSON_Object; return item; }

void cJSON_AddItemToArray(cJSON *array, cJSON *item)
{
    cJSON *c;
    if (!item) return;
    if (!array->child) { array->child = item; }
    else
    {
        c = array->child;
        while (c && c->next) c = c->next;
        c->next = item;
        item->prev = c;
    }
}

void cJSON_AddItemToObject(cJSON *object, const char *string, cJSON *item)
{
    if (!item) return;
    if (item->string) cJSON_free(item->string);
    item->string = cJSON_strdup(string);
    cJSON_AddItemToArray(object, item);
}

cJSON *cJSON_GetObjectItem(const cJSON * const object, const char * const string)
{
    cJSON *c = object ? object->child : NULL;
    while (c && strcasecmp(c->string, string)) c = c->next;
    return c;
}

cJSON *cJSON_GetObjectItemCaseSensitive(const cJSON * const object, const char * const string)
{
    cJSON *c = object ? object->child : NULL;
    while (c && strcmp(c->string, string)) c = c->next;
    return c;
}

int cJSON_HasObjectItem(const cJSON *object, const char *string)
{
    return cJSON_GetObjectItem(object, string) ? 1 : 0;
}

int cJSON_IsNumber(const cJSON * const item) { return (item != NULL) && ((item->type & 0xFF) == cJSON_Number); }
int cJSON_IsString(const cJSON * const item) { return (item != NULL) && ((item->type & 0xFF) == cJSON_String); }
int cJSON_IsObject(const cJSON * const item) { return (item != NULL) && ((item->type & 0xFF) == cJSON_Object); }
int cJSON_IsArray(const cJSON * const item)  { return (item != NULL) && ((item->type & 0xFF) == cJSON_Array); }
