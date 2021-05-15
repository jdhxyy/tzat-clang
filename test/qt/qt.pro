TEMPLATE = app
CONFIG += console
CONFIG -= app_bundle
CONFIG -= qt

SOURCES += \
        main.c \
    ../../tzat.c \
    ../../lib/async-clang/async.c \
    ../../lib/crc16-clang/crc16.c \
    ../../lib/lagan-clang/lagan.c \
    ../../lib/tzfifo/tzfifo.c \
    ../../lib/tzlist/tzlist.c \
    ../../lib/tzmalloc/bget.c \
    ../../lib/tzmalloc/tzmalloc.c \
    ../../lib/tztime/tztime.c

INCLUDEPATH += ../../ \
    ../../lib/tztime \
    ../../lib/tzmalloc \
    ../../lib/tzlist \
    ../../lib/tzfifo \
    ../../lib/lagan-clang \
    ../../lib/pt \
    ../../lib/async-clang \
    ../../lib/tztype-clang \

HEADERS += \
    ../../tzat.h \
    ../../lib/async-clang/async.h \
    ../../lib/crc16-clang/crc16.h \
    ../../lib/lagan-clang/lagan.h \
    ../../lib/pt/lc-switch.h \
    ../../lib/pt/lc.h \
    ../../lib/pt/pt-sem.h \
    ../../lib/pt/pt.h \
    ../../lib/tzfifo/tzfifo.h \
    ../../lib/tzlist/tzlist.h \
    ../../lib/tzmalloc/bget.h \
    ../../lib/tzmalloc/tzmalloc.h \
    ../../lib/tztime/tztime.h \
    ../../lib/tztype-clang/tztype.h
