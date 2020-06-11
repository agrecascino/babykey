TEMPLATE = app
CONFIG += console c++11
CONFIG -= app_bundle
CONFIG -= qt

SOURCES += \
    synthesizer.cpp

LIBS += -lportaudio -lpthread -lX11 -lasound -lportmidi

QMAKE_CXXFLAGS +=  -std=c++17 -fmodules


