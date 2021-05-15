// Copyright 2021-2021 The jdh99 Authors. All rights reserved.
// AT组件
// Authors: jdh99 <jdh821@163.com>

#include "tzat.h"

#include "lagan.h"
#include "tzmalloc.h"
#include "async.h"
#include "pt.h"
#include "tzfifo.h"
#include "tzlist.h"
#include "tztime.h"

#include <string.h>
#include <stdarg.h>
#include <stdio.h>

// 检查超时间隔.单位:ms
#define CHECK_TIMEOUT_INTERVAL 10

#pragma pack(1)

// 响应数据结构体
typedef struct {
    // 响应缓存.缓存中不包括回车换行符,用'\0'替代了
    char* buf;
    // 响应缓存最大字节数
    int bufSize;
    // 当前字节数
    int bufLen;

    // 设置的响应行数.如果设置为0,则接收到OK或者ERROR就会返回
    int setLineNum;
    // 接收到的行数
    int recvLineCounts;
    // 超时时间.单位:us
    uint64_t timeout;
    // 开始时间.单位:us
    uint64_t timeBegin;

    // 结果
    bool isWaitEnd;
    TZATRespResult result;
} tResp;

// URC是Unsolicited Result Code,即"非请求结果码"
typedef struct {
    char* prefix;
    int prefixLen;
    int comparePrefixNum;

    char* suffix;
    int suffixLen;
    int compareSuffixNum;
    
    TZBufferDynamic* buffer;
    int bufferSize;

    // 等待前缀标志
    bool isWaitPrefix;

    // 回调函数
    TZDataFunc callback;
} tUrcItem;

// 接收指定长度的数据
typedef struct {
    uint8_t* buf;
    // 最大字节数
    int bufSize;
    // 当前字节数
    int bufLen;

    // 超时时间.单位:us
    uint64_t timeout;
    // 开始时间.单位:us
    uint64_t timeBegin;

    // 结果
    bool isWaitEnd;
    TZATRespResult result;
    TZTADataFunc callback;
} tReceive;

#pragma pack(0)

static int mid = -1;
static TZDataFunc sendFunc = NULL;
static TZIsAllowSendFunc isAllowSendFunc = NULL;

static intptr_t fifo = 0;
static intptr_t urcList = 0;

// 等待响应数据
static tResp waitResp;
// 等待指定长度数据
static tReceive waitData;

static int checkFifo(void);
static void dealWaitResp(uint8_t byte);
static void dealUrcList(uint8_t byte);
static void dealUrcItem(uint8_t byte, tUrcItem* item);
static void dealWaitData(uint8_t byte);
static TZListNode* createNode(void);

static int checkTimeout(void);

// TZATLoad 模块载入
void TZATLoad(TZDataFunc send, TZIsAllowSendFunc isAllowSend) {
    waitResp.isWaitEnd = true;
    waitData.isWaitEnd = true;

    if (mid == -1) {
        mid = TZMallocRegister(0, TZAT_TAG, TZAT_MALLOC_SIZE);
        if (mid == -1) {
            LE(TZAT_TAG, "malloc register failed!");
            return;
        }
    }
    fifo = TZFifoCreate(mid, TZAT_FIFO_SIZE, 1);
    if (fifo == 0) {
        LE(TZAT_TAG, "create fifo failed!");
        return;
    }
    urcList = TZListCreateList(mid);
    if (urcList == 0) {
        LE(TZAT_TAG, "create list failed!");
        return;
    }

    sendFunc = send;
    isAllowSendFunc = isAllowSend;

    AsyncStart(checkFifo, ASYNC_NO_WAIT);
    AsyncStart(checkTimeout, CHECK_TIMEOUT_INTERVAL * ASYNC_MILLISECOND);
}

static int checkFifo(void) {
    static struct pt pt = {0};
    static uint8_t byte = 0;

    PT_BEGIN(&pt);

    while (1) {
        if (TZFifoRead(fifo, &byte, 1) != 1) {
            PT_EXIT(&pt);
        }

        if (waitResp.isWaitEnd == false) {
            dealWaitResp(byte);
            continue;
        }
        if (waitData.isWaitEnd == false) {
            dealWaitData(byte);
            continue;
        }
        dealUrcList(byte);
    }

    PT_END(&pt);
}

