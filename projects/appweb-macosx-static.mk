#
#   appweb-macosx-static.mk -- Makefile to build Embedthis Appweb Enterprise Edition for macosx
#

NAME                  := appweb
VERSION               := 8.1.0
PROFILE               ?= static
ARCH                  ?= $(shell uname -m | sed 's/i.86/x86/;s/x86_64/x64/;s/arm.*/arm/;s/mips.*/mips/')
CC_ARCH               ?= $(shell echo $(ARCH) | sed 's/x86/i686/;s/x64/x86_64/')
OS                    ?= macosx
CC                    ?= clang
AR                    ?= ar
CONFIG                ?= $(OS)-$(ARCH)-$(PROFILE)
BUILD                 ?= build/$(CONFIG)
LBIN                  ?= $(BUILD)/bin
PATH                  := $(LBIN):$(PATH)

ME_COM_CGI            ?= 0
ME_COM_COMPILER       ?= 1
ME_COM_DIR            ?= 0
ME_COM_EJS            ?= 0
ME_COM_ESP            ?= 1
ME_COM_FAST           ?= 0
ME_COM_HTTP           ?= 1
ME_COM_LIB            ?= 1
ME_COM_MATRIXSSL      ?= 0
ME_COM_MBEDTLS        ?= 1
ME_COM_MDB            ?= 1
ME_COM_MPR            ?= 1
ME_COM_NANOSSL        ?= 0
ME_COM_OPENSSL        ?= 0
ME_COM_OSDEP          ?= 1
ME_COM_PCRE           ?= 1
ME_COM_PHP            ?= 0
ME_COM_SQLITE         ?= 0
ME_COM_SSL            ?= 1
ME_COM_VXWORKS        ?= 0
ME_COM_WATCHDOG       ?= 1

ME_COM_OPENSSL_PATH   ?= "/path/to/openssl"

ifeq ($(ME_COM_LIB),1)
    ME_COM_COMPILER := 1
endif
ifeq ($(ME_COM_MBEDTLS),1)
    ME_COM_SSL := 1
endif
ifeq ($(ME_COM_OPENSSL),1)
    ME_COM_SSL := 1
endif
ifeq ($(ME_COM_ESP),1)
    ME_COM_MDB := 1
endif

CFLAGS                += -fstack-protector --param=ssp-buffer-size=4 -Wformat -Wformat-security -w
DFLAGS                +=  $(patsubst %,-D%,$(filter ME_%,$(MAKEFLAGS))) -DME_COM_CGI=$(ME_COM_CGI) -DME_COM_COMPILER=$(ME_COM_COMPILER) -DME_COM_DIR=$(ME_COM_DIR) -DME_COM_EJS=$(ME_COM_EJS) -DME_COM_ESP=$(ME_COM_ESP) -DME_COM_FAST=$(ME_COM_FAST) -DME_COM_HTTP=$(ME_COM_HTTP) -DME_COM_LIB=$(ME_COM_LIB) -DME_COM_MATRIXSSL=$(ME_COM_MATRIXSSL) -DME_COM_MBEDTLS=$(ME_COM_MBEDTLS) -DME_COM_MDB=$(ME_COM_MDB) -DME_COM_MPR=$(ME_COM_MPR) -DME_COM_NANOSSL=$(ME_COM_NANOSSL) -DME_COM_OPENSSL=$(ME_COM_OPENSSL) -DME_COM_OSDEP=$(ME_COM_OSDEP) -DME_COM_PCRE=$(ME_COM_PCRE) -DME_COM_PHP=$(ME_COM_PHP) -DME_COM_SQLITE=$(ME_COM_SQLITE) -DME_COM_SSL=$(ME_COM_SSL) -DME_COM_VXWORKS=$(ME_COM_VXWORKS) -DME_COM_WATCHDOG=$(ME_COM_WATCHDOG) 
IFLAGS                += "-I$(BUILD)/inc"
LDFLAGS               += '-Wl,-rpath,@executable_path/' '-Wl,-rpath,@loader_path/'
LIBPATHS              += -L$(BUILD)/bin
LIBS                  += -ldl -lpthread -lm

DEBUG                 ?= debug
CFLAGS-debug          ?= -g
DFLAGS-debug          ?= -DME_DEBUG
LDFLAGS-debug         ?= -g
DFLAGS-release        ?= 
CFLAGS-release        ?= -O2
LDFLAGS-release       ?= 
CFLAGS                += $(CFLAGS-$(DEBUG))
DFLAGS                += $(DFLAGS-$(DEBUG))
LDFLAGS               += $(LDFLAGS-$(DEBUG))

ME_ROOT_PREFIX        ?= 
ME_BASE_PREFIX        ?= $(ME_ROOT_PREFIX)/usr/local
ME_DATA_PREFIX        ?= $(ME_ROOT_PREFIX)/
ME_STATE_PREFIX       ?= $(ME_ROOT_PREFIX)/var
ME_APP_PREFIX         ?= $(ME_BASE_PREFIX)/lib/$(NAME)
ME_VAPP_PREFIX        ?= $(ME_APP_PREFIX)/$(VERSION)
ME_BIN_PREFIX         ?= $(ME_ROOT_PREFIX)/usr/local/bin
ME_INC_PREFIX         ?= $(ME_ROOT_PREFIX)/usr/local/include
ME_LIB_PREFIX         ?= $(ME_ROOT_PREFIX)/usr/local/lib
ME_MAN_PREFIX         ?= $(ME_ROOT_PREFIX)/usr/local/share/man
ME_SBIN_PREFIX        ?= $(ME_ROOT_PREFIX)/usr/local/sbin
ME_ETC_PREFIX         ?= $(ME_ROOT_PREFIX)/etc/$(NAME)
ME_WEB_PREFIX         ?= $(ME_ROOT_PREFIX)/var/www/$(NAME)
ME_LOG_PREFIX         ?= $(ME_ROOT_PREFIX)/var/log/$(NAME)
ME_SPOOL_PREFIX       ?= $(ME_ROOT_PREFIX)/var/spool/$(NAME)
ME_CACHE_PREFIX       ?= $(ME_ROOT_PREFIX)/var/spool/$(NAME)/cache
ME_SRC_PREFIX         ?= $(ME_ROOT_PREFIX)$(NAME)-$(VERSION)

WEB_USER              ?= $(shell egrep 'www-data|_www|nobody' /etc/passwd | sed 's^:.*^^' |  tail -1)
WEB_GROUP             ?= $(shell egrep 'www-data|_www|nobody|nogroup' /etc/group | sed 's^:.*^^' |  tail -1)

TARGETS               += $(BUILD)/bin/appweb
TARGETS               += $(BUILD)/bin/authpass
ifeq ($(ME_COM_ESP),1)
    TARGETS           += $(BUILD)/bin/appweb-esp
endif
ifeq ($(ME_COM_ESP),1)
    TARGETS           += $(BUILD)/.extras-modified
endif
ifeq ($(ME_COM_HTTP),1)
    TARGETS           += $(BUILD)/bin/http
endif
TARGETS               += $(BUILD)/.install-certs-modified
ifeq ($(ME_COM_SQLITE),1)
    TARGETS           += $(BUILD)/bin/libsql.a
endif
TARGETS               += $(BUILD)/bin/makerom
TARGETS               += $(BUILD)/bin/server
TARGETS               += src/server/cache
TARGETS               += $(BUILD)/bin/appman

unexport CDPATH

ifndef SHOW
.SILENT:
endif

all build compile: prep $(TARGETS)

.PHONY: prep

prep:
	@echo "      [Info] Use "make SHOW=1" to trace executed commands."
	@if [ "$(CONFIG)" = "" ] ; then echo WARNING: CONFIG not set ; exit 255 ; fi
	@if [ "$(ME_APP_PREFIX)" = "" ] ; then echo WARNING: ME_APP_PREFIX not set ; exit 255 ; fi
	@[ ! -x $(BUILD)/bin ] && mkdir -p $(BUILD)/bin; true
	@[ ! -x $(BUILD)/inc ] && mkdir -p $(BUILD)/inc; true
	@[ ! -x $(BUILD)/obj ] && mkdir -p $(BUILD)/obj; true
	@[ ! -f $(BUILD)/inc/me.h ] && cp projects/appweb-macosx-static-me.h $(BUILD)/inc/me.h ; true
	@if ! diff $(BUILD)/inc/me.h projects/appweb-macosx-static-me.h >/dev/null ; then\
		cp projects/appweb-macosx-static-me.h $(BUILD)/inc/me.h  ; \
	fi; true
	@if [ -f "$(BUILD)/.makeflags" ] ; then \
		if [ "$(MAKEFLAGS)" != "`cat $(BUILD)/.makeflags`" ] ; then \
			echo "   [Warning] Make flags have changed since the last build" ; \
			echo "   [Warning] Previous build command: "`cat $(BUILD)/.makeflags`"" ; \
		fi ; \
	fi
	@echo "$(MAKEFLAGS)" >$(BUILD)/.makeflags

