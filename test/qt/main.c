#include <stdio.h>
#include <windows.h>
#include <time.h>

#include "tzat.h"
#include "lagan.h"
#include "pt.h"
#include "tztime.h"
#include "tzmalloc.h"
#include "async.h"
#include "tztype.h"

#define RAM_INTERNAL 0

static int gMid = -1;

static void print(uint8_t* bytes, int size);
static LaganTime getLaganTime(void);
static uint64_t getTime(void);

static void tzatSend(uint8_t* bytes, int size);
static bool tzatIsAllowSend(void);

static int case1(void);
static int case2(void);
static int receiveTask(void);

static int case3(void);
static void receiveCallback(uint8_t* bytes, int size);
static void receiveDataCallback(TZATRespResult result, uint8_t* bytes, int size);

int main() {
    LaganLoad(print, getLaganTime);

    TZTimeLoad(getTime);
    TZMallocLoad(RAM_INTERNAL, 20, 100 * 1024, malloc(100 * 1024));
    gMid = TZMallocRegister(RAM_INTERNAL, "test", 4096);

    TZATLoad(tzatSend, tzatIsAllowSend);

    AsyncStart(case1, ASYNC_ONLY_ONE_TIME);
    AsyncStart(case2, ASYNC_ONLY_ONE_TIME);
    AsyncStart(receiveTask, 100 * ASYNC_MILLISECOND);
    AsyncStart(case3, ASYNC_ONLY_ONE_TIME);

    while (1) {
        AsyncRun();
    }

    return 0;
}

static void print(uint8_t* bytes, int size) {
    printf("%s\n", bytes);
}

static LaganTime getLaganTime(void) {
    SYSTEMTIME t1;
    GetSystemTime(&t1);

    LaganTime time;
    time.Year = t1.wYear;
    time.Month = t1.wMonth;
    time.Day = t1.wDay;
    time.Hour = t1.wHour;
    time.Minute = t1.wMinute;
    time.Second = t1.wSecond;
    time.Us = t1.wMilliseconds * 1000;
    return time;
}

static uint64_t getTime(void) {
    struct timeval t;
    gettimeofday(&t, NULL);
    return t.tv_sec * 1000000 + t.tv_usec;
}

static void tzatSend(uint8_t* bytes, int size) {
    printf("tzat send:%d %d %s\n", size, (int)strlen((char*)bytes), (char*)bytes);
}

static bool tzatIsAllowSend(void) {
    return true;
}

static int case1(void) {
    static struct pt pt;

    PT_BEGIN(&pt);

    PT_WAIT_UNTIL(&pt, TZATExecCmd(0, "AT+CIPSEND=3,\"%s\",%d\r\n", "192.168.1.2", 1234));

    PT_END(&pt);
}

static int case2(void) {
    static struct pt pt;
    static intptr_t respHandle = 0;

    PT_BEGIN(&pt);

    respHandle = TZATCreateResp(100, 3, 5000);
    PT_WAIT_UNTIL(&pt, TZATExecCmd(respHandle, "AT+UART_DEF=%d,%d,%d,%d,%d\r\n", 115200, 8, 1, 0, 0));
    printf("result:%d\n", TZATRespGetResult(respHandle));

    int total = TZATRespGetLineTotal(respHandle);
    for (int i = 0; i < total; i++) {
        printf("%d:%s\n", i, TZATRespGetLine(respHandle, i));
    }
    TZATDeleteResp(respHandle);

    PT_END(&pt);
}

static int receiveTask(void) {
    static struct pt pt;
    static int num = 0;

    PT_BEGIN(&pt);

    TZATReceive((uint8_t*)"hellojdh\r\n", strlen("hellojdh\r\n"));
    TZATReceive((uint8_t*)"abcdefg\r\n", strlen("abcdefg\r\n"));
    TZATReceive((uint8_t*)"hijklmn\r\n", strlen("hijklmn\r\n"));
    TZATReceive((uint8_t*)"+IPD,5,\"192.168.1.119\",12100:ABCDE", strlen("+IPD,7,\"192.168.1.119\",12100:ABCDE"));

    num++;
    printf("\nsend num:%d\n", num);

    PT_END(&pt);
}

static int case3(void) {
    static struct pt pt;

    PT_BEGIN(&pt);

    TZATRegisterUrc("+IPD,", ":", 100, receiveCallback);

    PT_END(&pt);
}

static void receiveCallback(uint8_t* bytes, int size) {
    printf("receiveCallback:%d\n", size);
    for (int i = 0; i < size; i++) {
        printf("%c", bytes[i]);
    }
    printf("\n");

    if (TZATIsBusy()) {
        printf("--------------------->busy!\n");
    }
    TZATSetWaitDataCallback(5, 100, receiveDataCallback);
}

static void receiveDataCallback(TZATRespResult result, uint8_t* bytes, int size) {
    static int num = 0;
    printf("num:%d result:%d\n", num + 1, result);
    if (result != TZAT_RESP_RESULT_OK) {
        return;
    }
    printf("receiveDataCallback:%d\n", size);
    for (int i = 0; i < size; i++) {
        printf("%c", bytes[i]);
    }
    printf("\n");
    num++;
}
