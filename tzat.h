// Copyright 2021-2021 The jdh99 Authors. All rights reserved.
// AT组件
// Authors: jdh99 <jdh821@163.com>

#ifndef TZAT_H
#define TZAT_H

#include "tztype.h"

#define TZAT_TAG "tzat"
#define TZAT_MALLOC_SIZE 8192

// 最大命令字节数
#define TZAT_CMD_LEN_MAX 128
// 帧FIFO大小
#define TZAT_FIFO_SIZE 2048

typedef enum {
    // 成功
    TZAT_RESP_RESULT_OK = 0,
    // 超时
    TZAT_RESP_RESULT_TIMEOUT,
    // 缺少内存
    TZAT_RESP_RESULT_LACK_OF_MEMORY,
    // 参数错误
    TZAT_RESP_RESULT_PARAM_ERROR,
    // 模块忙碌
    TZAT_RESP_RESULT_BUSY,
    // 其他错误
    TZAT_RESP_RESULT_OTHER
} TZATRespResult;

// TZTADataFunc 接收指定长度数据回调函数
typedef void (*TZTADataFunc)(TZATRespResult result, uint8_t* bytes, int size);

// TZATSetMid 设置内存id
// 如果不调用本函数.则模块使用默认内存ID
// 必须在调用TZATCreate函数前调用本函数,否则模块使用默认内存ID
void TZATSetMid(int id);

// TZATCreate 创建AT组件
// send是本组件发送函数.isAllowSend是是否允许发送函数
// 创建成功返回句柄.失败返回0
intptr_t TZATCreate(TZDataFunc send, TZIsAllowSendFunc isAllowSend);

// TZATReceive 接收数据.用户模块接收到数据后需调用本函数
void TZATReceive(intptr_t handle, uint8_t* data, int size);

// TZATCreateResp 创建响应结构体
// bufSize是响应数据最大字节数
// setLineNum是接收的响应行数.如果设置为0,则接收到OK或者ERROR就会返回
// timeout是接收超时时间.单位:ms
// 创建失败返回0,创建成功返回响应结构句柄.注意使用完毕必须释放句柄
intptr_t TZATCreateResp(int bufSize, int setLineNum, int timeout);

// TZATDeleteResp 删除响应结构体.会释放结构体所占的内存空间
void TZATDeleteResp(intptr_t respHandle);

// TZATIsBusy 是否忙碌.忙碌时不应该发送命令或者接收指定长度数据
bool TZATIsBusy(intptr_t handle);

// TZATExecCmd 发送命令并接收响应.如果不需要响应,则respHandle可以设置为0
// 注意本函数需通过PT_WAIT_THREAD调用.调用本函数前必须调用TZATIsBusy判断忙碌
int TZATExecCmd(intptr_t handle, intptr_t respHandle, char* cmd, ...);

// TZATRespGetResult 读取响应结果
TZATRespResult TZATRespGetResult(intptr_t respHandle);

// TZATRespGetLineTotal 读取响应结果行数.如果返回的是0表示接收失败
int TZATRespGetLineTotal(intptr_t respHandle);

// TZATRespGetLine 读取指定行
// lineNumber是行号.行号从0开始
// 如果指定行不存在,则返回的是NULL.注意可能有空行
const char* TZATRespGetLine(intptr_t respHandle, int lineNumber);

// TZATRespGetLineByKeyword 读取关键字所在行
// 如果行不存在,则返回的是NULL.注意可能有空行
const char* TZATRespGetLineByKeyword(intptr_t respHandle, const char* keyword);

// TZATRegisterUrc 注册URC回调函数
// prefix是前缀,suffix是后缀
// bufSize是正文数据最大字节数,正文不包括前缀和后缀
// callback是回调函数
bool TZATRegisterUrc(intptr_t handle, char* prefix, char* suffix, int bufSize, TZDataFunc callback);

// TZATSetWaitDataCallback 设置接收指定长度数据的回调函数
// size是接收数据字节数.timeout是超时时间,单位:ms
bool TZATSetWaitDataCallback(intptr_t handle, int size, int timeout, TZTADataFunc callback);

// TZATSetEndSign 设置结束符.如果不需要额外设置则可设置为'\0'
// 设置了结束符,则不会以默认的OK或者ERROR来判断结尾
void TZATSetEndSign(intptr_t handle, char ch);

// TZATSendData 发送数据
void TZATSendData(intptr_t handle, uint8_t* data, int size);

#endif