clean:
	rm -f "$(BUILD)/obj/appweb.o"
	rm -f "$(BUILD)/obj/authpass.o"
	rm -f "$(BUILD)/obj/cgiHandler.o"
	rm -f "$(BUILD)/obj/cgiProgram.o"
	rm -f "$(BUILD)/obj/config.o"
	rm -f "$(BUILD)/obj/convenience.o"
	rm -f "$(BUILD)/obj/esp.o"
	rm -f "$(BUILD)/obj/espHandler.o"
	rm -f "$(BUILD)/obj/espLib.o"
	rm -f "$(BUILD)/obj/fastHandler.o"
	rm -f "$(BUILD)/obj/fastProgram.o"
	rm -f "$(BUILD)/obj/http.o"
	rm -f "$(BUILD)/obj/httpLib.o"
	rm -f "$(BUILD)/obj/makerom.o"
	rm -f "$(BUILD)/obj/mbedtls.o"
	rm -f "$(BUILD)/obj/mpr-mbedtls.o"
	rm -f "$(BUILD)/obj/mpr-openssl.o"
	rm -f "$(BUILD)/obj/mpr-version.o"
	rm -f "$(BUILD)/obj/mprLib.o"
	rm -f "$(BUILD)/obj/pcre.o"
	rm -f "$(BUILD)/obj/rom.o"
	rm -f "$(BUILD)/obj/server.o"
	rm -f "$(BUILD)/obj/sqlite.o"
	rm -f "$(BUILD)/obj/sqlite3.o"
	rm -f "$(BUILD)/obj/watchdog.o"
	rm -f "$(BUILD)/bin/appweb"
	rm -f "$(BUILD)/bin/authpass"
	rm -f "$(BUILD)/bin/appweb-esp"
	rm -f "$(BUILD)/.extras-modified"
	rm -f "$(BUILD)/bin/http"
	rm -f "$(BUILD)/.install-certs-modified"
	rm -f "$(BUILD)/bin/libappweb.a"
	rm -f "$(BUILD)/bin/libesp.a"
	rm -f "$(BUILD)/bin/libhttp.a"
	rm -f "$(BUILD)/bin/libmbedtls.a"
	rm -f "$(BUILD)/bin/libmpr.a"
	rm -f "$(BUILD)/bin/libmpr-mbedtls.a"
	rm -f "$(BUILD)/bin/libmpr-version.a"
	rm -f "$(BUILD)/bin/libpcre.a"
	rm -f "$(BUILD)/bin/libsql.a"
	rm -f "$(BUILD)/bin/makerom"
	rm -f "$(BUILD)/bin/server"
	rm -f "$(BUILD)/bin/appman"

clobber: clean
	rm -fr ./$(BUILD)

#
#   me.h
#

$(BUILD)/inc/me.h: $(DEPS_1)

#
#   osdep.h
#
DEPS_2 += src/osdep/osdep.h
DEPS_2 += $(BUILD)/inc/me.h

$(BUILD)/inc/osdep.h: $(DEPS_2)
	@echo '      [Copy] $(BUILD)/inc/osdep.h'
	mkdir -p "$(BUILD)/inc"
	cp src/osdep/osdep.h $(BUILD)/inc/osdep.h

#
#   mpr.h
#
DEPS_3 += src/mpr/mpr.h
DEPS_3 += $(BUILD)/inc/me.h
DEPS_3 += $(BUILD)/inc/osdep.h

$(BUILD)/inc/mpr.h: $(DEPS_3)
	@echo '      [Copy] $(BUILD)/inc/mpr.h'
	mkdir -p "$(BUILD)/inc"
	cp src/mpr/mpr.h $(BUILD)/inc/mpr.h

#
#   http.h
#
DEPS_4 += src/http/http.h
DEPS_4 += $(BUILD)/inc/mpr.h

$(BUILD)/inc/http.h: $(DEPS_4)
	@echo '      [Copy] $(BUILD)/inc/http.h'
	mkdir -p "$(BUILD)/inc"
	cp src/http/http.h $(BUILD)/inc/http.h

#
#   appweb.h
#
DEPS_5 += src/appweb.h
DEPS_5 += $(BUILD)/inc/osdep.h
DEPS_5 += $(BUILD)/inc/mpr.h
DEPS_5 += $(BUILD)/inc/http.h

$(BUILD)/inc/appweb.h: $(DEPS_5)
	@echo '      [Copy] $(BUILD)/inc/appweb.h'
	mkdir -p "$(BUILD)/inc"
	cp src/appweb.h $(BUILD)/inc/appweb.h

#
#   config.h
#

$(BUILD)/inc/config.h: $(DEPS_6)

#
#   customize.h
#
DEPS_7 += src/customize.h

$(BUILD)/inc/customize.h: $(DEPS_7)
	@echo '      [Copy] $(BUILD)/inc/customize.h'
	mkdir -p "$(BUILD)/inc"
	cp src/customize.h $(BUILD)/inc/customize.h

#
#   embedtls.h
#
DEPS_8 += src/mbedtls/embedtls.h

$(BUILD)/inc/embedtls.h: $(DEPS_8)
	@echo '      [Copy] $(BUILD)/inc/embedtls.h'
	mkdir -p "$(BUILD)/inc"
	cp src/mbedtls/embedtls.h $(BUILD)/inc/embedtls.h

#
#   esp.h
#
DEPS_9 += src/esp/esp.h
DEPS_9 += $(BUILD)/inc/me.h
DEPS_9 += $(BUILD)/inc/osdep.h
DEPS_9 += $(BUILD)/inc/http.h

$(BUILD)/inc/esp.h: $(DEPS_9)
	@echo '      [Copy] $(BUILD)/inc/esp.h'
	mkdir -p "$(BUILD)/inc"
	cp src/esp/esp.h $(BUILD)/inc/esp.h

#
#   mbedtls.h
#
DEPS_10 += src/mbedtls/mbedtls.h

$(BUILD)/inc/mbedtls.h: $(DEPS_10)
	@echo '      [Copy] $(BUILD)/inc/mbedtls.h'
	mkdir -p "$(BUILD)/inc"
	cp src/mbedtls/mbedtls.h $(BUILD)/inc/mbedtls.h

#
#   mpr-version.h
#
DEPS_11 += src/mpr-version/mpr-version.h
DEPS_11 += $(BUILD)/inc/mpr.h

$(BUILD)/inc/mpr-version.h: $(DEPS_11)
	@echo '      [Copy] $(BUILD)/inc/mpr-version.h'
	mkdir -p "$(BUILD)/inc"
	cp src/mpr-version/mpr-version.h $(BUILD)/inc/mpr-version.h

#
#   pcre.h
#
DEPS_12 += src/pcre/pcre.h

$(BUILD)/inc/pcre.h: $(DEPS_12)
	@echo '      [Copy] $(BUILD)/inc/pcre.h'
	mkdir -p "$(BUILD)/inc"
	cp src/pcre/pcre.h $(BUILD)/inc/pcre.h

#
#   sqlite3.h
#
DEPS_13 += src/sqlite/sqlite3.h

$(BUILD)/inc/sqlite3.h: $(DEPS_13)
	@echo '      [Copy] $(BUILD)/inc/sqlite3.h'
	mkdir -p "$(BUILD)/inc"
	cp src/sqlite/sqlite3.h $(BUILD)/inc/sqlite3.h

#
#   sqlite3rtree.h
#

$(BUILD)/inc/sqlite3rtree.h: $(DEPS_14)

#
#   windows.h
#

$(BUILD)/inc/windows.h: $(DEPS_15)

#
#   appweb.o
#
DEPS_16 += $(BUILD)/inc/appweb.h

$(BUILD)/obj/appweb.o: \
    src/server/appweb.c $(DEPS_16)
	@echo '   [Compile] $(BUILD)/obj/appweb.o'
	$(CC) -c -o $(BUILD)/obj/appweb.o -arch $(CC_ARCH) $(CFLAGS) $(DFLAGS) -D_FILE_OFFSET_BITS=64 -DMBEDTLS_USER_CONFIG_FILE=\"embedtls.h\" -DME_COM_OPENSSL_PATH=$(ME_COM_OPENSSL_PATH) $(IFLAGS) "-I$(ME_COM_OPENSSL_PATH)/include" src/server/appweb.c

#
#   authpass.o
#
DEPS_17 += $(BUILD)/inc/appweb.h

$(BUILD)/obj/authpass.o: \
    src/utils/authpass.c $(DEPS_17)
	@echo '   [Compile] $(BUILD)/obj/authpass.o'
	$(CC) -c -o $(BUILD)/obj/authpass.o -arch $(CC_ARCH) $(CFLAGS) $(DFLAGS) -D_FILE_OFFSET_BITS=64 -DMBEDTLS_USER_CONFIG_FILE=\"embedtls.h\" -DME_COM_OPENSSL_PATH=$(ME_COM_OPENSSL_PATH) $(IFLAGS) "-I$(ME_COM_OPENSSL_PATH)/include" src/utils/authpass.c

#
#   appweb.h
#

src/appweb.h: $(DEPS_18)

#
#   cgiHandler.o
#
DEPS_19 += src/appweb.h

$(BUILD)/obj/cgiHandler.o: \
    src/modules/cgiHandler.c $(DEPS_19)
	@echo '   [Compile] $(BUILD)/obj/cgiHandler.o'
	$(CC) -c -o $(BUILD)/obj/cgiHandler.o -arch $(CC_ARCH) $(CFLAGS) $(DFLAGS) -D_FILE_OFFSET_BITS=64 -DMBEDTLS_USER_CONFIG_FILE=\"embedtls.h\" -DME_COM_OPENSSL_PATH=$(ME_COM_OPENSSL_PATH) $(IFLAGS) "-I$(ME_COM_OPENSSL_PATH)/include" src/modules/cgiHandler.c

#
#   cgiProgram.o
#

$(BUILD)/obj/cgiProgram.o: \
    src/utils/cgiProgram.c $(DEPS_20)
	@echo '   [Compile] $(BUILD)/obj/cgiProgram.o'
	$(CC) -c -o $(BUILD)/obj/cgiProgram.o -arch $(CC_ARCH) $(CFLAGS) $(DFLAGS) $(IFLAGS) src/utils/cgiProgram.c

#
#   config.o
#
DEPS_21 += src/appweb.h
DEPS_21 += $(BUILD)/inc/pcre.h

