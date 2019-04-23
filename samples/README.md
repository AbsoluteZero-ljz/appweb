Embedthis Appweb Samples
===

These samples are configured to use a locally built Appweb or Appweb installed to the default location
(usually /usr/local/lib/apppweb). To build the samples, you will need to install Appweb and the MakeMe build tool from:

* Appweb - [https://www.embedthis.com/appweb/download.html](https://www.embedthis.com/appweb/download.html)
* MakeMe - [https://www.embedthis.com/makeme/download.html](https://www.embedthis.com/makeme/download.html)
* Pak - [https://www.embedthis.com/pak/download.html](https://www.embedthis.com/pak/download.html)

The following samples are available:

* [chroot-server](chroot-server/README.md)              Configuring a secure chroot jail for the server.
* [cpp-handler](cpp-handler/README.md)                  C++ Handler
* [cpp-module](cpp-module/README.md)                    C++ Module
* [deploy-server](deploy-server/README.md)              Deploy Appweb files for copying to a target.
* [esp-hosted](esp-hosted/README.md)                    Host an ESP application.
* [login-basic](login-basic/README.md)                  Login using Basic or Digest authentication (not recommended).
* [login-form](login-form/README.md)                    Login using Web Forms (recommended).
* [max-server](max-server/README.md)                    Maximum configuration in appweb.conf.
* [min-server](min-server/README.md)                    Minimum configuration in appweb.conf.
* [non-blocking-client](non-blocking-client/README.md)  Using the client HTTP API to retrieve a document.
* [secure-server](secure-server/README.md)              Secure server using SSL, secure login, chroot and sandbox limits.
* [simple-action](simple-action/README.md)              Action callback. Binding C function to URI.
* [simple-client](simple-client/README.md)              Using the client HTTP API to retrieve a document.
* [simple-handler](simple-handler/README.md)            Simple Appweb URL handler.
* [simple-module](simple-module/README.md)              Simple Appweb loadable module.
* [simple-server](simple-server/README.md)              Simple Http server.
* [spy-fliter](spy-filter/README.md)                    Simple HTTP pipeline filter.
* [ssl-server](ssl-server/README.md)                    SSL server.
* [threaded-client](threaded-client/README.md)          Using the client HTTP API with threads.
* [tiny-server](tiny-server/README.md)                  Configure Appweb to be tiny.
* [typical-server](typical-server/README.md)            A more fully featured server main program.
* [websockets-echo](websockets-echo/README.md)          WebSockets echo server using an ESP controller.
* [websockets-output](websockets-output/README.md)      Using WebSockets to send a large file.

### SSL Certificates

Some samples require SSL certificates and keys. These can be generated in the Appweb source tree via:

    me generate-certs

Then in the samples directory, run

    me samples-certs

This will copy the generated certificates into the 'certs' directory.

### Building

To build the samples, see the per-sample README instructions. Many can run without extra build steps.

To build all, use:

    me --file samples.me samples

### Documentation

The full product documentation is supplied in HTML format under the doc directory. This is also available online at:

* https://www.embedthis.com/appweb/doc/index.html

Licensing
---

Please see: 

* https://www.embedthis.com/licensing/index.html


Support
---
Embedthis provides support for Appweb for commercial customers. If you are interested in commercial support, 
please contact Embedthis at:

* sales@embedthis.com

For non-commercial customers, you may ask questions in the form of well described issues at the:

* [GitHub Appweb Issue Database](http://github.com/embedthis/appweb/issues)


Copyright
---

Copyright (c) Embedthis Software. All Rights Reserved. Embedthis and Appweb are trademarks of 
Embedthis Software, LLC. Other brands and their products are trademarks of their respective holders.
