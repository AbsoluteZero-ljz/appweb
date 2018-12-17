appweb-php
===

Appweb package for PHP.

### To install:

    pak install appweb-php

### To build with PHP

    ./configure --with php

### Get Pak from

[https://embedthis.com/pak/](https://embedthis.com/pak/)


### Some Tips for Building PHP from Source

The Appweb PHP handler is called mod_php.so and the module loads the actual PHP library which is called libphp5.so
on Linux and libphp5.dll on Windows. You may replace the PHP library with a custom build from the PHP
source base if you wish.

### Minimal Configure for PHP

The following configure command will build a minimal PHP library for Linux. You will certainly need to adjust for your needs.

./configure \
    --disable-debug \
    --disable-rpath \
    --disable-cli \
    --enable-bcmath \
    --enable-calendar \
    --enable-maintainer-zts \
    --enable-embed=shared \
    --enable-force-cgi-redirect \
    --enable-ftp \
    --enable-inline-optimization \
    --enable-magic-quotes \
    --enable-memory-limit \
    --enable-safe-mode \
    --enable-sockets \
    --enable-track-vars \
    --enable-trans-sid \
    --enable-wddx \
    --sysconfdir=/etc/appWeb \
    --with-pic \
    --with-exec-dir=/etc/appWeb/exec \
    --with-db \
    --with-regex=system \
    --with-pear \
    --with-xml \
    --with-xmlrpc \
    --with-zlib
