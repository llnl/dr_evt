# Makefile for Sphinx documentation

# You can set these variables from the command line
SPHINXOPTS    ?=
SPHINXBUILD   ?= sphinx-build
DOXYGEN       ?= doxygen
SOURCEDIR     = .
BUILDDIR      = _build
DOXYFILE      = Doxyfile
DOXYGENDIR    = doxygen

# Put it first so that "make" without argument is like "make help"
help:
	@$(SPHINXBUILD) -M help "$(SOURCEDIR)" "$(BUILDDIR)" $(SPHINXOPTS) $(O)

.PHONY: help Makefile clean install serve doxygen

# Install dependencies
install:
	pip install -r requirements.txt

# Clean build directory
clean:
	rm -rf $(BUILDDIR) $(DOXYGENDIR)

# Build Doxygen XML from the project root. Breathe renders that XML as part of
# the Sphinx site, preserving the selected Sphinx theme.
doxygen:
	@if command -v $(DOXYGEN) >/dev/null 2>&1; then \
		cd .. && $(DOXYGEN) docs/$(DOXYFILE); \
	else \
		echo "Doxygen is not installed; skipping the C++ Development API Reference."; \
	fi

# Build HTML documentation
html: doxygen
	@$(SPHINXBUILD) -b html "$(SOURCEDIR)" "$(BUILDDIR)/html" $(SPHINXOPTS) $(O)
	@echo
	@echo "Build finished. Sphinx pages are in $(BUILDDIR)/html/."

# Build PDF documentation
pdf:
	@$(SPHINXBUILD) -b latex "$(SOURCEDIR)" "$(BUILDDIR)/latex" $(SPHINXOPTS) $(O)
	@cd $(BUILDDIR)/latex && make
	@echo
	@echo "Build finished. The PDF is in $(BUILDDIR)/latex/DR_EVT.pdf"

# Serve documentation with auto-reload
serve: doxygen
	@command -v sphinx-autobuild >/dev/null 2>&1 || { \
		echo "Installing sphinx-autobuild..."; \
		pip install sphinx-autobuild; \
	}
	@sphinx-autobuild "$(SOURCEDIR)" "$(BUILDDIR)/html" --open-browser

# Check for broken links
linkcheck:
	@$(SPHINXBUILD) -b linkcheck "$(SOURCEDIR)" "$(BUILDDIR)/linkcheck" $(SPHINXOPTS) $(O)
	@echo
	@echo "Link check complete. Results in $(BUILDDIR)/linkcheck/output.txt"

# Catch-all target: route all unknown targets to Sphinx
%: Makefile
	@$(SPHINXBUILD) -M $@ "$(SOURCEDIR)" "$(BUILDDIR)" $(SPHINXOPTS) $(O)