$(BUILD)/obj/config.o: \
    src/config.c $(DEPS_21)
	@echo '   [Compile] $(BUILD)/obj/config.o'
	$(CC) -c -o $(BUILD)/obj/config.o -arch $(CC_ARCH) $(CFLAGS) $(DFLAGS) -D_FILE_OFFSET_BITS=64 -DMBEDTLS_USER_CONFIG_FILE=\"embedtls.h\" -DME_COM_OPENSSL_PATH=$(ME_COM_OPENSSL_PATH) $(IFLAGS) "-I$(ME_COM_OPENSSL_PATH)/include" src/config.c

#
#   convenience.o
#
DEPS_22 += src/appweb.h

$(BUILD)/obj/convenience.o: \
    src/convenience.c $(DEPS_22)
	@echo '   [Compile] $(BUILD)/obj/convenience.o'
	$(CC) -c -o $(BUILD)/obj/convenience.o -arch $(CC_ARCH) $(CFLAGS) $(DFLAGS) -D_FILE_OFFSET_BITS=64 -DMBEDTLS_USER_CONFIG_FILE=\"embedtls.h\" -DME_COM_OPENSSL_PATH=$(ME_COM_OPENSSL_PATH) $(IFLAGS) "-I$(ME_COM_OPENSSL_PATH)/include" src/convenience.c

#
#   esp.h
#

src/esp/esp.h: $(DEPS_23)

#
#   esp.o
#
DEPS_24 += src/esp/esp.h
DEPS_24 += $(BUILD)/inc/mpr-version.h

$(BUILD)/obj/esp.o: \
    src/esp/esp.c $(DEPS_24)
	@echo '   [Compile] $(BUILD)/obj/esp.o'
	$(CC) -c -o $(BUILD)/obj/esp.o -arch $(CC_ARCH) $(CFLAGS) $(DFLAGS) -D_FILE_OFFSET_BITS=64 -DMBEDTLS_USER_CONFIG_FILE=\"embedtls.h\" -DME_COM_OPENSSL_PATH=$(ME_COM_OPENSSL_PATH) $(IFLAGS) "-I$(ME_COM_OPENSSL_PATH)/include" src/esp/esp.c

#
#   espHandler.o
#
DEPS_25 += src/appweb.h
DEPS_25 += $(BUILD)/inc/esp.h

$(BUILD)/obj/espHandler.o: \
    src/modules/espHandler.c $(DEPS_25)
	@echo '   [Compile] $(BUILD)/obj/espHandler.o'
	$(CC) -c -o $(BUILD)/obj/espHandler.o -arch $(CC_ARCH) $(CFLAGS) $(DFLAGS) -D_FILE_OFFSET_BITS=64 -DMBEDTLS_USER_CONFIG_FILE=\"embedtls.h\" -DME_COM_OPENSSL_PATH=$(ME_COM_OPENSSL_PATH) $(IFLAGS) "-I$(ME_COM_OPENSSL_PATH)/include" src/modules/espHandler.c

#
#   espLib.o
#
DEPS_26 += src/esp/esp.h
DEPS_26 += $(BUILD)/inc/pcre.h
DEPS_26 += $(BUILD)/inc/http.h

$(BUILD)/obj/espLib.o: \
    src/esp/espLib.c $(DEPS_26)
	@echo '   [Compile] $(BUILD)/obj/espLib.o'
	$(CC) -c -o $(BUILD)/obj/espLib.o -arch $(CC_ARCH) $(CFLAGS) $(DFLAGS) -D_FILE_OFFSET_BITS=64 -DMBEDTLS_USER_CONFIG_FILE=\"embedtls.h\" -DME_COM_OPENSSL_PATH=$(ME_COM_OPENSSL_PATH) $(IFLAGS) "-I$(ME_COM_OPENSSL_PATH)/include" src/esp/espLib.c

#
#   fastHandler.o
#
DEPS_27 += src/appweb.h

$(BUILD)/obj/fastHandler.o: \
    src/modules/fastHandler.c $(DEPS_27)
	@echo '   [Compile] $(BUILD)/obj/fastHandler.o'
	$(CC) -c -o $(BUILD)/obj/fastHandler.o -arch $(CC_ARCH) $(CFLAGS) $(DFLAGS) -D_FILE_OFFSET_BITS=64 -DMBEDTLS_USER_CONFIG_FILE=\"embedtls.h\" -DME_COM_OPENSSL_PATH=$(ME_COM_OPENSSL_PATH) $(IFLAGS) "-I$(ME_COM_OPENSSL_PATH)/include" src/modules/fastHandler.c

#
#   fcgiapp.h
#

../../../..$(ME_INC_PREFIX)/fcgiapp.h: $(DEPS_28)

#
#   fastProgram.o
#
DEPS_29 += ../../../..$(ME_INC_PREFIX)/fcgiapp.h

$(BUILD)/obj/fastProgram.o: \
    src/utils/fastProgram.c $(DEPS_29)
	@echo '   [Compile] $(BUILD)/obj/fastProgram.o'
	$(CC) -c -o $(BUILD)/obj/fastProgram.o -arch $(CC_ARCH) $(CFLAGS) $(DFLAGS) "-I$(ME_INC_PREFIX)" src/utils/fastProgram.c

#
#   http.h
#

src/http/http.h: $(DEPS_30)

#
#   http.o
#
DEPS_31 += src/http/http.h

$(BUILD)/obj/http.o: \
    src/http/http.c $(DEPS_31)
	@echo '   [Compile] $(BUILD)/obj/http.o'
	$(CC) -c -o $(BUILD)/obj/http.o -arch $(CC_ARCH) $(CFLAGS) $(DFLAGS) -D_FILE_OFFSET_BITS=64 -DMBEDTLS_USER_CONFIG_FILE=\"embedtls.h\" -DME_COM_OPENSSL_PATH=$(ME_COM_OPENSSL_PATH) $(IFLAGS) "-I$(ME_COM_OPENSSL_PATH)/include" src/http/http.c

#
#   httpLib.o
#
DEPS_32 += src/http/http.h
DEPS_32 += $(BUILD)/inc/pcre.h

$(BUILD)/obj/httpLib.o: \
    src/http/httpLib.c $(DEPS_32)
	@echo '   [Compile] $(BUILD)/obj/httpLib.o'
	$(CC) -c -o $(BUILD)/obj/httpLib.o -arch $(CC_ARCH) $(CFLAGS) $(DFLAGS) -D_FILE_OFFSET_BITS=64 -DMBEDTLS_USER_CONFIG_FILE=\"embedtls.h\" -DME_COM_OPENSSL_PATH=$(ME_COM_OPENSSL_PATH) $(IFLAGS) "-I$(ME_COM_OPENSSL_PATH)/include" src/http/httpLib.c

#
#   makerom.o
#
DEPS_33 += $(BUILD)/inc/mpr.h

$(BUILD)/obj/makerom.o: \
    src/makerom/makerom.c $(DEPS_33)
	@echo '   [Compile] $(BUILD)/obj/makerom.o'
	$(CC) -c -o $(BUILD)/obj/makerom.o -arch $(CC_ARCH) $(CFLAGS) $(DFLAGS) -D_FILE_OFFSET_BITS=64 -DMBEDTLS_USER_CONFIG_FILE=\"embedtls.h\" -DME_COM_OPENSSL_PATH=$(ME_COM_OPENSSL_PATH) $(IFLAGS) "-I$(ME_COM_OPENSSL_PATH)/include" src/makerom/makerom.c

#
#   mbedtls.h
#

src/mbedtls/mbedtls.h: $(DEPS_34)

#
#   mbedtls.o
#
DEPS_35 += src/mbedtls/mbedtls.h

$(BUILD)/obj/mbedtls.o: \
    src/mbedtls/mbedtls.c $(DEPS_35)
	@echo '   [Compile] $(BUILD)/obj/mbedtls.o'
	$(CC) -c -o $(BUILD)/obj/mbedtls.o -arch $(CC_ARCH) $(CFLAGS) $(DFLAGS) -DMBEDTLS_USER_CONFIG_FILE=\"embedtls.h\" $(IFLAGS) src/mbedtls/mbedtls.c

#
#   mpr-mbedtls.o
#
DEPS_36 += $(BUILD)/inc/mpr.h

$(BUILD)/obj/mpr-mbedtls.o: \
    src/mpr-mbedtls/mpr-mbedtls.c $(DEPS_36)
	@echo '   [Compile] $(BUILD)/obj/mpr-mbedtls.o'
	$(CC) -c -o $(BUILD)/obj/mpr-mbedtls.o -arch $(CC_ARCH) $(CFLAGS) $(DFLAGS) -D_FILE_OFFSET_BITS=64 -DMBEDTLS_USER_CONFIG_FILE=\"embedtls.h\" $(IFLAGS) src/mpr-mbedtls/mpr-mbedtls.c

#
#   mpr-openssl.o
#
DEPS_37 += $(BUILD)/inc/mpr.h

$(BUILD)/obj/mpr-openssl.o: \
    src/mpr-openssl/mpr-openssl.c $(DEPS_37)
	@echo '   [Compile] $(BUILD)/obj/mpr-openssl.o'
	$(CC) -c -o $(BUILD)/obj/mpr-openssl.o -arch $(CC_ARCH) $(CFLAGS) $(DFLAGS) $(IFLAGS) "-I$(BUILD)/inc" "-I$(ME_COM_OPENSSL_PATH)/include" src/mpr-openssl/mpr-openssl.c

#
#   mpr-version.h
#

src/mpr-version/mpr-version.h: $(DEPS_38)

#
#   mpr-version.o
#
DEPS_39 += src/mpr-version/mpr-version.h
DEPS_39 += $(BUILD)/inc/pcre.h

