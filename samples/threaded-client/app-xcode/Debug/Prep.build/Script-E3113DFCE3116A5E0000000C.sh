#!/bin/bash
PATH=$PATH:/usr/local/bin
[ ! -x ${INC_DIR} ] && mkdir -p ${INC_DIR} ${OBJ_DIR} ${LIB_DIR} ${BIN_DIR}
[ ! -f ${INC_DIR}/me.h -a -f app-macosx-debug-me.h ] && cp app-macosx-debug-me.h ${INC_DIR}/me.h
if [ -f app-macosx-debug-me.h ] ; then
if ! diff ${INC_DIR}/me.h app-macosx-debug-me.h >/dev/null 2>&1 ; then
cp app-macosx-debug-me.h ${INC_DIR}/me.h
fi
fi
