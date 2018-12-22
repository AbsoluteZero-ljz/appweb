#  
#	Makefile -- Top level Makefile for Appweb
#
#	Copyright (c) Embedthis Software LLC, 2003-2012. All Rights Reserved.
#
#
#	Standard Make targets supported are:
#	
#		make 						# Does a "make compile"
#		make clean					# Removes generated objects
#		make compile				# Compiles the source
#		make depend					# Generates the make dependencies
#		make test 					# Runs unit tests
#		make package				# Creates an installable package
#
#	Installation targets. Use "make ROOT_DIR=myDir" to do a custom local install:
#
#		make install				# Call install-binary + install-dev
#		make install-binary			# Install binary files
#		make install-dev			# Install development libraries and headers
#
#	To remove, use make uninstall-ITEM, where ITEM is a component above.
#
EJS		:= ejs-1
MPR		:= mpr-3
TOOLS	:= tools

include		build/make/Makefile.top

dependExtra:
	@[ ! -L extensions ] && ln -s ../packages extensions ; true


diff import sync:
	@if [ ! -x $(BLD_TOOLS_DIR)/edep$(BLD_BUILD_EXE) -a "$(BUILDING_CROSS)" != 1 ] ; then \
		$(MAKE) -S --no-print-directory _RECURSIVE_=1 -C $(BLD_TOP)/build/src compile ; \
	fi
	@import.ksh --$@ --src ../$(TOOLS) --dir . ../$(TOOLS)/build/export/export.gen
	@import.ksh --$@ --src ../$(TOOLS) --dir . ../$(TOOLS)/build/export/export.configure
	@import.ksh --$@ --src ../$(MPR) --dir . ../$(MPR)/build/export/export.gen
	@import.ksh --$@ --src ../$(MPR) --dir ./src/include --strip ./all/ ../$(MPR)/build/export/export.h
	@import.ksh --$@ --src ../$(MPR) --dir ./src/mpr --strip ./all/ ../$(MPR)/build/export/export.c
	@import.ksh --$@ --src ../$(EJS) --dir . ../$(EJS)/build/export/export.gen
	@import.ksh --$@ --src ../$(EJS) --dir ./src/include --strip ./all/ ../$(EJS)/build/export/export.h
	@import.ksh --$@ --src ../$(EJS) --dir ./src/ejs --strip ./all/ ../$(EJS)/build/export/export.c
	@if [ ../$(EJS)/doc/api/ejscript/index.html -nt doc/ejs/api/ejscript/index.html ] ; then \
		echo "#  import ejs doc"  \
		chmod -R +w doc/ejs doc/man ; \
		rm -fr doc/ejs ; \
		mkdir -p doc/ejs ; \
		( cd ../$(EJS)/doc ; find . -type f | \
			egrep -v '/xml/|/html/|/dsi/|.makedep|.DS_Store|.pptx|\/Archive' | cpio -pdum ../../appweb.3/doc/ejs ) ; \
		chmod +w doc/man/* ; \
		cp doc/ejs/man/*.1 doc/man ; \
		chmod -R +w doc/ejs ; \
	fi
	@echo

#
#	Convenient configure targets
#
config:
	$(call log) "[Config]" "configure"
	./configure --without-matrixSsl
	$(MAKE) depend clean >/dev/null

rom:
	./configure --host=i686-apple-darwin --build=x86_64-apple-darwin --rom --static --without-ssl --without-php --without-ejs

config32:
	./configure --host=i686-apple-darwin --build=i686-apple-darwin --without-matrixssl --without-php

cross64:
	./configure --shared --build=i686-apple-darwin --host=x86_64-apple-darwin --without-ssl --without-php

universal:
	./configure --shared --host=universal-apple-darwin --build=universal-apple-darwin --without-ssl --without-php

cross-ppc:
	./configure --shared --host=ppc-apple-darwin --without-ssl --without-php

release:
	./configure --defaults=release

release32:
	./configure --defaults=release --host=i686-apple-darwin --build=i686-apple-darwin --without-matrixssl --without-php

vx5:
	unset WIND_HOME WIND_BASE ; \
	SEARCH_PATH=/tornado ./configure --host=i386-wrs-vxworks --enable-all --without-ssl --without-php

vx vx6:
	unset WIND_HOME WIND_BASE ; \
	./configure --host=pentium-wrs-vxworks --enable-all --without-ssl --without-php

vxsim:
	unset WIND_HOME WIND_BASE ; \
	SEARCH_PATH=/tornado ./configure --host=simnt-wrs-vxworks --without-ssl --without-php --shared

wince:
	./configure --host=arm-ms-wince --shared --without-php --without-ssl --config=flat --disable-auto-compile \
		--disable-cross-compiler

#
#	Samples for cross compilation
#	
vx5env:
	ARCH=386 ; \
	WIND_HOME=c:/tornado ; \
	WIND_BASE=$$WIND_HOME ; \
	WIND_GNU_PATH=$$WIND_BASE/host ; \
	AR=$$WIND_GNU_PATH/$$WIND_HOST_TYPE/bin/ar$${ARCH}.exe \
	CC=$$WIND_GNU_PATH/$$WIND_HOST_TYPE/bin/cc$${ARCH}.exe \
	LD=$$WIND_GNU_PATH/$$WIND_HOST_TYPE/bin/ld$${ARCH}.exe \
	NM=$$WIND_GNU_PATH/$$WIND_HOST_TYPE/bin/nm$${ARCH}.exe \
	RANLIB=$$WIND_GNU_PATH/$$WIND_HOST_TYPE/bin/ranlib$${ARCH}.exe \
	STRIP=$$WIND_GNU_PATH/$$WIND_HOST_TYPE/bin/strip$${ARCH}.exe \
	IFLAGS="-I$$WIND_BASE/target/h -I$$WIND_BASE/target/h/wrn/coreip" \
	SEARCH_PATH=/tornado ./configure --host=i386-wrs-vxworks --enable-all --without-ssl --without-php

vx6env:
	ARCH=pentium ; \
	WIND_HOME=c:/WindRiver ; \
	VXWORKS=vxworks-6.3 ; \
	WIND_BASE=$$WIND_HOME/$$VXWORKS ; \
	PLATFORM=i586-wrs-vxworks ; \
	WIND_GNU_PATH=$$WIND_HOME/gnu/3.4.4-vxworks-6.3 ; \
	AR=$$WIND_GNU_PATH/$$WIND_HOST_TYPE/bin/ar$${ARCH}.exe \
	CC=$$WIND_GNU_PATH/$$WIND_HOST_TYPE/bin/cc$${ARCH}.exe \
	LD=$$WIND_GNU_PATH/$$WIND_HOST_TYPE/bin/cc$${ARCH}.exe \
	NM=$$WIND_GNU_PATH/$$WIND_HOST_TYPE/$${PLATFORM}/bin/nm.exe \
	RANLIB=$$WIND_GNU_PATH/$$WIND_HOST_TYPE/$${PLATFORM}/bin/ranlib.exe \
	STRIP=$$WIND_GNU_PATH/$$WIND_HOST_TYPE/$${PLATFORM}/bin/strip.exe \
	CFLAGS="-I$$WIND_BASE/target/h -I$$WIND_BASE/target/h/wrn/coreip" \
	./configure --host=i386-wrs-vxworks --enable-all --without-ssl --without-php

vxenv:
	wrenv -p vxworks-6.3 -f sh -o print_env

cygwin:
	./configure --cygwin --defaults=dev --enable-test --disable-samples \
	--without-php --without-matrixssl --without-openssl --without-gacompat

freebsd:
	./configure --defaults=dev --enable-test --disable-samples --without-php --without-matrixssl \
		--without-openssl --without-gacompat --disable-multithread

php:
	DIR=/Users/mob/svn/packages/php/php-5.2.0 ; \
	CC=arm-linux-gcc AR=arm-linux-ar LD=arm-linux-ld
	./configure -with-php=builtin --with-php-dir=$$DIR \
		--with-php-iflags="-I$$DIR -I$$DIR/main -I$$DIR/Zend -I$$DIR/TSRM" \
		--with-php-libpath="$$DIR/libs" --with-php-libs="libphp crypt resolv db z"

#
#	Using ubuntu packages: uclibc-toolchain, libuclibc-dev
#	Use dpkg -L package to see installed files. Installed under /usr/i386-uclibc-linux
#
uclibc:
	PREFIX=i386-uclibc-linux; \
	DIR=/usr/i386-uclibc-linux/bin ; \
	AR=$${DIR}/$${PREFIX}-ar \
	CC=$${DIR}/$${PREFIX}-gcc \
	LD=$${DIR}/$${PREFIX}-gcc \
	NM=$${DIR}/$${PREFIX}-nm \
	RANLIB=$${DIR}/$${PREFIX}-ranlib \
	STRIP=$${DIR}/$${PREFIX}-strip \
	CFLAGS="-fno-stack-protector" \
	CXXFLAGS="-fno-rtti -fno-exceptions" \
	BUILD_CC=/usr/bin/cc \
	BUILD_LD=/usr/bin/cc \
	./configure --host=i386-pc-linux --enable-all

#
#	Don't use these targets. Use make ROOT_DIR=/path install-min
#
VXDEPLOY	:= target
CEDEPLOY	:= ctarget

vxdeploy:
	[ -f .embedthis ] && subst v: c:/home/mob/hg/appweb/target >/dev/null ; true
	mkdir -p $(VXDEPLOY)/web $(VXDEPLOY)/logs $(VXDEPLOY)/cgi-bin
	rm -f $(VXDEPLOY)/*$(BLD_EXE)
	cp $(BLD_BIN_DIR)/$(BLD_HOST_SYSTEM)/*$(BLD_EXE) $(VXDEPLOY)
	[ -f $(BLD_BIN_DIR)/$(BLD_HOST_SYSTEM)/cgiProgram$(BLD_EXE) ] && \
		cp $(BLD_BIN_DIR)/$(BLD_HOST_SYSTEM)/cgiProgram$(BLD_EXE) $(VXDEPLOY)/cgi-bin ; true
	[ $(BLD_FEATURE_STATIC) = 0 ] && cp $(BLD_MOD_DIR)/$(BLD_HOST_SYSTEM)/*$(BLD_EXE) $(VXDEPLOY) ; true
	cp $(BLD_MOD_DIR)/$(BLD_HOST_SYSTEM)/*.mod $(VXDEPLOY)
	sed -e 's/UploadDir.*/UploadDir ./' < src/server/template/flat/appweb.conf >$(VXDEPLOY)/appweb.conf
	cp -r src/server/*.db $(VXDEPLOY)
	cp -r src/server/mime.types $(VXDEPLOY)
	cp -r src/server/web/* $(VXDEPLOY)/web
	cd $(VXDEPLOY)/web ; find . -name '*.ejs' | grep -v mgmt | sed 's/.\///' | xargs -i -t ajsweb compile {}
	cd $(VXDEPLOY)/demo ; ajsweb compile

IDE	:= projects/WINCE/appweb-dynamic/'Pocket PC 2003 (ARMV4)'/Debug
cedeploy:
	[ -f .embedthis ] && subst v: c:/home/mob/hg/appweb/target >/dev/null ; true
	mkdir -p $(CEDEPLOY)/web $(CEDEPLOY)/logs $(CEDEPLOY)/cgi-bin
	rm -f $(CEDEPLOY)/*$(BLD_EXE)
	[ -f $(BLD_BIN_DIR)/$(BLD_HOST_SYSTEM)/cgiProgram$(BLD_EXE) ] && \
		cp $(BLD_BIN_DIR)/$(BLD_HOST_SYSTEM)/cgiProgram$(BLD_EXE) $(CEDEPLOY)/cgi-bin ; true
	[ $(BLD_FEATURE_STATIC) = 0 ] && cp $(BLD_MOD_DIR)/$(BLD_HOST_SYSTEM)/*$(BLD_EXE) $(CEDEPLOY) ; true
	cp $(BLD_MOD_DIR)/$(BLD_HOST_SYSTEM)/*.mod $(CEDEPLOY)
	sed -e 's/UploadDir.*/UploadDir ./' < src/server/template/flat/appweb.conf >$(CEDEPLOY)/appweb.conf
	cp -r src/server/*.db $(CEDEPLOY)
	cp -r src/server/mime.types $(CEDEPLOY)
	cp -r src/server/web/* $(CEDEPLOY)/web
	cd $(CEDEPLOY)/web ; find . -name '*.ejs' | grep -v mgmt | sed 's/.\///' | xargs -i -t ajsweb compile {}
	cd $(CEDEPLOY)/demo ; ajsweb compile
	cp $(IDE)/*$(BLD_SHOBJ) $(CEDEPLOY)
	cp $(IDE)/*$(BLD_EXE) $(CEDEPLOY)

#	cp $(BLD_LIB_DIR)/$(BLD_HOST_SYSTEM)/*$(BLD_SHOBJ) $(CEDEPLOY)
#	cp $(BLD_BIN_DIR)/$(BLD_HOST_SYSTEM)/*$(BLD_EXE) $(CEDEPLOY)

REMOTE	:= \\\\castor\\vxworks
remoteDeploy:
	[ -f .embedthis ] && subst r: $(REMOTE) >/dev/null ; true
	mkdir -p $(REMOTE)/web $(REMOTE)/logs $(REMOTE)/cgi-bin
	rm -f $(REMOTE)/*$(BLD_EXE)
	cp bin/$(BLD_HOST_SYSTEM)/*$(BLD_EXE) $(REMOTE)
	cp bin/$(BLD_HOST_SYSTEM)/cgiProgram$(BLD_EXE) $(REMOTE)/cgi-bin
	[ "$(BLD_FEATURE_STATIC)" = 0 ] && cp modules/$(BLD_HOST_SYSTEM)/*$(BLD_EXE) $(REMOTE) ; true
	cp -r src/server/template/flat/appweb.conf $(DEPLOY)/appweb.conf
	cp -r src/server/*.db $(REMOTE)
	cp -r src/server/web/* $(REMOTE)/web

ifeq ($(shell [ -f buildConfig.make ] && echo found),found)
uclinuxCheck: 
	@if [ "$(UCLINUX_BUILD_USER)" = 1 ] ; \
	then \
		rm -f build/buildConfig.defaults ; \
		BLD_PRODUCT=appweb ; \
		echo "    ln -s $$BLD_PRODUCT/uclinux.defaults build/buildConfig.defaults" ;\
		ln -s $$BLD_PRODUCT/uclinux.defaults build/buildConfig.defaults ; \
		if [ ! -f build/buildConfig.cache -o ../../.config -nt buildConfig.make ] ; then \
			if [ "$$CONFIG_USER_APPWEB_DYNAMIC" = "y" ] ; then \
				SW="$$SW" ; \
			else \
				SW="$$SW --static" ; \
			fi ; \
			if [ "$$CONFIG_USER_APPWEB_MULTITHREAD" = "y" ] ; then \
				SW="$$SW --enable-multi-thread" ; \
			else SW="$$SW --disable-multi-thread" ; \
			fi ; \
			if [ "$$CONFIG_USER_APPWEB_SSL" = "y" ] ; then \
				SW="$$SW --with-openssl=../../lib/libssl" ; \
			elif [ "$$CONFIG_USER_APPWEB_MATRIXSSL" = "y" ] ; then \
				SW="$$SW --with-matrixssl=../../lib/matrixssl" ; \
			else SW="$$SW --without-ssl" ; \
			fi ; \
			if [ "$$CONFIG_USER_APPWEB_CGI" = "y" ] ; \
			then SW="$$SW --enable-cgi" ; \
			else SW="$$SW --disable-cgi" ; \
			fi ; \
			echo "    ./configure $$SW " ; \
			./configure $$SW; \
			echo "  $(MAKE) -S $(MAKEF)" ; \
			$(MAKE) -S $(MAKEF) ; \
		fi ; \
	else \
		echo "Must run configure first" ; \
		exit 2 ; \
	fi
endif

testExtra: test-projects

test-projects:
ifeq    ($(BLD_HOST_OS),WIN)
	if [ "$(BUILD_DEPTH)" -ge 3 ] ; then \
		$(BLD_TOOLS_DIR)/nativeBuild ; \
	fi
endif

#
#   Local variables:
#   tab-width: 4
#   c-basic-offset: 4
#   End:
#   vim: sw=4 ts=4 noexpandtab
#