$(BUILD)/obj/mpr-version.o: \
    src/mpr-version/mpr-version.c $(DEPS_39)
	@echo '   [Compile] $(BUILD)/obj/mpr-version.o'
	$(CC) -c -o $(BUILD)/obj/mpr-version.o -arch $(CC_ARCH) $(CFLAGS) $(DFLAGS) $(IFLAGS) src/mpr-version/mpr-version.c

#
#   mpr.h
#

src/mpr/mpr.h: $(DEPS_40)

#
#   mprLib.o
#
DEPS_41 += src/mpr/mpr.h

$(BUILD)/obj/mprLib.o: \
    src/mpr/mprLib.c $(DEPS_41)
	@echo '   [Compile] $(BUILD)/obj/mprLib.o'
	$(CC) -c -o $(BUILD)/obj/mprLib.o -arch $(CC_ARCH) $(CFLAGS) $(DFLAGS) -D_FILE_OFFSET_BITS=64 -DMBEDTLS_USER_CONFIG_FILE=\"embedtls.h\" -DME_COM_OPENSSL_PATH=$(ME_COM_OPENSSL_PATH) $(IFLAGS) "-I$(ME_COM_OPENSSL_PATH)/include" src/mpr/mprLib.c

#
#   pcre.h
#

src/pcre/pcre.h: $(DEPS_42)

#
#   pcre.o
#
DEPS_43 += $(BUILD)/inc/me.h
DEPS_43 += src/pcre/pcre.h

$(BUILD)/obj/pcre.o: \
    src/pcre/pcre.c $(DEPS_43)
	@echo '   [Compile] $(BUILD)/obj/pcre.o'
	$(CC) -c -o $(BUILD)/obj/pcre.o -arch $(CC_ARCH) $(CFLAGS) $(DFLAGS) $(IFLAGS) src/pcre/pcre.c

#
#   rom.o
#
DEPS_44 += $(BUILD)/inc/mpr.h

$(BUILD)/obj/rom.o: \
    src/rom.c $(DEPS_44)
	@echo '   [Compile] $(BUILD)/obj/rom.o'
	$(CC) -c -o $(BUILD)/obj/rom.o -arch $(CC_ARCH) $(CFLAGS) $(DFLAGS) -D_FILE_OFFSET_BITS=64 -DMBEDTLS_USER_CONFIG_FILE=\"embedtls.h\" -DME_COM_OPENSSL_PATH=$(ME_COM_OPENSSL_PATH) $(IFLAGS) "-I$(ME_COM_OPENSSL_PATH)/include" src/rom.c

#
#   server.o
#
DEPS_45 += src/http/http.h

$(BUILD)/obj/server.o: \
    src/http/server.c $(DEPS_45)
	@echo '   [Compile] $(BUILD)/obj/server.o'
	$(CC) -c -o $(BUILD)/obj/server.o -arch $(CC_ARCH) $(CFLAGS) $(DFLAGS) -D_FILE_OFFSET_BITS=64 -DMBEDTLS_USER_CONFIG_FILE=\"embedtls.h\" -DME_COM_OPENSSL_PATH=$(ME_COM_OPENSSL_PATH) $(IFLAGS) "-I$(ME_COM_OPENSSL_PATH)/include" src/http/server.c

#
#   sqlite3.h
#

src/sqlite/sqlite3.h: $(DEPS_46)

#
#   sqlite.o
#
DEPS_47 += $(BUILD)/inc/me.h
DEPS_47 += src/sqlite/sqlite3.h
DEPS_47 += $(BUILD)/inc/windows.h

$(BUILD)/obj/sqlite.o: \
    src/sqlite/sqlite.c $(DEPS_47)
	@echo '   [Compile] $(BUILD)/obj/sqlite.o'
	$(CC) -c -o $(BUILD)/obj/sqlite.o -arch $(CC_ARCH) $(CFLAGS) $(DFLAGS) $(IFLAGS) src/sqlite/sqlite.c

#
#   sqlite3.o
#
DEPS_48 += $(BUILD)/inc/me.h
DEPS_48 += src/sqlite/sqlite3.h
DEPS_48 += $(BUILD)/inc/config.h
DEPS_48 += $(BUILD)/inc/windows.h
DEPS_48 += $(BUILD)/inc/sqlite3rtree.h

$(BUILD)/obj/sqlite3.o: \
    src/sqlite/sqlite3.c $(DEPS_48)
	@echo '   [Compile] $(BUILD)/obj/sqlite3.o'
	$(CC) -c -o $(BUILD)/obj/sqlite3.o -arch $(CC_ARCH) $(CFLAGS) $(DFLAGS) $(IFLAGS) src/sqlite/sqlite3.c

#
#   watchdog.o
#
DEPS_49 += $(BUILD)/inc/mpr.h

$(BUILD)/obj/watchdog.o: \
    src/watchdog/watchdog.c $(DEPS_49)
	@echo '   [Compile] $(BUILD)/obj/watchdog.o'
	$(CC) -c -o $(BUILD)/obj/watchdog.o -arch $(CC_ARCH) $(CFLAGS) $(DFLAGS) -D_FILE_OFFSET_BITS=64 -DMBEDTLS_USER_CONFIG_FILE=\"embedtls.h\" -DME_COM_OPENSSL_PATH=$(ME_COM_OPENSSL_PATH) $(IFLAGS) "-I$(ME_COM_OPENSSL_PATH)/include" src/watchdog/watchdog.c

ifeq ($(ME_COM_MBEDTLS),1)
#
#   libmbedtls
#
DEPS_50 += $(BUILD)/inc/osdep.h
DEPS_50 += $(BUILD)/inc/embedtls.h
DEPS_50 += $(BUILD)/inc/mbedtls.h
DEPS_50 += $(BUILD)/obj/mbedtls.o

$(BUILD)/bin/libmbedtls.a: $(DEPS_50)
	@echo '      [Link] $(BUILD)/bin/libmbedtls.a'
	$(AR) -cr $(BUILD)/bin/libmbedtls.a "$(BUILD)/obj/mbedtls.o"
endif

ifeq ($(ME_COM_MBEDTLS),1)
#
#   libmpr-mbedtls
#
DEPS_51 += $(BUILD)/bin/libmbedtls.a
DEPS_51 += $(BUILD)/obj/mpr-mbedtls.o

$(BUILD)/bin/libmpr-mbedtls.a: $(DEPS_51)
	@echo '      [Link] $(BUILD)/bin/libmpr-mbedtls.a'
	$(AR) -cr $(BUILD)/bin/libmpr-mbedtls.a "$(BUILD)/obj/mpr-mbedtls.o"
endif

ifeq ($(ME_COM_OPENSSL),1)
#
#   libmpr-openssl
#
DEPS_52 += $(BUILD)/obj/mpr-openssl.o

$(BUILD)/bin/libmpr-openssl.a: $(DEPS_52)
	@echo '      [Link] $(BUILD)/bin/libmpr-openssl.a'
	$(AR) -cr $(BUILD)/bin/libmpr-openssl.a "$(BUILD)/obj/mpr-openssl.o"
endif

#
#   libmpr
#
DEPS_53 += $(BUILD)/inc/osdep.h
ifeq ($(ME_COM_MBEDTLS),1)
    DEPS_53 += $(BUILD)/bin/libmpr-mbedtls.a
endif
ifeq ($(ME_COM_MBEDTLS),1)
    DEPS_53 += $(BUILD)/bin/libmbedtls.a
endif
ifeq ($(ME_COM_OPENSSL),1)
    DEPS_53 += $(BUILD)/bin/libmpr-openssl.a
endif
DEPS_53 += $(BUILD)/inc/mpr.h
DEPS_53 += $(BUILD)/obj/mprLib.o

$(BUILD)/bin/libmpr.a: $(DEPS_53)
	@echo '      [Link] $(BUILD)/bin/libmpr.a'
	$(AR) -cr $(BUILD)/bin/libmpr.a "$(BUILD)/obj/mprLib.o"

ifeq ($(ME_COM_PCRE),1)
#
#   libpcre
#
DEPS_54 += $(BUILD)/inc/pcre.h
DEPS_54 += $(BUILD)/obj/pcre.o

$(BUILD)/bin/libpcre.a: $(DEPS_54)
	@echo '      [Link] $(BUILD)/bin/libpcre.a'
	$(AR) -cr $(BUILD)/bin/libpcre.a "$(BUILD)/obj/pcre.o"
endif

ifeq ($(ME_COM_HTTP),1)
#
#   libhttp
#
DEPS_55 += $(BUILD)/bin/libmpr.a
ifeq ($(ME_COM_PCRE),1)
    DEPS_55 += $(BUILD)/bin/libpcre.a
endif
DEPS_55 += $(BUILD)/inc/http.h
DEPS_55 += $(BUILD)/obj/httpLib.o

$(BUILD)/bin/libhttp.a: $(DEPS_55)
	@echo '      [Link] $(BUILD)/bin/libhttp.a'
	$(AR) -cr $(BUILD)/bin/libhttp.a "$(BUILD)/obj/httpLib.o"
endif

#
#   libmpr-version
#
DEPS_56 += $(BUILD)/inc/mpr-version.h
DEPS_56 += $(BUILD)/obj/mpr-version.o

$(BUILD)/bin/libmpr-version.a: $(DEPS_56)
	@echo '      [Link] $(BUILD)/bin/libmpr-version.a'
	$(AR) -cr $(BUILD)/bin/libmpr-version.a "$(BUILD)/obj/mpr-version.o"

ifeq ($(ME_COM_ESP),1)
#
#   libesp
#
ifeq ($(ME_COM_HTTP),1)
    DEPS_57 += $(BUILD)/bin/libhttp.a
endif
DEPS_57 += $(BUILD)/bin/libmpr-version.a
ifeq ($(ME_COM_SQLITE),1)
    DEPS_57 += $(BUILD)/bin/libsql.a
