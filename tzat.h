// Copyright 2021-2021 The jdh99 Authors. All rights reserved.
// AT���
// Authors: jdh99 <jdh821@163.com>

#ifndef TZAT_H
#define TZAT_H

#include "tztype.h"

#define TZAT_TAG "tzat"
#define TZAT_MALLOC_SIZE 8192

// ��������ֽ���
#define TZAT_CMD_LEN_MAX 128
// ֡FIFO��С
#define TZAT_FIFO_SIZE 2048

typedef enum {
    // �ɹ�
    TZAT_RESP_RESULT_OK = 0,
    // ��ʱ
    TZAT_RESP_RESULT_TIMEOUT,
    // ȱ���ڴ�
    TZAT_RESP_RESULT_LACK_OF_MEMORY,
    // ��������
    TZAT_RESP_RESULT_PARAM_ERROR,
    // ģ��æµ
    TZAT_RESP_RESULT_BUSY,
    // ��������
    TZAT_RESP_RESULT_OTHER
} TZATRespResult;

// TZTADataFunc ����ָ���������ݻص�����
typedef void (*TZTADataFunc)(TZATRespResult result, uint8_t* bytes, int size);

// TZATLoad ģ������
void TZATLoad(TZDataFunc send, TZIsAllowSendFunc isAllowSend);

// TZATReceive ��������.�û�ģ����յ����ݺ�����ñ�����
void TZATReceive(uint8_t* data, int size);

// TZATCreateResp ������Ӧ�ṹ��
// bufSize����Ӧ��������ֽ���
// setLineNum�ǽ��յ���Ӧ����.�������Ϊ0,����յ�OK����ERROR�ͻ᷵��
// timeout�ǽ��ճ�ʱʱ��.��λ:ms
// ����ʧ�ܷ���0,�����ɹ�������Ӧ�ṹ���.ע��ʹ����ϱ����ͷž��
intptr_t TZATCreateResp(int bufSize, int setLineNum, int timeout);

// TZATDeleteResp ɾ����Ӧ�ṹ��.���ͷŽṹ����ռ���ڴ�ռ�
void TZATDeleteResp(intptr_t respHandle);

// TZATIsBusy �Ƿ�æµ.æµʱ��Ӧ�÷���������߽���ָ����������
bool TZATIsBusy(void);

// TZATExecCmd �������������Ӧ.�������Ҫ��Ӧ,��respHandle��������Ϊ0
// ע�Ȿ������ͨ��PT_WAIT_THREAD����
int TZATExecCmd(intptr_t respHandle, char* cmd, ...);

// TZATRespGetResult ��ȡ��Ӧ���
TZATRespResult TZATRespGetResult(intptr_t respHandle);

// TZATRespGetLineTotal ��ȡ��Ӧ�������.������ص���0��ʾ����ʧ��
int TZATRespGetLineTotal(intptr_t respHandle);

// TZATRespGetLine ��ȡָ����
// lineNumber���к�.�кŴ�0��ʼ
// ���ָ���в�����,�򷵻ص���NULL
const char* TZATRespGetLine(intptr_t respHandle, int lineNumber);

// TZATRegisterUrc ע��URC�ص�����
// prefix��ǰ׺,suffix�Ǻ�׺
// bufSize��������������ֽ���,���Ĳ�����ǰ׺�ͺ�׺
// callback�ǻص�����
bool TZATRegisterUrc(char* prefix, char* suffix, int bufSize, TZDataFunc callback);

// TZATSetWaitDataCallback ���ý���ָ���������ݵĻص�����
// size�ǽ��������ֽ���.timeout�ǳ�ʱʱ��,��λ:ms
bool TZATSetWaitDataCallback(int size, int timeout, TZTADataFunc callback);

#endif
