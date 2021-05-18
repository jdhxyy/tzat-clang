// Copyright 2021-2021 The jdh99 Authors. All rights reserved.
// AT���
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

// ��鳬ʱ���.��λ:ms
#define CHECK_TIMEOUT_INTERVAL 10

#pragma pack(1)

// ��Ӧ���ݽṹ��
typedef struct {
    // ��Ӧ����.�����в������س����з�,��'\0'�����
    char* buf;
    // ��Ӧ��������ֽ���
    int bufSize;
    // ��ǰ�ֽ���
    int bufLen;

    // ���õ���Ӧ����.�������Ϊ0,����յ�OK����ERROR�ͻ᷵��
    int setLineNum;
    // ���յ�������
    int recvLineCounts;
    // ��ʱʱ��.��λ:us
    uint64_t timeout;
    // ��ʼʱ��.��λ:us
    uint64_t timeBegin;

    // ���
    bool isWaitEnd;
    TZATRespResult result;
} tResp;

// URC��Unsolicited Result Code,��"����������"
typedef struct {
    char* prefix;
    int prefixLen;
    int comparePrefixNum;

    char* suffix;
    int suffixLen;
    int compareSuffixNum;
    
    TZBufferDynamic* buffer;
    int bufferSize;

    // �ȴ�ǰ׺��־
    bool isWaitPrefix;

    // �ص�����
    TZDataFunc callback;
} tUrcItem;

// ����ָ�����ȵ�����
typedef struct {
    uint8_t* buf;
    // ����ֽ���
    int bufSize;
    // ��ǰ�ֽ���
    int bufLen;

    // ��ʱʱ��.��λ:us
    uint64_t timeout;
    // ��ʼʱ��.��λ:us
    uint64_t timeBegin;

    // ���
    bool isWaitEnd;
    TZATRespResult result;
    TZTADataFunc callback;
} tReceive;

// AT�������
typedef struct {
    TZDataFunc send;
    TZIsAllowSendFunc isAllowSend;

    intptr_t fifo;
    intptr_t urcList;

    // �ȴ���Ӧ����
    tResp waitResp;
    // �ȴ�ָ����������
    tReceive waitData;

    // �û����õĽ�����
    char endSign;
} tObjItem;

#pragma pack(0)

static int mid = -1;
static intptr_t objList = 0;

static int checkFifo(void);
static void checkObjFifo(tObjItem* obj);
static void dealWaitResp(tObjItem* obj, uint8_t byte);
static void dealUrcList(tObjItem* obj, uint8_t byte);
static void dealUrcItem(uint8_t byte, tUrcItem* item);
static void dealWaitData(tObjItem* obj, uint8_t byte);
static int checkTimeout(void);
static void checkObjTimeout(tObjItem* obj, uint64_t now);
static TZListNode* createNode(intptr_t list, int itemSize);

// TZATSetMid �����ڴ�id
// ��������ñ�����.��ģ��ʹ��Ĭ���ڴ�ID
// �����ڵ���TZATCreate����ǰ���ñ�����,����ģ��ʹ��Ĭ���ڴ�ID
void TZATSetMid(int id) {
    if (mid == -1) {
        mid = id;
    }
}

// TZATCreate ����AT���
// send�Ǳ�������ͺ���.isAllowSend���Ƿ������ͺ���
// �����ɹ����ؾ��.ʧ�ܷ���0
intptr_t TZATCreate(TZDataFunc send, TZIsAllowSendFunc isAllowSend) {
    static bool isFirst = true;

    if (isFirst) {
        isFirst = false;

        if (mid == -1) {
            mid = TZMallocRegister(0, TZAT_TAG, TZAT_MALLOC_SIZE);
            if (mid == -1) {
                LE(TZAT_TAG, "create object failed!malloc register failed!");
                return 0;
            }
        }

        objList = TZListCreateList(mid);
        if (objList == 0) {
            LE(TZAT_TAG, "create object failed!create list failed!");
            return 0;
        }

        AsyncStart(checkFifo, ASYNC_NO_WAIT);
        AsyncStart(checkTimeout, CHECK_TIMEOUT_INTERVAL * ASYNC_MILLISECOND);
    }

    if (mid == -1 || objList == 0) {
        return 0;
    }

    TZListNode* node = createNode(objList, sizeof(tObjItem));
    if (node == NULL) {
        LE(TZAT_TAG, "create object failed!create node failed!");
        return 0;
    }

    tObjItem* obj = (tObjItem*)node->Data;
    obj->waitResp.isWaitEnd = true;
    obj->waitData.isWaitEnd = true;

    obj->fifo = TZFifoCreate(mid, TZAT_FIFO_SIZE, 1);
    if (obj->fifo == 0) {
        LE(TZAT_TAG, "create object failed!create fifo failed!");
        TZFree(obj);
        TZFree(node);
        return 0;
    }
    obj->urcList = TZListCreateList(mid);
    if (obj->urcList == 0) {
        LE(TZAT_TAG, "create object failed!create urc list failed!");
        TZFifoDelete(obj->fifo);
        TZFree(obj);
        TZFree(node);
        return 0;
    }

    obj->send = send;
    obj->isAllowSend = isAllowSend;
    obj->endSign = '\0';
    TZListAppend(objList, node);
    return (intptr_t)obj;
}

