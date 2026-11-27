AM_CFLAGS = \
	-Wall

AM_CPPFLAGS = \
	@PYTHON_CPPFLAGS@ \
	-include Python.h

AM_CXXFLAGS = \
	-Wall \
	@ENIGMA2_CFLAGS@ \
	@UCHARDET_CFLAGS@ \
	@OPENSSL_CFLAGS@

plugindir = $(libdir)/enigma2/python/Plugins/SystemPlugins/ServiceApp

plugin_LTLIBRARIES = serviceapp.la

serviceapp_la_SOURCES = \
	serviceapp.cpp \
	extplayer.cpp \
	scriptrun.cpp \
	myconsole.cpp \
	wrappers.cpp \
	m3u8.cpp \
	hlsexplorer.cpp \
	gstplayer.cpp \
	exteplayer3.cpp \
	common.cpp \
	cJSON/cJSON.c \
	subtitles/subtitles.cpp \
	subtitles/subrip.cpp

noinst_HEADERS = \
	common.h \
	exteplayer3.h \
	extplayer.h \
	gstplayer.h \
	hlsexplorer.h \
	m3u8.h \
	myconsole.h \
	scriptrun.h \
	serviceapp.h \
	wrappers.h \
	cJSON/cJSON.h \
	subtitles/subrip.h \
	subtitles/subtitles.h

serviceapp_la_LDFLAGS = \
	@UCHARDET_LIBS@ \
	@OPENSSL_LIBS@ \
	-avoid-version \
	-module \
	-shared
