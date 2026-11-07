"""Skip parsing generated API blocks when Doxygen XML is unavailable."""

from sphinx.util.docutils import SphinxDirective


class IfDoxygenDirective(SphinxDirective):
    """Parse Breathe directives only in builds that registered them.

    Sphinx's ``only`` directive parses its content before checking the tag,
    which reports unknown Breathe directives in documentation-only builds.
    """

    has_content = True

    def run(self):
        if not self.env.app.tags.has('doxygen'):
            return []
        return self.parse_content_to_nodes(allow_section_headings=True)


def setup(app):
    app.add_directive('if-doxygen', IfDoxygenDirective)
    return {'parallel_read_safe': True, 'parallel_write_safe': True}