static int checkFifo(void) {
    static struct pt pt = {0};
    static TZListNode* node = NULL;

    PT_BEGIN(&pt);

    node = TZListGetHeader(objList);
    for (;;) {
        if (node == NULL) {
            break;
        }

        checkObjFifo((tObjItem*)node->Data);
        node = node->Next;
    }

    PT_END(&pt);
}

static void checkObjFifo(tObjItem* obj) {
    uint8_t byte = 0;
    for (;;) {
        if (TZFifoRead(obj->fifo, &byte, 1) != 1) {
            return;
        }

        if (obj->waitResp.isWaitEnd == false) {
            dealWaitResp(obj, byte);
            continue;
        }
        if (obj->waitData.isWaitEnd == false) {
            dealWaitData(obj, byte);
            continue;
        }
        dealUrcList(obj, byte);
    }
}

static void dealWaitResp(tObjItem* obj, uint8_t byte) {
    // ���ձ�־.0:��ͨ.1:����.2:OK.3:ERROR.4:�û�������
    int flag = 0;
    if (byte == '\n' && obj->waitResp.bufLen >= 1 && obj->waitResp.buf[obj->waitResp.bufLen - 1] == '\r') {
        flag = 1;
    } else if (byte == 'K' && obj->waitResp.bufLen >= 1 && obj->waitResp.buf[obj->waitResp.bufLen - 1] == 'O') {
        flag = 2;
    } else if (byte == 'R' && obj->waitResp.bufLen >= 4 && memcmp(obj->waitResp.buf + obj->waitResp.bufLen - 4, "ERRO", 
        4) == 0) {
        flag = 3;
    } else if (obj->endSign != '\0' && byte == obj->endSign) {
        flag = 4;
    }

    if (obj->waitResp.setLineNum == 0) {
        // �ж�OK��ERROR
        if (flag == 2 || flag == 3 || flag == 4) {
            obj->waitResp.recvLineCounts++;
            obj->waitResp.buf[obj->waitResp.bufLen++] = '\0';
            
            obj->waitResp.result = TZAT_RESP_RESULT_OK;
            obj->waitResp.isWaitEnd = true;
            return;
        }
    } else {
        // �ж������Ƿ�
        if (flag == 1) {
            obj->waitResp.recvLineCounts++;
            obj->waitResp.buf[obj->waitResp.bufLen++] = '\0';

            if (obj->waitResp.recvLineCounts >= obj->waitResp.setLineNum) {
                obj->waitResp.result = TZAT_RESP_RESULT_OK;
                obj->waitResp.isWaitEnd = true;
            } else if (obj->waitResp.bufLen >= obj->waitResp.bufSize) {
                obj->waitResp.result = TZAT_RESP_RESULT_LACK_OF_MEMORY;
                obj->waitResp.isWaitEnd = true;
            }
            return;
        }
    }

    // ��ͨ����
    obj->waitResp.buf[obj->waitResp.bufLen++] = (char)byte;
    // ���ǵ����������Եö���һ���ֽڿռ�
    if (obj->waitResp.bufLen >= obj->waitResp.bufSize - 1) {
        obj->waitResp.result = TZAT_RESP_RESULT_LACK_OF_MEMORY;
        obj->waitResp.isWaitEnd = true;
    }
}

static void dealUrcList(tObjItem* obj, uint8_t byte) {
    TZListNode* node = TZListGetHeader(obj->urcList);
    for (;;) {
        if (node == NULL) {
            break;
        }
        
        dealUrcItem(byte, (tUrcItem*)node->Data);
        node = node->Next;
    }
}

static void dealUrcItem(uint8_t byte, tUrcItem* item) {
    // �Ƚ�ǰ׺
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

    // ��������,ͬʱ�ȽϺ�׺
    item->buffer->buf[item->buffer->len++] = byte;
    if (byte == item->suffix[item->compareSuffixNum]) {
        item->compareSuffixNum++;
        if (item->compareSuffixNum >= item->suffixLen) {
            // ���ճɹ�
            item->callback(item->buffer->buf, item->buffer->len - item->suffixLen);
            item->isWaitPrefix = true;
            return;
        }
    } else {
        item->compareSuffixNum = 0;
    }

    if (item->buffer->len >= item->bufferSize) {
        // �ﵽ��������δ���յ�β׺
        item->isWaitPrefix = true;
    }
}