endif
DEPS_57 += $(BUILD)/inc/esp.h
DEPS_57 += $(BUILD)/obj/espLib.o
ifeq ($(ME_COM_SQLITE),1)
    DEPS_57 += $(BUILD)/bin/libsql.a
endif

$(BUILD)/bin/libesp.a: $(DEPS_57)
	@echo '      [Link] $(BUILD)/bin/libesp.a'
	$(AR) -cr $(BUILD)/bin/libesp.a "$(BUILD)/obj/espLib.o"
endif

#
#   libappweb
#
ifeq ($(ME_COM_ESP),1)
    DEPS_58 += $(BUILD)/bin/libesp.a
endif
ifeq ($(ME_COM_SQLITE),1)
    DEPS_58 += $(BUILD)/bin/libsql.a
endif
ifeq ($(ME_COM_HTTP),1)
    DEPS_58 += $(BUILD)/bin/libhttp.a
endif
DEPS_58 += $(BUILD)/bin/libmpr.a
DEPS_58 += $(BUILD)/inc/appweb.h
DEPS_58 += $(BUILD)/inc/customize.h
DEPS_58 += $(BUILD)/obj/config.o
DEPS_58 += $(BUILD)/obj/convenience.o
DEPS_58 += $(BUILD)/obj/cgiHandler.o
DEPS_58 += $(BUILD)/obj/espHandler.o
DEPS_58 += $(BUILD)/obj/fastHandler.o
DEPS_58 += $(BUILD)/obj/rom.o

$(BUILD)/bin/libappweb.a: $(DEPS_58)
	@echo '      [Link] $(BUILD)/bin/libappweb.a'
	$(AR) -cr $(BUILD)/bin/libappweb.a "$(BUILD)/obj/config.o" "$(BUILD)/obj/convenience.o" "$(BUILD)/obj/cgiHandler.o" "$(BUILD)/obj/espHandler.o" "$(BUILD)/obj/fastHandler.o" "$(BUILD)/obj/rom.o"

#
#   appweb
#
DEPS_59 += $(BUILD)/bin/libappweb.a
DEPS_59 += $(BUILD)/obj/appweb.o

ifeq ($(ME_COM_MBEDTLS),1)
    LIBS_59 += -lmbedtls
endif
ifeq ($(ME_COM_MBEDTLS),1)
    LIBS_59 += -lmpr-mbedtls
endif
ifeq ($(ME_COM_MBEDTLS),1)
    LIBS_59 += -lmbedtls
endif
ifeq ($(ME_COM_OPENSSL),1)
    LIBS_59 += -lmpr-openssl
endif
ifeq ($(ME_COM_OPENSSL),1)
ifeq ($(ME_COM_SSL),1)
    LIBS_59 += -lssl
    LIBPATHS_59 += -L"$(ME_COM_OPENSSL_PATH)"
endif
endif
ifeq ($(ME_COM_OPENSSL),1)
    LIBS_59 += -lcrypto
    LIBPATHS_59 += -L"$(ME_COM_OPENSSL_PATH)"
endif
LIBS_59 += -lmpr
ifeq ($(ME_COM_OPENSSL),1)
    LIBS_59 += -lmpr-openssl
endif
ifeq ($(ME_COM_MBEDTLS),1)
    LIBS_59 += -lmpr-mbedtls
endif
ifeq ($(ME_COM_PCRE),1)
    LIBS_59 += -lpcre
endif
ifeq ($(ME_COM_HTTP),1)
    LIBS_59 += -lhttp
endif
ifeq ($(ME_COM_PCRE),1)
    LIBS_59 += -lpcre
endif
LIBS_59 += -lmpr
LIBS_59 += -lmpr-version
ifeq ($(ME_COM_ESP),1)
    LIBS_59 += -lesp
endif
ifeq ($(ME_COM_SQLITE),1)
    LIBS_59 += -lsql
endif
LIBS_59 += -lmpr-version
ifeq ($(ME_COM_HTTP),1)
    LIBS_59 += -lhttp
endif
LIBS_59 += -lappweb
ifeq ($(ME_COM_ESP),1)
    LIBS_59 += -lesp
endif

$(BUILD)/bin/appweb: $(DEPS_59)
	@echo '      [Link] $(BUILD)/bin/appweb'
	$(CC) -o $(BUILD)/bin/appweb -arch $(CC_ARCH) $(LDFLAGS) $(LIBPATHS)  "$(BUILD)/obj/appweb.o" $(LIBPATHS_59) $(LIBS_59) $(LIBS_59) $(LIBS) -lpam 

#
#   authpass
#
DEPS_60 += $(BUILD)/bin/libappweb.a
DEPS_60 += $(BUILD)/obj/authpass.o

ifeq ($(ME_COM_MBEDTLS),1)
    LIBS_60 += -lmbedtls
endif
ifeq ($(ME_COM_MBEDTLS),1)
    LIBS_60 += -lmpr-mbedtls
endif
ifeq ($(ME_COM_MBEDTLS),1)
    LIBS_60 += -lmbedtls
endif
ifeq ($(ME_COM_OPENSSL),1)
    LIBS_60 += -lmpr-openssl
endif
ifeq ($(ME_COM_OPENSSL),1)
ifeq ($(ME_COM_SSL),1)
    LIBS_60 += -lssl
    LIBPATHS_60 += -L"$(ME_COM_OPENSSL_PATH)"
endif
endif
ifeq ($(ME_COM_OPENSSL),1)
    LIBS_60 += -lcrypto
    LIBPATHS_60 += -L"$(ME_COM_OPENSSL_PATH)"
endif
LIBS_60 += -lmpr
ifeq ($(ME_COM_OPENSSL),1)
    LIBS_60 += -lmpr-openssl
endif
ifeq ($(ME_COM_MBEDTLS),1)
    LIBS_60 += -lmpr-mbedtls
endif
ifeq ($(ME_COM_PCRE),1)
    LIBS_60 += -lpcre
endif
ifeq ($(ME_COM_HTTP),1)
    LIBS_60 += -lhttp
endif
ifeq ($(ME_COM_PCRE),1)
    LIBS_60 += -lpcre
endif
LIBS_60 += -lmpr
LIBS_60 += -lmpr-version
ifeq ($(ME_COM_ESP),1)
    LIBS_60 += -lesp
endif
ifeq ($(ME_COM_SQLITE),1)
    LIBS_60 += -lsql
endif
LIBS_60 += -lmpr-version
ifeq ($(ME_COM_HTTP),1)
    LIBS_60 += -lhttp
endif
LIBS_60 += -lappweb
ifeq ($(ME_COM_ESP),1)
    LIBS_60 += -lesp
endif

$(BUILD)/bin/authpass: $(DEPS_60)
	@echo '      [Link] $(BUILD)/bin/authpass'
	$(CC) -o $(BUILD)/bin/authpass -arch $(CC_ARCH) $(LDFLAGS) $(LIBPATHS)  "$(BUILD)/obj/authpass.o" $(LIBPATHS_60) $(LIBS_60) $(LIBS_60) $(LIBS) -lpam 

ifeq ($(ME_COM_ESP),1)
#
#   espcmd
#
DEPS_61 += $(BUILD)/bin/libesp.a
ifeq ($(ME_COM_SQLITE),1)
    DEPS_61 += $(BUILD)/bin/libsql.a
endif
DEPS_61 += $(BUILD)/obj/esp.o

ifeq ($(ME_COM_MBEDTLS),1)
    LIBS_61 += -lmbedtls
endif
ifeq ($(ME_COM_MBEDTLS),1)
    LIBS_61 += -lmpr-mbedtls
endif
ifeq ($(ME_COM_MBEDTLS),1)
    LIBS_61 += -lmbedtls
endif
ifeq ($(ME_COM_OPENSSL),1)
    LIBS_61 += -lmpr-openssl
endif
ifeq ($(ME_COM_OPENSSL),1)
ifeq ($(ME_COM_SSL),1)
    LIBS_61 += -lssl
    LIBPATHS_61 += -L"$(ME_COM_OPENSSL_PATH)"
endif
endif
ifeq ($(ME_COM_OPENSSL),1)
    LIBS_61 += -lcrypto
    LIBPATHS_61 += -L"$(ME_COM_OPENSSL_PATH)"
endif
LIBS_61 += -lmpr
ifeq ($(ME_COM_OPENSSL),1)
    LIBS_61 += -lmpr-openssl
endif
ifeq ($(ME_COM_MBEDTLS),1)
    LIBS_61 += -lmpr-mbedtls
endif
ifeq ($(ME_COM_PCRE),1)
    LIBS_61 += -lpcre
endif
ifeq ($(ME_COM_HTTP),1)
    LIBS_61 += -lhttp
endif
ifeq ($(ME_COM_PCRE),1)
    LIBS_61 += -lpcre
endif
LIBS_61 += -lmpr
LIBS_61 += -lmpr-version
LIBS_61 += -lesp
ifeq ($(ME_COM_SQLITE),1)
    LIBS_61 += -lsql
endif
LIBS_61 += -lmpr-version
ifeq ($(ME_COM_HTTP),1)
    LIBS_61 += -lhttp
endif

$(BUILD)/bin/appweb-esp: $(DEPS_61)
	@echo '      [Link] $(BUILD)/bin/appweb-esp'
	$(CC) -o $(BUILD)/bin/appweb-esp -arch $(CC_ARCH) $(LDFLAGS) $(LIBPATHS)  "$(BUILD)/obj/esp.o" $(LIBPATHS_61) $(LIBS_61) $(LIBS_61) $(LIBS) -lpam 
endif