static void dealWaitResp(uint8_t byte) {
    // 接收标志.0:普通.1:换行.2:OK.3:ERROR
    int flag = 0;
    if (byte == '\n' && waitResp.bufLen >= 1 && waitResp.buf[waitResp.bufLen - 1] == '\r') {
        flag = 1;
    } else if (byte == 'K' && waitResp.bufLen >= 1 && waitResp.buf[waitResp.bufLen - 1] == 'O') {
        flag = 2;
    } else if (byte == 'R' && waitResp.bufLen >= 4 && memcmp(waitResp.buf + waitResp.bufLen - 4, "ERRO", 4) == 0) {
        flag = 3;
    }

    if (waitResp.setLineNum == 0) {
        // 判断OK和ERROR
        if (flag == 2 || flag == 3) {
            waitResp.result = TZAT_RESP_RESULT_OK;
            waitResp.isWaitEnd = true;
            return;
        }
    } else {
        // 判断行数是否够
        if (flag == 1) {
            waitResp.recvLineCounts++;
            waitResp.buf[waitResp.bufLen++] = '\0';

            if (waitResp.recvLineCounts >= waitResp.setLineNum) {
                waitResp.result = TZAT_RESP_RESULT_OK;
                waitResp.isWaitEnd = true;
            } else if (waitResp.bufLen >= waitResp.bufSize) {
                waitResp.result = TZAT_RESP_RESULT_LACK_OF_MEMORY;
                waitResp.isWaitEnd = true;
            }
            return;
        }
    }

    // 普通数据
    waitResp.buf[waitResp.bufLen++] = (char)byte;
    if (waitResp.bufLen >= waitResp.bufSize) {
        waitResp.result = TZAT_RESP_RESULT_LACK_OF_MEMORY;
        waitResp.isWaitEnd = true;
    }
}

static void dealUrcList(uint8_t byte) {
    TZListNode* node = TZListGetHeader(urcList);
    for (;;) {
        if (node == NULL) {
            break;
        }
        
        dealUrcItem(byte, (tUrcItem*)node->Data);
        node = node->Next;
    }
}

static void dealUrcItem(uint8_t byte, tUrcItem* item) {
    // 比较前缀
    if (item->isWaitPrefix) {
        if (byte == item->prefix[item->comparePrefixNum]) {
            item->comparePrefixNum++;
            if (item->comparePrefixNum >= item->prefixLen) {
                item->isWaitPrefix = false;
                item->comparePrefixNum = 0;
                item->buffer->len = 0;
            }
        } else {
            item->comparePrefixNum = 0;
        }
        return;
    }

    // 接收正文,同时比较后缀
    item->buffer->buf[item->buffer->len++] = byte;
    if (byte == item->suffix[item->compareSuffixNum]) {
        item->compareSuffixNum++;
        if (item->compareSuffixNum >= item->suffixLen) {
            // 接收成功
            item->callback(item->buffer->buf, item->buffer->len - item->suffixLen);
            item->isWaitPrefix = true;
            return;
        }
    } else {
        item->compareSuffixNum = 0;
    }

    if (item->buffer->len >= item->bufferSize) {
        // 达到缓存上限未接收到尾缀
        item->isWaitPrefix = true;
    }
}

static void dealWaitData(uint8_t byte) {
    // 普通数据
    waitData.buf[waitData.bufLen++] = byte;
    if (waitData.bufLen >= waitData.bufSize) {
        waitData.result = TZAT_RESP_RESULT_OK;
        waitData.isWaitEnd = true;

        waitData.callback(waitData.result, waitData.buf, waitData.bufLen);
        TZFree(waitData.buf);
        waitData.buf = NULL;
    }
}

static int checkTimeout(void) {
    static struct pt pt = {0};
    static uint64_t now = 0;

    PT_BEGIN(&pt);

    now = TZTimeGet();
    if (waitResp.isWaitEnd == false) {
        if (now - waitResp.timeBegin > waitResp.timeout) {
            waitResp.result = TZAT_RESP_RESULT_TIMEOUT;
            waitResp.isWaitEnd = true;
        }
    }
    if (waitData.isWaitEnd == false) {
        if (now - waitData.timeBegin > waitData.timeout) {
            waitData.result = TZAT_RESP_RESULT_TIMEOUT;
            waitData.isWaitEnd = true;

            waitData.callback(waitData.result, NULL, 0);
            TZFree(waitData.buf);
            waitData.buf = NULL;
        }
    }

    PT_END(&pt);
}