static void dealWaitData(tObjItem* obj, uint8_t byte) {
    // ��ͨ����
    obj->waitData.buf[obj->waitData.bufLen++] = byte;
    if (obj->waitData.bufLen >= obj->waitData.bufSize) {
        obj->waitData.result = TZAT_RESP_RESULT_OK;
        obj->waitData.isWaitEnd = true;

        obj->waitData.callback(obj->waitData.result, obj->waitData.buf, obj->waitData.bufLen);
        TZFree(obj->waitData.buf);
        obj->waitData.buf = NULL;
    }
}

static int checkTimeout(void) {
    static struct pt pt = {0};
    static TZListNode* node = NULL;
    static uint64_t now = 0;

    PT_BEGIN(&pt);

    now = TZTimeGet();
    node = TZListGetHeader(objList);
    for (;;) {
        if (node == NULL) {
            break;
        }

        checkObjTimeout((tObjItem*)node->Data, now);
        node = node->Next;
    }

    PT_END(&pt);
}

static void checkObjTimeout(tObjItem* obj, uint64_t now) {
    if (obj->waitResp.isWaitEnd == false) {
        if (now - obj->waitResp.timeBegin > obj->waitResp.timeout) {
            obj->waitResp.result = TZAT_RESP_RESULT_TIMEOUT;
            obj->waitResp.isWaitEnd = true;
        }
    }
    if (obj->waitData.isWaitEnd == false) {
        if (now - obj->waitData.timeBegin > obj->waitData.timeout) {
            obj->waitData.result = TZAT_RESP_RESULT_TIMEOUT;
            obj->waitData.isWaitEnd = true;

            obj->waitData.callback(obj->waitData.result, NULL, 0);
            TZFree(obj->waitData.buf);
            obj->waitData.buf = NULL;
        }
    }
}

static TZListNode* createNode(intptr_t list, int itemSize) {
    TZListNode* node = TZListCreateNode(list);
    if (node == NULL) {
        return NULL;
    }
    node->Data = TZMalloc(mid, itemSize);
    if (node->Data == NULL) {
        TZFree(node);
        return NULL;
    }
    return node;
}

// TZATReceive ��������.�û�ģ����յ����ݺ�����ñ�����
void TZATReceive(intptr_t handle, uint8_t* data, int size) {
    if (handle == 0) {
        return;
    }
    tObjItem* obj = (tObjItem*)handle;
    TZFifoWriteBatch(obj->fifo, data, size);
}

// TZATCreateResp ������Ӧ�ṹ��
// bufSize����Ӧ��������ֽ���
// setLineNum�ǽ��յ���Ӧ����.�������Ϊ0,����յ�OK����ERROR�ͻ᷵��
// timeout�ǽ��ճ�ʱʱ��.��λ:ms
// ����ʧ�ܷ���0,�����ɹ�������Ӧ�ṹ���.ע��ʹ����ϱ����ͷž��
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
    // �����'\0'
    if (bufSize < 2) {
        bufSize = 2;
    }
    resp->bufSize = bufSize;
    resp->setLineNum = setLineNum;
    resp->timeout = (uint64_t)timeout * 1000;
    resp->isWaitEnd = true;
    return (intptr_t)resp;
}

// TZATDeleteResp ɾ����Ӧ�ṹ��.���ͷŽṹ����ռ���ڴ�ռ�
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

// TZATIsBusy �Ƿ�æµ.æµʱ��Ӧ�÷���������߽���ָ����������
bool TZATIsBusy(intptr_t handle) {
    if (handle == 0) {
        return true;
    }
    tObjItem* obj = (tObjItem*)handle;
    return (obj->waitResp.isWaitEnd == false || obj->waitData.isWaitEnd == false);
}

