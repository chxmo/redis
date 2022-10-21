/* Hash Tables Implementation.
 *
 * This file implements in-memory hash tables with insert/del/replace/find/
 * get-random-element operations. Hash tables will auto-resize if needed
 * tables of power of two in size are used, collisions are handled by
 * chaining. See the source code for more information... :)
 *
 * Copyright (c) 2006-2012, Salvatore Sanfilippo <antirez at gmail dot com>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 *   * Redistributions of source code must retain the above copyright notice,
 *     this list of conditions and the following disclaimer.
 *   * Redistributions in binary form must reproduce the above copyright
 *     notice, this list of conditions and the following disclaimer in the
 *     documentation and/or other materials provided with the distribution.
 *   * Neither the name of Redis nor the names of its contributors may be used
 *     to endorse or promote products derived from this software without
 *     specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#ifndef __DICT_H
#define __DICT_H

#include "mt19937-64.h"
#include <limits.h>
#include <stdint.h>
#include <stdlib.h>

#define DICT_OK 0
#define DICT_ERR 1

/* Unused arguments generate annoying warnings... */
#define DICT_NOTUSED(V) ((void) V)

/**
 * 哈希表节点结构
 */
typedef struct dictEntry {

    /** 键值对中的键 **/
    void *key;
    /**
     * 键值对中的值
     * 共用体，内部成员共用一段内存，同一时刻只能保存一个成员数值
     * 此处代表了字典可存储的4种数类型：指针、无符号整数、有符号整数、双精度等类型
     */
    union {
        void *val;
        uint64_t u64;
        int64_t s64;
        double d;
    } v;
    /**指向下一个哈希节点，形成链表，以此来解决键冲突的问题，即链表法**/
    struct dictEntry *next;
} dictEntry;

/**
 * 类型特定函数，表示字典中特定的函数方法
 * - (*function) 函数指针
 * - *(*function) 函数指针的指针
 * @see server.c 中的dbDictType定义的函数与具体实现
 * **/
typedef struct dictType {
    /** hash函数 为key生成hash **/
    uint64_t (*hashFunction)(const void *key);
    /** key的复制函数 **/
    void *(*keyDup)(void *privdata, const void *key);
    /** value的复制函数 **/
    void *(*valDup)(void *privdata, const void *obj);
    /** key的比较函数 **/
    int (*keyCompare)(void *privdata, const void *key1, const void *key2);
    /** key的销毁函数 **/
    void (*keyDestructor)(void *privdata, void *key);
    /** value的销毁函数 **/
    void (*valDestructor)(void *privdata, void *obj);
    /** 字典拓展时是否允许内存分配函数**/
    int (*expandAllowed)(size_t moreMem, double usedRatio);
} dictType;

/* This is our hash table structure. Every dictionary has two of this as we
 * implement incremental rehashing, for the old to the new table. */
/**
 * 哈希表结构
 * - dictEntry 表明指针指向的数据类型是哈希节点结构
 * - **table 二级指针，表明table是一个指向指针的指针 从而组合成了一个数组
 * **/
typedef struct dictht {
    /** 哈希表数组 **/
    dictEntry **table;
    /** 哈希表大小，是2的整数次幂 **/
    unsigned long size;
    /** 哈希表大小掩码，用于计算索引值，总是等于size != 0 时 size-1，通过表达式(hash & sizemask)即可计算出所属的槽**/
    unsigned long sizemask;
    /** 哈希表已有节点数量**/
    unsigned long used;
} dictht;

/**
 * 字典结构
 */
typedef struct dict {
    /** 指向类型特定函数的指针，内部保存了一簇用于操作特定类型键值对的函数 **/
    dictType *type;
    /** 私有数据，保存了需要传给那些类型特定函数的可选参数**/
    void *privdata;
    /** 两张哈希表，真正使用的是ht[0],ht[1]用于rehash 此实现的是增量rehash，避免了一次rehash耗时过长而产生阻塞**/
    dictht ht[2];
    /** rehash索引，用于记录rehash的进度，当为-1时，表明不在进行rehash **/
    long rehashidx; /* rehashing not in progress if rehashidx == -1 */
    /** 记录当前rehash迭代次数 **/
    int16_t pauserehash; /* If >0 rehashing is paused (<0 indicates coding error) */
} dict;

/* If safe is set to 1 this is a safe iterator, that means, you can call
 * dictAdd, dictFind, and other functions against the dictionary even while
 * iterating. Otherwise it is a non safe iterator, and only dictNext()
 * should be called while iterating. */
/**
 * 字典迭代器结构
 * safe = 1 线程安全，迭代过程可以执行任何操作
 * safe = 0 线程不安全，只允许dictNext方法执行
 */
typedef struct dictIterator {
    /** 字典指针**/
    dict *d;
    /** 索引 **/
    long index;
    /** table为dictht的索引为0或1，safe表示是否需要安全遍历**/
    int table, safe;
    /** 当前节点与下一个节点 **/
    dictEntry *entry, *nextEntry;
    /**
     * 非安全遍历时检测是否非法使用的状态值（64位整数）
     * 非安全迭代器初始化时计算该值
     * 释放迭代器时在此计算并比较该值，若不同则说明存在非法操作
     */
    /* unsafe iterator fingerprint for misuse detection. */
    long long fingerprint;
} dictIterator;

