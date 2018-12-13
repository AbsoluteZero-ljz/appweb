Expansive.load({

    services: {
        name: 'markdown',

        transforms: {
            mappings: {
                md: [ 'esp', 'html', 'replace']
            },

            init: function(transform) {
                transform.md = Cmd.locate('marked')
            },

            render: function(contents, meta, transform) {
                if (transform.md) {
                    contents = run(transform.md + ' --gfm', contents)
                    contents = contents.replace(/<!--clear-->/g, '<span class="clearfix"></span>')
                    contents = contents.replace(/<!--more-->/g, '<span class="clearfix"></span>')
                }
                return contents
            }
        }
    }
})
