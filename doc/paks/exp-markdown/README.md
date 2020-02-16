exp-markdown
===

Expansive plugin for Markdown files.

Provides the 'markdown' service.

### To install:

    pak install exp-markdown

### To configure in expansive.json:

* enable &mdash; Enable the compile-markdown-html service to process Markdown files.
* mappings &mdash; Files extensions to process. Defaults to { md: [ 'esp', 'html' ] }. This processes files with a '.md.esp' and '.md.html' extension.

```
{
    services: {
        'markdown': {
            enable: true,
        }
    }
}

```

### Get Pak from

[https://www.embedthis.com/pak/](https://www.embedthis.com/pak/)