// TZATReceive 接收数据.用户模块接收到数据后需调用本函数
void TZATReceive(uint8_t* data, int size) {
    TZFifoWriteBatch(fifo, data, size);
}

// TZATCreateResp 创建响应结构体
// bufSize是响应数据最大字节数
// setLineNum是接收的响应行数.如果设置为0,则接收到OK或者ERROR就会返回
// timeout是接收超时时间.单位:ms
// 创建失败返回0,创建成功返回响应结构句柄.注意使用完毕必须释放句柄
intptr_t TZATCreateResp(int bufSize, int setLineNum, int timeout) {
    tResp* resp = (tResp*)TZMalloc(mid, sizeof(tResp));
    if (resp == NULL) {
        return 0;
    }
    resp->buf = TZMalloc(mid, bufSize);
    if (resp->buf == NULL) {
        TZFree(resp);
        return 0;
    }
    resp->bufSize = bufSize;
    resp->setLineNum = setLineNum;
    resp->timeout = (uint64_t)timeout * 1000;
    resp->isWaitEnd = true;
    return (intptr_t)resp;
}

// TZATDeleteResp 删除响应结构体.会释放结构体所占的内存空间
void TZATDeleteResp(intptr_t respHandle) {
    if (respHandle == 0) {
        return;
    }

    tResp* resp = (tResp*)respHandle;
    if (resp->buf != NULL) {
        TZFree(resp->buf);
    }
    TZFree(resp);
}

// TZATIsBusy 是否忙碌.忙碌时不应该发送命令或者接收指定长度数据
bool TZATIsBusy(void) {
    return (waitResp.isWaitEnd == false || waitData.isWaitEnd == false);
}

// TZATExecCmd 发送命令并接收响应.如果不需要响应,则respHandle可以设置为0
// 注意本函数需通过PT_WAIT_THREAD调用
int TZATExecCmd(intptr_t respHandle, char* cmd, ...) {
    static struct pt pt = {0};
    char buf[TZAT_CMD_LEN_MAX] = {0};
    va_list args;
   
    PT_BEGIN(&pt);

    if (TZATIsBusy()) {
        if (respHandle != 0) {
            tResp* resp = (tResp*)respHandle;
            resp->result = TZAT_RESP_RESULT_BUSY;
        }
        PT_EXIT(&pt);
    }

	va_start(args, cmd);
    int len = vsnprintf(buf, TZAT_CMD_LEN_MAX - 1, cmd, args);
    va_end(args);

    if (len > TZAT_CMD_LEN_MAX || len < 0) {
        LE(TZAT_TAG, "cmd len is too long!cmd:%s", cmd);
        PT_EXIT(&pt);
    }

    sendFunc((uint8_t*)buf, (int)strlen(buf));

    if (respHandle != 0) {
        waitResp = *(tResp*)respHandle;
        memset(waitResp.buf, 0, (size_t)waitResp.bufSize);
        waitResp.timeBegin = TZTimeGet();
        waitResp.isWaitEnd = false;
        PT_WAIT_UNTIL(&pt, waitResp.isWaitEnd);
        *(tResp*)respHandle = waitResp;
    }

    PT_END(&pt);
}

// TZATRespGetResult 读取响应结果
TZATRespResult TZATRespGetResult(intptr_t respHandle) {
    if (respHandle == 0) {
        return TZAT_RESP_RESULT_OTHER;
    }

    tResp* resp = (tResp*)respHandle;
    if (resp->isWaitEnd == false) {
        return TZAT_RESP_RESULT_OTHER;
    }
    return resp->result;
}

// TZATRespGetLineTotal 读取响应结果行数.如果返回的是0表示接收失败
int TZATRespGetLineTotal(intptr_t respHandle) {
    if (respHandle == 0) {
        return 0;
    }

    tResp* resp = (tResp*)respHandle;
    if (resp->isWaitEnd == false) {
        return 0;
    }
    return resp->recvLineCounts;
}

