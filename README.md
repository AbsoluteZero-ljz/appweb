Embedthis Appweb
===

The compact, fast and secure web server for embedded applications.

Branches
---
The repository has several branches:

* master - Most recent release of the software.
* dev - Current ongoing development.
* X.X - Archived prior release branches for maintenance.

Licensing
---
See [LICENSE.md](LICENSE.md) for details.

### Documentation

  See https://www.embedthis.com/appweb/doc/index.html.

### Building from Source

You can build Appweb with make, Visual Studio, Xcode or [MakeMe](https://www.embedthis.com/makeme/).

The default configuration for Make and the IDE projects will build with [ESP](https://www.embedthis.com/esp/) and SSL using the [MbedTLS](https://github.com/ARMmbed/mbedtls) TLS stack. To build with CGI, OpenSSL or other modules, read the [Projects Readme](projects/README.md) for details.

### To build with Make:

#### Linux or MacOS

    make

or to see the commands as they are invoked:

    make SHOW=1

You can pass make variables to tailor the build. For a list of variables:

	make help

#### Windows

First open a Windows cmd prompt window and then set your Visual Studio environment variables by running vcvarsall.bat from your Visual Studio installation folder.

Then run a Windows cmd prompt window and type:

    make

### To build with Visual Studio:

Open the projects/appweb-windows-default.sln solution file and select Build -> Solution.

### To build with Xcode.

Open the projects/appweb-macosx-default.sln solution file and build the solution.

### To build with MakeMe:

To install MakeMe, download it from https://www.embedthis.com/makeme/.

    ./configure
    me

For a list of configure options:

	./configure --help

### To run

	make run

or

    me run

### To install:

    sudo make install

or

    sudo me install

### To uninstall

    sudo make uninstall

or

    sudo me uninstall

### To test:

    me test

Resources
---
  - [Appweb web site](https://www.embedthis.com/)
  - [Appweb GitHub repository](http://github.com/embedthis/appweb)
  - [Embedthis web site](https://www.embedthis.com/)