ifeq ($(ME_COM_ESP),1)
#
#   extras
#
DEPS_62 += src/esp/esp-compile.json
DEPS_62 += src/esp/vcvars.bat

$(BUILD)/.extras-modified: $(DEPS_62)
	@echo '      [Copy] $(BUILD)/bin'
	mkdir -p "$(BUILD)/bin"
	cp src/esp/esp-compile.json $(BUILD)/bin/esp-compile.json
	cp src/esp/vcvars.bat $(BUILD)/bin/vcvars.bat
	touch "$(BUILD)/.extras-modified"
endif

ifeq ($(ME_COM_HTTP),1)
#
#   httpcmd
#
DEPS_63 += $(BUILD)/bin/libhttp.a
DEPS_63 += $(BUILD)/obj/http.o

ifeq ($(ME_COM_MBEDTLS),1)
    LIBS_63 += -lmbedtls
endif
ifeq ($(ME_COM_MBEDTLS),1)
    LIBS_63 += -lmpr-mbedtls
endif
ifeq ($(ME_COM_MBEDTLS),1)
    LIBS_63 += -lmbedtls
endif
ifeq ($(ME_COM_OPENSSL),1)
    LIBS_63 += -lmpr-openssl
endif
ifeq ($(ME_COM_OPENSSL),1)
ifeq ($(ME_COM_SSL),1)
    LIBS_63 += -lssl
    LIBPATHS_63 += -L"$(ME_COM_OPENSSL_PATH)"
endif
endif
ifeq ($(ME_COM_OPENSSL),1)
    LIBS_63 += -lcrypto
    LIBPATHS_63 += -L"$(ME_COM_OPENSSL_PATH)"
endif
LIBS_63 += -lmpr
ifeq ($(ME_COM_OPENSSL),1)
    LIBS_63 += -lmpr-openssl
endif
ifeq ($(ME_COM_MBEDTLS),1)
    LIBS_63 += -lmpr-mbedtls
endif
ifeq ($(ME_COM_PCRE),1)
    LIBS_63 += -lpcre
endif
LIBS_63 += -lhttp
ifeq ($(ME_COM_PCRE),1)
    LIBS_63 += -lpcre
endif
LIBS_63 += -lmpr

$(BUILD)/bin/http: $(DEPS_63)
	@echo '      [Link] $(BUILD)/bin/http'
	$(CC) -o $(BUILD)/bin/http -arch $(CC_ARCH) $(LDFLAGS) $(LIBPATHS)  "$(BUILD)/obj/http.o" $(LIBPATHS_63) $(LIBS_63) $(LIBS_63) $(LIBS) -lpam 
endif

#
#   installPrep
#

installPrep: $(DEPS_64)
	if [ "`id -u`" != 0 ] ; \
	then echo "Must run as root. Rerun with sudo." ; \
	exit 255 ; \
	fi

#
#   install-certs
#
DEPS_65 += src/certs/samples/ca.crt
DEPS_65 += src/certs/samples/ca.key
DEPS_65 += src/certs/samples/ec.crt
DEPS_65 += src/certs/samples/ec.key
DEPS_65 += src/certs/samples/roots.crt
DEPS_65 += src/certs/samples/self.crt
DEPS_65 += src/certs/samples/self.key
DEPS_65 += src/certs/samples/test.crt
DEPS_65 += src/certs/samples/test.key

$(BUILD)/.install-certs-modified: $(DEPS_65)
	@echo '      [Copy] $(BUILD)/bin'
	mkdir -p "$(BUILD)/bin"
	cp src/certs/samples/ca.crt $(BUILD)/bin/ca.crt
	cp src/certs/samples/ca.key $(BUILD)/bin/ca.key
	cp src/certs/samples/ec.crt $(BUILD)/bin/ec.crt
	cp src/certs/samples/ec.key $(BUILD)/bin/ec.key
	cp src/certs/samples/roots.crt $(BUILD)/bin/roots.crt
	cp src/certs/samples/self.crt $(BUILD)/bin/self.crt
	cp src/certs/samples/self.key $(BUILD)/bin/self.key
	cp src/certs/samples/test.crt $(BUILD)/bin/test.crt
	cp src/certs/samples/test.key $(BUILD)/bin/test.key
	touch "$(BUILD)/.install-certs-modified"

ifeq ($(ME_COM_SQLITE),1)
#
#   libsql
#
DEPS_66 += $(BUILD)/inc/sqlite3.h
DEPS_66 += $(BUILD)/obj/sqlite3.o

$(BUILD)/bin/libsql.a: $(DEPS_66)
	@echo '      [Link] $(BUILD)/bin/libsql.a'
	$(AR) -cr $(BUILD)/bin/libsql.a "$(BUILD)/obj/sqlite3.o"
endif

#
#   makerom
#
DEPS_67 += $(BUILD)/bin/libmpr.a
DEPS_67 += $(BUILD)/obj/makerom.o

ifeq ($(ME_COM_MBEDTLS),1)
    LIBS_67 += -lmbedtls
endif
ifeq ($(ME_COM_MBEDTLS),1)
    LIBS_67 += -lmpr-mbedtls
endif
ifeq ($(ME_COM_MBEDTLS),1)
    LIBS_67 += -lmbedtls
endif
ifeq ($(ME_COM_OPENSSL),1)
    LIBS_67 += -lmpr-openssl
endif
ifeq ($(ME_COM_OPENSSL),1)
ifeq ($(ME_COM_SSL),1)
    LIBS_67 += -lssl
    LIBPATHS_67 += -L"$(ME_COM_OPENSSL_PATH)"
endif
endif
ifeq ($(ME_COM_OPENSSL),1)
    LIBS_67 += -lcrypto
    LIBPATHS_67 += -L"$(ME_COM_OPENSSL_PATH)"
endif
LIBS_67 += -lmpr
ifeq ($(ME_COM_OPENSSL),1)
    LIBS_67 += -lmpr-openssl
endif
ifeq ($(ME_COM_MBEDTLS),1)
    LIBS_67 += -lmpr-mbedtls
endif

$(BUILD)/bin/makerom: $(DEPS_67)
	@echo '      [Link] $(BUILD)/bin/makerom'
	$(CC) -o $(BUILD)/bin/makerom -arch $(CC_ARCH) $(LDFLAGS) $(LIBPATHS)  "$(BUILD)/obj/makerom.o" $(LIBPATHS_67) $(LIBS_67) $(LIBS_67) $(LIBS) 

#
#   server
#
ifeq ($(ME_COM_HTTP),1)
    DEPS_68 += $(BUILD)/bin/libhttp.a
endif
DEPS_68 += $(BUILD)/obj/server.o

ifeq ($(ME_COM_MBEDTLS),1)
    LIBS_68 += -lmbedtls
endif
ifeq ($(ME_COM_MBEDTLS),1)
    LIBS_68 += -lmpr-mbedtls
endif
ifeq ($(ME_COM_MBEDTLS),1)
    LIBS_68 += -lmbedtls
endif
ifeq ($(ME_COM_OPENSSL),1)
    LIBS_68 += -lmpr-openssl
endif
ifeq ($(ME_COM_OPENSSL),1)
ifeq ($(ME_COM_SSL),1)
    LIBS_68 += -lssl
    LIBPATHS_68 += -L"$(ME_COM_OPENSSL_PATH)"
endif
endif
ifeq ($(ME_COM_OPENSSL),1)
    LIBS_68 += -lcrypto
    LIBPATHS_68 += -L"$(ME_COM_OPENSSL_PATH)"
endif
LIBS_68 += -lmpr
ifeq ($(ME_COM_OPENSSL),1)
    LIBS_68 += -lmpr-openssl
endif
ifeq ($(ME_COM_MBEDTLS),1)
    LIBS_68 += -lmpr-mbedtls
endif
ifeq ($(ME_COM_PCRE),1)
    LIBS_68 += -lpcre
endif
ifeq ($(ME_COM_HTTP),1)
    LIBS_68 += -lhttp
endif
ifeq ($(ME_COM_PCRE),1)
    LIBS_68 += -lpcre
endif
LIBS_68 += -lmpr

$(BUILD)/bin/server: $(DEPS_68)
	@echo '      [Link] $(BUILD)/bin/server'
	$(CC) -o $(BUILD)/bin/server -arch $(CC_ARCH) $(LDFLAGS) $(LIBPATHS)  "$(BUILD)/obj/server.o" $(LIBPATHS_68) $(LIBS_68) $(LIBS_68) $(LIBS) -lpam 

#
#   server-cache
#

src/server/cache: $(DEPS_69)
	( \
	cd src/server; \
	mkdir -p "cache" ; \
	)

ifeq ($(ME_COM_WATCHDOG),1)
#
#   watchdog
#
DEPS_70 += $(BUILD)/bin/libmpr.a
DEPS_70 += $(BUILD)/obj/watchdog.o

ifeq ($(ME_COM_MBEDTLS),1)
    LIBS_70 += -lmbedtls
endif
ifeq ($(ME_COM_MBEDTLS),1)
    LIBS_70 += -lmpr-mbedtls
endif
ifeq ($(ME_COM_MBEDTLS),1)
    LIBS_70 += -lmbedtls
endif
ifeq ($(ME_COM_OPENSSL),1)
    LIBS_70 += -lmpr-openssl
endif
ifeq ($(ME_COM_OPENSSL),1)
ifeq ($(ME_COM_SSL),1)
    LIBS_70 += -lssl
    LIBPATHS_70 += -L"$(ME_COM_OPENSSL_PATH)"
endif
endif
ifeq ($(ME_COM_OPENSSL),1)
    LIBS_70 += -lcrypto
    LIBPATHS_70 += -L"$(ME_COM_OPENSSL_PATH)"
