appweb-php
===

This is an extension package to support PHP in Appweb. This package implements an Appweb in-memory handler for PHP.
The package does not include PHP itself, you need to build PHP from source into a library that will be referenced by
this module.

The Appweb PHP handler is called mod_php.so and the module loads the actual PHP library which is called libphp5.so
on Linux and libphp5.dll on Windows.

### Using PHP via CGI

If you wish to use PHP via CGI, you do not need this package. Simply build Appweb with CGI support and run the standard PHP cli command. Also be aware that PHP via CGI will run much more slowly than using the appweb-php in-memory PHP handler. This will be especially apparent for small scripts where the load time for each script will be the dominant overhead.

If you use PHP via CGI, you will need to disable the PHP module if you want your script to use the ".php" extension. If your scripts do not use this extension, you can keep the PHP module enabled.

### To build Appweb with PHP with MakeMe

    ./configure --with php
    me

### To build Appweb with PHP with Make

    make ME_COM_PHP=1 ME_COM_PHP_PATH=/path/to/php

### Get Pak from

[https://www.embedthis.com/pak/](https://www.embedthis.com/pak/)

### Get MakeMe from

[https://www.embedthis.com/makeme/](https://www.embedthis.com/makeme/)

### Some Tips for Building PHP from Source

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
