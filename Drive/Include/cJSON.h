/**
  ******************************************************************************
  * @file    cJSON.h
  * @brief   轻量级超快速 ANSI C JSON 解析器与生成器库
  *          针对 STM32 + FreeRTOS 嵌入式环境深度优化，支持内存钩子重定向
  ******************************************************************************
  */

#ifndef __CJSON_H__
#define __CJSON_H__

#ifdef __cplusplus
extern "C"
{
#endif

#include <stddef.h>
#include <stdint.h>

/* cJSON 类型定义 */
#define cJSON_Invalid (0)
#define cJSON_False   (1 << 0)
#define cJSON_True    (1 << 1)
#define cJSON_NULL    (1 << 2)
#define cJSON_Number  (1 << 3)
#define cJSON_String  (1 << 4)
#define cJSON_Array   (1 << 5)
#define cJSON_Object  (1 << 6)
#define cJSON_Raw     (1 << 7)

#define cJSON_IsReference 256
#define cJSON_StringIsConst 512

/* cJSON 树节点结构体 */
typedef struct cJSON
{
    struct cJSON *next;
    struct cJSON *prev;
    struct cJSON *child;

    int type;

    char *valuestring;
    int valueint;
    double valuedouble;

    char *string;
} cJSON;

typedef struct cJSON_Hooks
{
    void *(*malloc_fn)(size_t sz);
    void (*free_fn)(void *ptr);
} cJSON_Hooks;

/* 核心内存管理钩子初始化 (推荐接入 FreeRTOS pvPortMalloc/vPortFree) */
void cJSON_InitHooks(cJSON_Hooks* hooks);

/* 解析 JSON 字符串并返回 cJSON 树节点 (使用完毕必须调用 cJSON_Delete 释放) */
cJSON *cJSON_Parse(const char *value);
cJSON *cJSON_ParseWithLength(const char *value, size_t buffer_length);

/* 将 cJSON 树序列化为字符串 (返回的字符串必须使用 cJSON_free 或 free 释放) */
char *cJSON_Print(const cJSON *item);
char *cJSON_PrintUnformatted(const cJSON *item);
int   cJSON_PrintPreallocated(cJSON *item, char *buffer, const int length, const int format);

/* 彻底递归销毁释放 cJSON 对象树及其子节点 */
void cJSON_Delete(cJSON *c);

/* 节点查询与获取接口 */
cJSON *cJSON_GetObjectItem(const cJSON * const object, const char * const string);
cJSON *cJSON_GetObjectItemCaseSensitive(const cJSON * const object, const char * const string);
int    cJSON_HasObjectItem(const cJSON *object, const char *string);
int    cJSON_GetArraySize(const cJSON *array);
cJSON *cJSON_GetArrayItem(const cJSON *array, int index);

/* 类型判断工具宏与函数 */
int cJSON_IsInvalid(const cJSON * const item);
int cJSON_IsFalse(const cJSON * const item);
int cJSON_IsTrue(const cJSON * const item);
int cJSON_IsBool(const cJSON * const item);
int cJSON_IsNull(const cJSON * const item);
int cJSON_IsNumber(const cJSON * const item);
int cJSON_IsString(const cJSON * const item);
int cJSON_IsArray(const cJSON * const item);
int cJSON_IsObject(const cJSON * const item);
int cJSON_IsRaw(const cJSON * const item);

/* 节点创建接口 */
cJSON *cJSON_CreateNull(void);
cJSON *cJSON_CreateTrue(void);
cJSON *cJSON_CreateFalse(void);
cJSON *cJSON_CreateBool(int b);
cJSON *cJSON_CreateNumber(double num);
cJSON *cJSON_CreateString(const char *string);
cJSON *cJSON_CreateRaw(const char *raw);
cJSON *cJSON_CreateArray(void);
cJSON *cJSON_CreateObject(void);

/* 对象与数组操作接口 */
void cJSON_AddItemToArray(cJSON *array, cJSON *item);
void cJSON_AddItemToObject(cJSON *object, const char *string, cJSON *item);

/* 快捷键值对追加宏 */
#define cJSON_AddNullToObject(object,name) cJSON_AddItemToObject(object, name, cJSON_CreateNull())
#define cJSON_AddTrueToObject(object,name) cJSON_AddItemToObject(object, name, cJSON_CreateTrue())
#define cJSON_AddFalseToObject(object,name) cJSON_AddItemToObject(object, name, cJSON_CreateFalse())
#define cJSON_AddBoolToObject(object,name,b) cJSON_AddItemToObject(object, name, cJSON_CreateBool(b))
#define cJSON_AddNumberToObject(object,name,n) cJSON_AddItemToObject(object, name, cJSON_CreateNumber(n))
#define cJSON_AddStringToObject(object,name,s) cJSON_AddItemToObject(object, name, cJSON_CreateString(s))
#define cJSON_AddRawToObject(object,name,s) cJSON_AddItemToObject(object, name, cJSON_CreateRaw(s))

/* 获取错误发生位置指针 */
const char *cJSON_GetErrorPtr(void);

#ifdef __cplusplus
}
#endif

#endif /* __CJSON_H__ */