typedef void (dictScanFunction)(void *privdata, const dictEntry *de);
typedef void (dictScanBucketFunction)(void *privdata, dictEntry **bucketref);

/* This is the initial size of every hash table */
#define DICT_HT_INITIAL_SIZE     4

/* ------------------------------- Macros ------------------------------------*/
/**
 * 随处可见的反斜杠"\" 表示继续符号，起连接的作用，即一行放不下了，故用该符号连接
 */
#define dictFreeVal(d, entry) \
    if ((d)->type->valDestructor) \
        (d)->type->valDestructor((d)->privdata, (entry)->v.val)

#define dictSetVal(d, entry, _val_) do { \
    if ((d)->type->valDup) \
        (entry)->v.val = (d)->type->valDup((d)->privdata, _val_); \
    else \
        (entry)->v.val = (_val_); \
} while(0)

#define dictSetSignedIntegerVal(entry, _val_) \
    do { (entry)->v.s64 = _val_; } while(0)

#define dictSetUnsignedIntegerVal(entry, _val_) \
    do { (entry)->v.u64 = _val_; } while(0)

#define dictSetDoubleVal(entry, _val_) \
    do { (entry)->v.d = _val_; } while(0)

#define dictFreeKey(d, entry) \
    if ((d)->type->keyDestructor) \
        (d)->type->keyDestructor((d)->privdata, (entry)->key)

#define dictSetKey(d, entry, _key_) do { \
    if ((d)->type->keyDup) \
        (entry)->key = (d)->type->keyDup((d)->privdata, _key_); \
    else \
        (entry)->key = (_key_); \
} while(0)

#define dictCompareKeys(d, key1, key2) \
    (((d)->type->keyCompare) ? \
        (d)->type->keyCompare((d)->privdata, key1, key2) : \
        (key1) == (key2))

#define dictHashKey(d, key) (d)->type->hashFunction(key)
#define dictGetKey(he) ((he)->key)
#define dictGetVal(he) ((he)->v.val)
#define dictGetSignedIntegerVal(he) ((he)->v.s64)
#define dictGetUnsignedIntegerVal(he) ((he)->v.u64)
#define dictGetDoubleVal(he) ((he)->v.d)
#define dictSlots(d) ((d)->ht[0].size+(d)->ht[1].size)
#define dictSize(d) ((d)->ht[0].used+(d)->ht[1].used)
#define dictIsRehashing(d) ((d)->rehashidx != -1)
#define dictPauseRehashing(d) (d)->pauserehash++
#define dictResumeRehashing(d) (d)->pauserehash--

/* If our unsigned long type can store a 64 bit number, use a 64 bit PRNG. */
#if ULONG_MAX >= 0xffffffffffffffff
#define randomULong() ((unsigned long) genrand64_int64())
#else
#define randomULong() random()
#endif

/* API */
dict *dictCreate(dictType *type, void *privDataPtr);
int dictExpand(dict *d, unsigned long size);
int dictTryExpand(dict *d, unsigned long size);
int dictAdd(dict *d, void *key, void *val);
dictEntry *dictAddRaw(dict *d, void *key, dictEntry **existing);
dictEntry *dictAddOrFind(dict *d, void *key);
int dictReplace(dict *d, void *key, void *val);
int dictDelete(dict *d, const void *key);
dictEntry *dictUnlink(dict *ht, const void *key);
void dictFreeUnlinkedEntry(dict *d, dictEntry *he);
void dictRelease(dict *d);
dictEntry * dictFind(dict *d, const void *key);
void *dictFetchValue(dict *d, const void *key);
int dictResize(dict *d);
dictIterator *dictGetIterator(dict *d);
dictIterator *dictGetSafeIterator(dict *d);
dictEntry *dictNext(dictIterator *iter);
void dictReleaseIterator(dictIterator *iter);
dictEntry *dictGetRandomKey(dict *d);
dictEntry *dictGetFairRandomKey(dict *d);
unsigned int dictGetSomeKeys(dict *d, dictEntry **des, unsigned int count);
void dictGetStats(char *buf, size_t bufsize, dict *d);
uint64_t dictGenHashFunction(const void *key, int len);
uint64_t dictGenCaseHashFunction(const unsigned char *buf, int len);
void dictEmpty(dict *d, void(callback)(void*));
void dictEnableResize(void);
void dictDisableResize(void);
int dictRehash(dict *d, int n);
int dictRehashMilliseconds(dict *d, int ms);
void dictSetHashFunctionSeed(uint8_t *seed);
uint8_t *dictGetHashFunctionSeed(void);
unsigned long dictScan(dict *d, unsigned long v, dictScanFunction *fn, dictScanBucketFunction *bucketfn, void *privdata);
uint64_t dictGetHash(dict *d, const void *key);
dictEntry **dictFindEntryRefByPtrAndHash(dict *d, const void *oldptr, uint64_t hash);

/* Hash table types */
extern dictType dictTypeHeapStringCopyKey;
extern dictType dictTypeHeapStrings;
extern dictType dictTypeHeapStringCopyKeyValue;

#ifdef REDIS_TEST
int dictTest(int argc, char *argv[], int accurate);
#endif

#endif /* __DICT_H */