endif
LIBS_70 += -lmpr
ifeq ($(ME_COM_OPENSSL),1)
    LIBS_70 += -lmpr-openssl
endif
ifeq ($(ME_COM_MBEDTLS),1)
    LIBS_70 += -lmpr-mbedtls
endif

$(BUILD)/bin/appman: $(DEPS_70)
	@echo '      [Link] $(BUILD)/bin/appman'
	$(CC) -o $(BUILD)/bin/appman -arch $(CC_ARCH) $(LDFLAGS) $(LIBPATHS)  "$(BUILD)/obj/watchdog.o" $(LIBPATHS_70) $(LIBS_70) $(LIBS_70) $(LIBS) 
endif

#
#   stop
#
DEPS_71 += compile

stop: $(DEPS_71)
	@./$(BUILD)/bin/appman stop disable uninstall >/dev/null 2>&1 ; true

#
#   installBinary
#

installBinary: $(DEPS_72)
	mkdir -p "$(ME_APP_PREFIX)" ; \
	rm -f "$(ME_APP_PREFIX)/latest" ; \
	ln -s "$(VERSION)" "$(ME_APP_PREFIX)/latest" ; \
	mkdir -p "$(ME_MAN_PREFIX)/man1" ; \
	chmod 755 "$(ME_MAN_PREFIX)/man1" ; \
	mkdir -p "$(ME_LOG_PREFIX)" ; \
	chmod 755 "$(ME_LOG_PREFIX)" ; \
	[ `id -u` = 0 ] && chown $(WEB_USER):$(WEB_GROUP) "$(ME_LOG_PREFIX)"; true ; \
	mkdir -p "$(ME_CACHE_PREFIX)" ; \
	chmod 755 "$(ME_CACHE_PREFIX)" ; \
	[ `id -u` = 0 ] && chown $(WEB_USER):$(WEB_GROUP) "$(ME_CACHE_PREFIX)"; true ; \
	mkdir -p "$(ME_VAPP_PREFIX)/bin" ; \
	cp $(BUILD)/bin/appweb $(ME_VAPP_PREFIX)/bin/appweb ; \
	chmod 755 "$(ME_VAPP_PREFIX)/bin/appweb" ; \
	mkdir -p "$(ME_BIN_PREFIX)" ; \
	rm -f "$(ME_BIN_PREFIX)/appweb" ; \
	ln -s "$(ME_VAPP_PREFIX)/bin/appweb" "$(ME_BIN_PREFIX)/appweb" ; \
	if [ "$(ME_COM_SSL)" = 1 ]; then true ; \
	mkdir -p "$(ME_VAPP_PREFIX)/bin" ; \
	cp $(BUILD)/bin/roots.crt $(ME_VAPP_PREFIX)/bin/roots.crt ; \
	fi ; \
	mkdir -p "$(ME_ETC_PREFIX)" ; \
	cp src/server/mime.types $(ME_ETC_PREFIX)/mime.types ; \
	mkdir -p "$(ME_ETC_PREFIX)" ; \
	cp src/server/appweb.conf $(ME_ETC_PREFIX)/appweb.conf ; \
	mkdir -p "$(ME_ETC_PREFIX)" ; \
	cp src/server/esp.json $(ME_ETC_PREFIX)/esp.json ; \
	mkdir -p "$(ME_ETC_PREFIX)" ; \
	cp src/server/sample.conf $(ME_ETC_PREFIX)/sample.conf ; \
	echo 'set LOG_DIR "$(ME_LOG_PREFIX)"\nset CACHE_DIR "$(ME_CACHE_PREFIX)"\nDocuments "$(ME_WEB_PREFIX)"\nListen 80\n<if SSL_MODULE>\nListenSecure 443\n</if>\n' >$(ME_ETC_PREFIX)/install.conf ; \
	mkdir -p "$(ME_WEB_PREFIX)" ; \
	mkdir -p "$(ME_WEB_PREFIX)/bench" ; \
	cp src/server/web/bench/1b.html $(ME_WEB_PREFIX)/bench/1b.html ; \
	cp src/server/web/bench/4k.html $(ME_WEB_PREFIX)/bench/4k.html ; \
	cp src/server/web/bench/64k.html $(ME_WEB_PREFIX)/bench/64k.html ; \
	cp src/server/web/favicon.ico $(ME_WEB_PREFIX)/favicon.ico ; \
	mkdir -p "$(ME_WEB_PREFIX)/icons" ; \
	cp src/server/web/icons/back.gif $(ME_WEB_PREFIX)/icons/back.gif ; \
	cp src/server/web/icons/blank.gif $(ME_WEB_PREFIX)/icons/blank.gif ; \
	cp src/server/web/icons/compressed.gif $(ME_WEB_PREFIX)/icons/compressed.gif ; \
	cp src/server/web/icons/folder.gif $(ME_WEB_PREFIX)/icons/folder.gif ; \
	cp src/server/web/icons/parent.gif $(ME_WEB_PREFIX)/icons/parent.gif ; \
	cp src/server/web/icons/space.gif $(ME_WEB_PREFIX)/icons/space.gif ; \
	cp src/server/web/icons/text.gif $(ME_WEB_PREFIX)/icons/text.gif ; \
	cp src/server/web/iehacks.css $(ME_WEB_PREFIX)/iehacks.css ; \
	mkdir -p "$(ME_WEB_PREFIX)/images" ; \
	cp src/server/web/images/banner.jpg $(ME_WEB_PREFIX)/images/banner.jpg ; \
	cp src/server/web/images/bottomShadow.jpg $(ME_WEB_PREFIX)/images/bottomShadow.jpg ; \
	cp src/server/web/images/shadow.jpg $(ME_WEB_PREFIX)/images/shadow.jpg ; \
	cp src/server/web/index.html $(ME_WEB_PREFIX)/index.html ; \
	cp src/server/web/min-index.html $(ME_WEB_PREFIX)/min-index.html ; \
	cp src/server/web/print.css $(ME_WEB_PREFIX)/print.css ; \
	cp src/server/web/screen.css $(ME_WEB_PREFIX)/screen.css ; \
	mkdir -p "$(ME_WEB_PREFIX)/test" ; \
	cp src/server/web/test/bench.html $(ME_WEB_PREFIX)/test/bench.html ; \
	cp src/server/web/test/index.html $(ME_WEB_PREFIX)/test/index.html ; \
	cp src/server/web/test/test.cgi $(ME_WEB_PREFIX)/test/test.cgi ; \
	cp src/server/web/test/test.esp $(ME_WEB_PREFIX)/test/test.esp ; \
	cp src/server/web/test/test.html $(ME_WEB_PREFIX)/test/test.html ; \
	cp src/server/web/test/test.pl $(ME_WEB_PREFIX)/test/test.pl ; \
	cp src/server/web/test/test.py $(ME_WEB_PREFIX)/test/test.py ; \
	mkdir -p "$(ME_WEB_PREFIX)/test" ; \
	cp src/server/web/test/test.cgi $(ME_WEB_PREFIX)/test/test.cgi ; \
	chmod 755 "$(ME_WEB_PREFIX)/test/test.cgi" ; \
	cp src/server/web/test/test.pl $(ME_WEB_PREFIX)/test/test.pl ; \
	chmod 755 "$(ME_WEB_PREFIX)/test/test.pl" ; \
	cp src/server/web/test/test.py $(ME_WEB_PREFIX)/test/test.py ; \
	chmod 755 "$(ME_WEB_PREFIX)/test/test.py" ; \
	mkdir -p "$(ME_VAPP_PREFIX)/bin" ; \
	cp $(BUILD)/bin/appman $(ME_VAPP_PREFIX)/bin/appman ; \
	chmod 755 "$(ME_VAPP_PREFIX)/bin/appman" ; \
	mkdir -p "$(ME_BIN_PREFIX)" ; \
	rm -f "$(ME_BIN_PREFIX)/appman" ; \
	ln -s "$(ME_VAPP_PREFIX)/bin/appman" "$(ME_BIN_PREFIX)/appman" ; \
	mkdir -p "$(ME_ROOT_PREFIX)/Library/LaunchDaemons" ; \
	cp installs/macosx/com.embedthis.appweb.plist $(ME_ROOT_PREFIX)/Library/LaunchDaemons/com.embedthis.appweb.plist ; \
	chmod 644 "$(ME_ROOT_PREFIX)/Library/LaunchDaemons/com.embedthis.appweb.plist" ; \
	if [ "$(ME_COM_ESP)" = 1 ]; then true ; \
	mkdir -p "$(ME_VAPP_PREFIX)/bin" ; \
	cp $(BUILD)/bin/appweb-esp $(ME_VAPP_PREFIX)/bin/appesp ; \
	chmod 755 "$(ME_VAPP_PREFIX)/bin/appesp" ; \
	mkdir -p "$(ME_BIN_PREFIX)" ; \
	rm -f "$(ME_BIN_PREFIX)/appesp" ; \
	ln -s "$(ME_VAPP_PREFIX)/bin/appesp" "$(ME_BIN_PREFIX)/appesp" ; \
	fi ; \
	if [ "$(ME_COM_ESP)" = 1 ]; then true ; \
	mkdir -p "$(ME_VAPP_PREFIX)/bin" ; \
	cp $(BUILD)/bin/appweb-esp $(ME_VAPP_PREFIX)/bin/appweb-esp ; \
	chmod 755 "$(ME_VAPP_PREFIX)/bin/appweb-esp" ; \
	mkdir -p "$(ME_BIN_PREFIX)" ; \
	rm -f "$(ME_BIN_PREFIX)/appweb-esp" ; \
	ln -s "$(ME_VAPP_PREFIX)/bin/appweb-esp" "$(ME_BIN_PREFIX)/appweb-esp" ; \
	fi ; \
	if [ "$(ME_COM_ESP)" = 1 ]; then true ; \
	fi ; \
	if [ "$(ME_COM_ESP)" = 1 ]; then true ; \
	mkdir -p "$(ME_VAPP_PREFIX)/bin" ; \
	cp $(BUILD)/bin/esp-compile.json $(ME_VAPP_PREFIX)/bin/esp-compile.json ; \
	cp $(BUILD)/bin/vcvars.bat $(ME_VAPP_PREFIX)/bin/vcvars.bat ; \
	fi ; \
	mkdir -p "$(ME_VAPP_PREFIX)/bin" ; \
	cp $(BUILD)/bin/http $(ME_VAPP_PREFIX)/bin/http ; \
	chmod 755 "$(ME_VAPP_PREFIX)/bin/http" ; \
	mkdir -p "$(ME_VAPP_PREFIX)/inc" ; \
	cp $(BUILD)/inc/me.h $(ME_VAPP_PREFIX)/inc/me.h ; \
	mkdir -p "$(ME_INC_PREFIX)/appweb" ; \
	rm -f "$(ME_INC_PREFIX)/appweb/me.h" ; \
	ln -s "$(ME_VAPP_PREFIX)/inc/me.h" "$(ME_INC_PREFIX)/appweb/me.h" ; \
	cp src/osdep/osdep.h $(ME_VAPP_PREFIX)/inc/osdep.h ; \
	mkdir -p "$(ME_INC_PREFIX)/appweb" ; \
	rm -f "$(ME_INC_PREFIX)/appweb/osdep.h" ; \
	ln -s "$(ME_VAPP_PREFIX)/inc/osdep.h" "$(ME_INC_PREFIX)/appweb/osdep.h" ; \
	cp src/appweb.h $(ME_VAPP_PREFIX)/inc/appweb.h ; \
	mkdir -p "$(ME_INC_PREFIX)/appweb" ; \
	rm -f "$(ME_INC_PREFIX)/appweb/appweb.h" ; \
	ln -s "$(ME_VAPP_PREFIX)/inc/appweb.h" "$(ME_INC_PREFIX)/appweb/appweb.h" ; \
	cp src/customize.h $(ME_VAPP_PREFIX)/inc/customize.h ; \
	mkdir -p "$(ME_INC_PREFIX)/appweb" ; \
	rm -f "$(ME_INC_PREFIX)/appweb/customize.h" ; \
	ln -s "$(ME_VAPP_PREFIX)/inc/customize.h" "$(ME_INC_PREFIX)/appweb/customize.h" ; \
	cp src/http/http.h $(ME_VAPP_PREFIX)/inc/http.h ; \
	mkdir -p "$(ME_INC_PREFIX)/appweb" ; \
	rm -f "$(ME_INC_PREFIX)/appweb/http.h" ; \
	ln -s "$(ME_VAPP_PREFIX)/inc/http.h" "$(ME_INC_PREFIX)/appweb/http.h" ; \
	cp src/mpr/mpr.h $(ME_VAPP_PREFIX)/inc/mpr.h ; \
	mkdir -p "$(ME_INC_PREFIX)/appweb" ; \
	rm -f "$(ME_INC_PREFIX)/appweb/mpr.h" ; \
	ln -s "$(ME_VAPP_PREFIX)/inc/mpr.h" "$(ME_INC_PREFIX)/appweb/mpr.h" ; \
	cp src/pcre/pcre.h $(ME_VAPP_PREFIX)/inc/pcre.h ; \
	mkdir -p "$(ME_INC_PREFIX)/appweb" ; \
	rm -f "$(ME_INC_PREFIX)/appweb/pcre.h" ; \
	ln -s "$(ME_VAPP_PREFIX)/inc/pcre.h" "$(ME_INC_PREFIX)/appweb/pcre.h" ; \
	if [ "$(ME_COM_ESP)" = 1 ]; then true ; \
	mkdir -p "$(ME_VAPP_PREFIX)/inc" ; \
	cp src/esp/esp.h $(ME_VAPP_PREFIX)/inc/esp.h ; \
	mkdir -p "$(ME_INC_PREFIX)/appweb" ; \
	rm -f "$(ME_INC_PREFIX)/appweb/esp.h" ; \
	ln -s "$(ME_VAPP_PREFIX)/inc/esp.h" "$(ME_INC_PREFIX)/appweb/esp.h" ; \
	fi ; \
	mkdir -p "$(ME_VAPP_PREFIX)/doc/man1" ; \
	cp doc/dist/man/appman.1 $(ME_VAPP_PREFIX)/doc/man1/appman.1 ; \
	mkdir -p "$(ME_MAN_PREFIX)/man1" ; \
	rm -f "$(ME_MAN_PREFIX)/man1/appman.1" ; \
	ln -s "$(ME_VAPP_PREFIX)/doc/man1/appman.1" "$(ME_MAN_PREFIX)/man1/appman.1" ; \
	cp doc/dist/man/appweb.1 $(ME_VAPP_PREFIX)/doc/man1/appweb.1 ; \
	mkdir -p "$(ME_MAN_PREFIX)/man1" ; \
	rm -f "$(ME_MAN_PREFIX)/man1/appweb.1" ; \
	ln -s "$(ME_VAPP_PREFIX)/doc/man1/appweb.1" "$(ME_MAN_PREFIX)/man1/appweb.1" ; \
	cp doc/dist/man/appwebMonitor.1 $(ME_VAPP_PREFIX)/doc/man1/appwebMonitor.1 ; \
	mkdir -p "$(ME_MAN_PREFIX)/man1" ; \
	rm -f "$(ME_MAN_PREFIX)/man1/appwebMonitor.1" ; \
	ln -s "$(ME_VAPP_PREFIX)/doc/man1/appwebMonitor.1" "$(ME_MAN_PREFIX)/man1/appwebMonitor.1" ; \
	cp doc/dist/man/authpass.1 $(ME_VAPP_PREFIX)/doc/man1/authpass.1 ; \
	mkdir -p "$(ME_MAN_PREFIX)/man1" ; \
	rm -f "$(ME_MAN_PREFIX)/man1/authpass.1" ; \
	ln -s "$(ME_VAPP_PREFIX)/doc/man1/authpass.1" "$(ME_MAN_PREFIX)/man1/authpass.1" ; \
	cp doc/dist/man/esp.1 $(ME_VAPP_PREFIX)/doc/man1/esp.1 ; \
	mkdir -p "$(ME_MAN_PREFIX)/man1" ; \
	rm -f "$(ME_MAN_PREFIX)/man1/esp.1" ; \
	ln -s "$(ME_VAPP_PREFIX)/doc/man1/esp.1" "$(ME_MAN_PREFIX)/man1/esp.1" ; \
	cp doc/dist/man/http.1 $(ME_VAPP_PREFIX)/doc/man1/http.1 ; \
	mkdir -p "$(ME_MAN_PREFIX)/man1" ; \
	rm -f "$(ME_MAN_PREFIX)/man1/http.1" ; \
	ln -s "$(ME_VAPP_PREFIX)/doc/man1/http.1" "$(ME_MAN_PREFIX)/man1/http.1" ; \
	cp doc/dist/man/makerom.1 $(ME_VAPP_PREFIX)/doc/man1/makerom.1 ; \
	mkdir -p "$(ME_MAN_PREFIX)/man1" ; \
	rm -f "$(ME_MAN_PREFIX)/man1/makerom.1" ; \
	ln -s "$(ME_VAPP_PREFIX)/doc/man1/makerom.1" "$(ME_MAN_PREFIX)/man1/makerom.1"

