"""Support optional C++ references and links within the current HTML build."""

from docutils import nodes
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


def localize_cpp_api_links(app, doctree, docname):
    """Keep Markdown links readable while targeting this build's declarations.

    The published latest reference can lag renamed APIs. When the C++ reference
    is generated locally, use its matching anchors; otherwise retain the
    published links for builds without Doxygen.
    """
    if app.builder.format != 'html' or not app.tags.has('doxygen'):
        return
    prefix = 'https://dr-evt.readthedocs.io/en/latest/api/cpp/'
    for reference in doctree.findall(nodes.reference):
        uri = reference.get('refuri', '')
        if not uri.startswith(prefix):
            continue
        path, separator, fragment = uri[len(prefix):].partition('#')
        if not path.endswith('.html'):
            continue
        target = 'api/cpp/' + path[:-5]
        if target not in app.env.found_docs:
            continue
        reference['refuri'] = app.builder.get_relative_uri(docname, target)
        if separator:
            reference['refuri'] += '#' + fragment
        reference['internal'] = True


def setup(app):
    app.add_directive('if-doxygen', IfDoxygenDirective)
    app.connect('doctree-resolved', localize_cpp_api_links)
    return {'parallel_read_safe': True, 'parallel_write_safe': True}
