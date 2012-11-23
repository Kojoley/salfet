TEMPLATE = app
CONFIG += console
CONFIG -= qt

SOURCES += \
    salfet.c

LIBS += -lavformat -lavfilter -lavcodec -lswscale -lavutil

QMAKE_CFLAGS = -std=c99

OTHER_FILES += \
    trash.c