#
#   start
#
DEPS_73 += compile
DEPS_73 += stop

start: $(DEPS_73)
	./$(BUILD)/bin/appman install enable start

#
#   install
#
DEPS_74 += installPrep
DEPS_74 += compile
DEPS_74 += stop
DEPS_74 += installBinary
DEPS_74 += start

install: $(DEPS_74)

#
#   run
#
DEPS_75 += compile

run: $(DEPS_75)
	( \
	cd src/server; \
	../../$(BUILD)/bin/appweb --log stdout:2 ; \
	)

#
#   uninstall
#
DEPS_76 += stop

uninstall: $(DEPS_76)
	( \
	cd installs; \
	rm -f "$(ME_ETC_PREFIX)/appweb.conf" ; \
	rm -f "$(ME_ETC_PREFIX)/esp.conf" ; \
	rm -f "$(ME_ETC_PREFIX)/mine.types" ; \
	rm -f "$(ME_ETC_PREFIX)/install.conf" ; \
	rm -fr "$(ME_INC_PREFIX)/appweb" ; \
	)

#
#   uninstallBinary
#

uninstallBinary: $(DEPS_77)
	rm -fr "$(ME_WEB_PREFIX)" ; \
	rm -fr "$(ME_SPOOL_PREFIX)" ; \
	rm -fr "$(ME_CACHE_PREFIX)" ; \
	rm -fr "$(ME_LOG_PREFIX)" ; \
	rm -fr "$(ME_VAPP_PREFIX)" ; \
	rmdir -p "$(ME_ETC_PREFIX)" 2>/dev/null ; true ; \
	rmdir -p "$(ME_WEB_PREFIX)" 2>/dev/null ; true ; \
	rmdir -p "$(ME_LOG_PREFIX)" 2>/dev/null ; true ; \
	rmdir -p "$(ME_SPOOL_PREFIX)" 2>/dev/null ; true ; \
	rmdir -p "$(ME_CACHE_PREFIX)" 2>/dev/null ; true ; \
	rm -f "$(ME_APP_PREFIX)/latest" ; \
	rmdir -p "$(ME_APP_PREFIX)" 2>/dev/null ; true

#
#   version
#

version: $(DEPS_78)
	echo $(VERSION)

