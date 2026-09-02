# Configuration file for Sphinx documentation

import os
import sys

# Project information
project = 'DR_EVT'
copyright = '2024-2026, Lawrence Livermore National Laboratory'
author = 'LLNL'
version = '1.0'
release = '1.0.0'

# Short description for metadata
html_title = 'DR_EVT Documentation'
html_short_title = 'DR_EVT'

# General configuration
extensions = [
    'sphinx.ext.autodoc',
    'sphinx.ext.napoleon',
    'sphinx.ext.viewcode',
    'sphinx.ext.githubpages',
    'myst_parser',  # For Markdown support
    'sphinxcontrib.mermaid',  # For Mermaid diagrams
]

# Markdown configuration
myst_enable_extensions = [
    "colon_fence",
    "deflist",
    "html_image",
]

# Mermaid configuration
mermaid_version = "10.6.1"  # Use specific stable version
mermaid_init_js = """
mermaid.initialize({
    startOnLoad: true,
    theme: 'default',
    flowchart: { useMaxWidth: true },
    gantt: { useMaxWidth: true }
});
"""

# Add any paths that contain templates here
templates_path = ['_templates']

# List of patterns to ignore
exclude_patterns = [
    '_build',
    'Thumbs.db',
    '.DS_Store',
    'dev/session-notes/*',  # Exclude development session notes
]

# Source file suffix
source_suffix = {
    '.rst': 'restructuredtext',
    '.md': 'markdown',
}

# The master toctree document
master_doc = 'index'

# HTML output options
html_theme = 'sphinx_rtd_theme'
html_static_path = ['_static']
html_logo = None
html_favicon = None

# Theme options
html_theme_options = {
    'navigation_depth': 4,
    'collapse_navigation': False,
    'sticky_navigation': True,
    'includehidden': True,
    'titles_only': False,
}

# HTML context
html_context = {
    "display_github": True,
    "github_user": "LLNL",
    "github_repo": "dr_evt",
    "github_version": "main",
    "conf_py_path": "/docs/",
}

# LaTeX output options (for PDF generation)
latex_elements = {
    'papersize': 'letterpaper',
    'pointsize': '10pt',
}

latex_documents = [
    (master_doc, 'DR_EVT.tex', 'DR_EVT Documentation',
     'LLNL', 'manual'),
]

# Manual page output
man_pages = [
    (master_doc, 'dr_evt', 'DR_EVT Documentation',
     [author], 1)
]

# Texinfo output
texinfo_documents = [
    (master_doc, 'DR_EVT', 'DR_EVT Documentation',
     author, 'DR_EVT', 'HPC Job Scheduler Simulator',
     'Miscellaneous'),
]
