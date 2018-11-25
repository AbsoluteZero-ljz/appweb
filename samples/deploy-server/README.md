Deploy Appweb Sample
===

This sample demonstrates the commands to use to deploy Appweb files to a staging directory.

Requirements
---
* [Appweb](https://www.embedthis.com/appweb/download.html)
* [MakeMe Build Tool](https://www.embedthis.com/makeme/download.html)

Steps:
---

1. Appweb must be built

    me

2. Deploy

    cd top-appweb-directory
    me --sets core,libs,esp --deploy /tmp/appweb

This will copy the required Appweb files to deploy into the nominated directory.

Other sets include: 'web', 'service', 'utils', 'test', 'dev', 'doc', 'package'

Documentation:
---
* [Appweb Documentation](https://www.embedthis.com/appweb/doc/index.html)
* [Building Appweb with MakeMe](https://www.embedthis.com/appweb/doc/source/me.html)

See Also:
---
* [min-server - Minimal server configuration](../min-server/README.md)
* [simple-server - Simple one-line embedding API](../simple-server/README.md)
