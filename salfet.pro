TEMPLATE = app
CONFIG += console
CONFIG -= qt

SOURCES += \
    salfet.c

LIBS += -lavformat -lavfilter -lavcodec -lswscale -lavutil