// TZATExecCmd �������������Ӧ.�������Ҫ��Ӧ,��respHandle��������Ϊ0
// ע�Ȿ������ͨ��PT_WAIT_THREAD����
int TZATExecCmd(intptr_t handle, intptr_t respHandle, char* cmd, ...) {
    static struct pt pt = {0};
    char buf[TZAT_CMD_LEN_MAX] = {0};
    va_list args;
    static tObjItem* obj = NULL;
   
    PT_BEGIN(&pt);

    if (handle == 0) {
        PT_EXIT(&pt);
    }
    obj = (tObjItem*)handle;

    if (TZATIsBusy(handle)) {
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

    obj->send((uint8_t*)buf, (int)strlen(buf));

    if (respHandle != 0) {
        obj->waitResp = *(tResp*)respHandle;
        memset(obj->waitResp.buf, 0, (size_t)obj->waitResp.bufSize);
        obj->waitResp.timeBegin = TZTimeGet();
        obj->waitResp.isWaitEnd = false;
        PT_WAIT_UNTIL(&pt, obj->waitResp.isWaitEnd);
        *(tResp*)respHandle = obj->waitResp;
    }

    PT_END(&pt);
}

// TZATRespGetResult ��ȡ��Ӧ���
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

// TZATRespGetLineTotal ��ȡ��Ӧ�������.������ص���0��ʾ����ʧ��
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

// TZATRespGetLine ��ȡָ����
// lineNumber���к�.�кŴ�0��ʼ
// ���ָ���в�����,�򷵻ص���NULL.ע������п���
const char* TZATRespGetLine(intptr_t respHandle, int lineNumber) {
    if (respHandle == 0) {
        return NULL;
    }

    tResp* resp = (tResp*)respHandle;
    if (resp->isWaitEnd == false || lineNumber >= resp->recvLineCounts) {
        return NULL;
    }

    int offset = 0;
    for (int i = 0; i < lineNumber; i++) {
        offset += (int)strlen(resp->buf + offset) + 1;
    }
    return resp->buf + offset;
}

// TZATRespGetLineByKeyword ��ȡ�ؼ���������
// ����в�����,�򷵻ص���NULL.ע������п���
const char* TZATRespGetLineByKeyword(intptr_t respHandle, const char* keyword) {
    if (respHandle == 0) {
        return NULL;
    }

    tResp* resp = (tResp*)respHandle;
    if (resp->isWaitEnd == false) {
        return NULL;
    }

    int offset = 0;
    int len = 0;
    for (int i = 0; i < resp->recvLineCounts; i++) {
        len = (int)strlen(resp->buf + offset);
        if (len == 0) {
            continue;
        }
        if (strstr(resp->buf + offset, keyword) != NULL) {
            return resp->buf + offset;
        }

        offset += len + 1;
    }
    return NULL;
}

// TZATRegisterUrc ע��URC�ص�����
// prefix��ǰ׺,suffix�Ǻ�׺
// bufSize��������������ֽ���,���Ĳ�����ǰ׺�ͺ�׺
// callback�ǻص�����
bool TZATRegisterUrc(intptr_t handle, char* prefix, char* suffix, int bufSize, TZDataFunc callback) {
    if (handle == 0) {
        return false;
    }
    tObjItem* obj = (tObjItem*)handle;

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

    TZListNode* node = createNode(obj->urcList, sizeof(tUrcItem));
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
    TZListAppend(obj->urcList, node);
    return true;
}

// TZATSetWaitDataCallback ���ý���ָ���������ݵĻص�����
// size�ǽ��������ֽ���.timeout�ǳ�ʱʱ��,��λ:ms
bool TZATSetWaitDataCallback(intptr_t handle, int size, int timeout, TZTADataFunc callback) {
    if (handle == 0) {
        return false;
    }
    tObjItem* obj = (tObjItem*)handle;

    if (TZATIsBusy(handle)) {
        return false;
    }

    if (size == 0 || timeout == 0 || callback == NULL) {
        LE(TZAT_TAG, "set wait data callback failed!size or timeout is 0 or callback is NULL:%d %d", size, timeout);
        return false;
    }

    if (obj->waitData.buf != NULL) {
        TZFree(obj->waitData.buf);
        obj->waitData.buf = NULL;
    }
    obj->waitData.buf = TZMalloc(mid, size);
    if (obj->waitData.buf == NULL) {
        LE(TZAT_TAG, "set wait data callback failed!malloc buf failed,size:%d", size);
        return false;
    }
    obj->waitData.bufSize = size;
    obj->waitData.bufLen = 0;
    obj->waitData.isWaitEnd = false;
    obj->waitData.timeBegin = TZTimeGet();
    obj->waitData.timeout = (uint64_t)timeout * 1000;
    obj->waitData.callback = callback;
    return true;
}

// TZATSetEndSign ���ý�����.�������Ҫ���������������Ϊ'\0'
void TZATSetEndSign(intptr_t handle, char ch) {
    if (handle == 0) {
        return;
    }
    tObjItem* obj = (tObjItem*)handle;
    obj->endSign = ch;
}

// TZATSendData ��������
void TZATSendData(intptr_t handle, uint8_t* data, int size) {
    if (handle == 0) {
        return;
    }
    tObjItem* obj = (tObjItem*)handle;
    obj->send(data, size);
}