// TZATRespGetLine 读取指定行
// lineNumber是行号.行号从0开始
// 如果指定行不存在,则返回的是NULL
const char* TZATRespGetLine(intptr_t respHandle, int lineNumber) {
    if (respHandle == 0) {
        return NULL;
    }

    tResp* resp = (tResp*)respHandle;
    if (resp->isWaitEnd == false) {
        return NULL;
    }

    int offset = 0;
    int len = 0;
    for (int i = 0; i < lineNumber; i++) {
        len = (int)strlen(resp->buf + offset);
        if (len == 0) {
            return NULL;
        }
        offset += len + 1;
    }
    if (strlen(resp->buf + offset) == 0) {
        return NULL;
    }
    return resp->buf + offset;
}

// TZATRegisterUrc 注册URC回调函数
// prefix是前缀,suffix是后缀
// bufSize是正文数据最大字节数,正文不包括前缀和后缀
// callback是回调函数
bool TZATRegisterUrc(char* prefix, char* suffix, int bufSize, TZDataFunc callback) {
    if (bufSize == 0) {
        LE(TZAT_TAG, "register urc failed:buf size is 0");
        return false;
    }
    if (prefix == NULL || suffix == NULL || callback == NULL) {
        LE(TZAT_TAG, "register urc failed:prefix or suffix or callback is null");
        return false;
    }
    int prefixLen = (int)strlen(prefix);
    int suffixLen = (int)strlen(suffix);

    if (prefixLen == 0 || suffixLen == 0) {
        LE(TZAT_TAG, "register urc failed:prefix len or suffix len is 0");
        return false;
    }

    TZListNode* node = createNode();
    if (node == NULL) {
        LE(TZAT_TAG, "register urc failed:create node failed!");
        return false;
    }

    tUrcItem* item = (tUrcItem*)node->Data;
    item->prefixLen = prefixLen;
    item->suffixLen = suffixLen;
    item->prefix = TZMalloc(mid, prefixLen + 1);
    if (item->prefix == NULL) {
        LE(TZAT_TAG, "register urc failed:prefix malloc failed!");
        TZFree(node);
        return false;
    }
    strcpy(item->prefix, prefix);

    item->suffix = TZMalloc(mid, suffixLen + 1);
    if (item->suffix == NULL) {
        LE(TZAT_TAG, "register urc failed:suffix malloc failed!");
        TZFree(item->prefix);
        TZFree(node);
        return false;
    }
    strcpy(item->suffix, suffix);

    item->bufferSize = bufSize;
    item->buffer = (TZBufferDynamic*)TZMalloc(mid, (int)sizeof(TZBufferDynamic) + bufSize + 1);
    if (item->buffer == NULL) {
        LE(TZAT_TAG, "register urc failed:buffer malloc failed!");
        TZFree(item->prefix);
        TZFree(item->suffix);
        TZFree(node);
        return false;
    }

    item->callback = callback;
    item->isWaitPrefix = true;
    TZListAppend(urcList, node);
    return true;
}

static TZListNode* createNode(void) {
    TZListNode* node = TZListCreateNode(urcList);
    if (node == NULL) {
        return NULL;
    }
    node->Data = TZMalloc(mid, sizeof(tUrcItem));
    if (node->Data == NULL) {
        TZFree(node);
        return NULL;
    }
    return node;
} 

// TZATSetWaitDataCallback 设置接收指定长度数据的回调函数
// size是接收数据字节数.timeout是超时时间,单位:ms
bool TZATSetWaitDataCallback(int size, int timeout, TZTADataFunc callback) {
    if (TZATIsBusy()) {
        return false;
    }

    if (size == 0 || timeout == 0 || callback == NULL) {
        LE(TZAT_TAG, "set wait data callback failed!size or timeout is 0 or callback is NULL:%d %d", size, timeout);
        return false;
    }

    if (waitData.buf != NULL) {
        TZFree(waitData.buf);
        waitData.buf = NULL;
    }
    waitData.buf = TZMalloc(mid, size);
    if (waitData.buf == NULL) {
        LE(TZAT_TAG, "set wait data callback failed!malloc buf failed,size:%d", size);
        return false;
    }
    waitData.bufSize = size;
    waitData.bufLen = 0;
    waitData.isWaitEnd = false;
    waitData.timeBegin = TZTimeGet();
    waitData.timeout = (uint64_t)timeout * 1000;
    waitData.callback = callback;
    return true;
}
