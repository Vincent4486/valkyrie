# SPDX-License-Identifier: CC0-1.0
# Documentation Build System for Valkyrie OS

.PHONY: all pdf html clean help man info

# Build directories
BUILD_DIR := build
PDF_DIR := $(BUILD_DIR)/pdf
HTML_DIR := $(BUILD_DIR)/html
MAN_DIR := $(BUILD_DIR)/man
INFO_DIR := $(BUILD_DIR)/info

# Source files
ADOC_FILES := $(wildcard *.ad)
PDF_TARGETS := $(patsubst %.ad,$(PDF_DIR)/%.pdf,$(ADOC_FILES))
HTML_TARGETS := $(patsubst %.ad,$(HTML_DIR)/%.html,$(ADOC_FILES))
MAN_TARGETS := $(patsubst %.ad,$(MAN_DIR)/%,$(ADOC_FILES))
INFO_TARGETS := $(patsubst %.ad,$(INFO_DIR)/%.info,$(ADOC_FILES))

# Pandoc options
PANDOC_FROM := asciidoc
PANDOC_PDF_OPTS := -f $(PANDOC_FROM) -t pdf --pdf-engine=xelatex
PANDOC_MAN_OPTS := -f $(PANDOC_FROM) -t man
PANDOC_TEXINFO_OPTS := -f $(PANDOC_FROM) -t texinfo --top-level-division=chapter

# Commands
PANDOC := pandoc
ASCIIDOCTOR := asciidoctor
MAKEINFO := makeinfo

# Detect availability
HAVE_PANDOC := $(shell which pandoc 2>/dev/null)
HAVE_ASCIIDOCTOR := $(shell which asciidoctor 2>/dev/null)
HAVE_MAKEINFO := $(shell which makeinfo 2>/dev/null)

# Colored output
RED := \033[0;31m
GREEN := \033[0;32m
YELLOW := \033[0;33m
NC := \033[0m

help:
	@echo "$(GREEN)Valkyrie OS Documentation Builder$(NC)"
	@echo ""
	@echo "Targets:"
	@echo "  $(YELLOW)all$(NC)          - Build PDF, HTML, man pages, and info pages"
	@echo "  $(YELLOW)pdf$(NC)          - Build all PDF documents"
	@echo "  $(YELLOW)html$(NC)         - Build all HTML documents"
	@echo "  $(YELLOW)man$(NC)          - Build man pages"
	@echo "  $(YELLOW)info$(NC)         - Build info pages"
	@echo "  $(YELLOW)clean$(NC)        - Remove all build artifacts"
	@echo "  $(YELLOW)help$(NC)         - Show this help message"
	@echo ""
	@echo "Build artifacts in: $(BUILD_DIR)/"
	@echo ""
	@echo "Tool Status:"
	@if [ -n "$(HAVE_PANDOC)" ]; then echo "  $(GREEN)✓$(NC) Pandoc"; else echo "  $(RED)✗$(NC) Pandoc (required)"; fi
	@if [ -n "$(HAVE_MAKEINFO)" ]; then echo "  $(GREEN)✓$(NC) makeinfo"; else echo "  $(RED)✗$(NC) makeinfo (optional, for info pages)"; fi

# Check for required tools
check-tools:
	@if [ -z "$(HAVE_PANDOC)" ]; then \
		echo "$(RED)ERROR: pandoc not found$(NC)"; \
		echo "  Install: brew install pandoc (macOS) or apt-get install pandoc (Debian/Ubuntu)"; \
		exit 1; \
	fi

check-asciidoctor:
	@if [ -z "$(HAVE_ASCIIDOCTOR)" ]; then \
		echo "$(RED)ERROR: asciidoctor not found$(NC)"; \
		echo "  Install: gem install asciidoctor (or apt-get install asciidoctor)"; \
		exit 1; \
	fi

# Create build directories
$(BUILD_DIR) $(PDF_DIR) $(HTML_DIR) $(MAN_DIR) $(INFO_DIR):
	@mkdir -p $@

# PDF targets
$(PDF_DIR)/%.pdf: %.ad | check-tools $(PDF_DIR)
	@echo "$(YELLOW)Building PDF: $@$(NC)"
	@$(PANDOC) $(PANDOC_PDF_OPTS) -o $@ $<

# HTML targets
$(HTML_DIR)/%.html: %.ad | check-asciidoctor $(HTML_DIR)
	@echo "$(YELLOW)Building HTML: $@$(NC)"
	@$(ASCIIDOCTOR) -a toc -o $@ $<

# PDF build target
pdf: $(PDF_TARGETS)
	@echo "$(GREEN)✓ PDF documents built in $(PDF_DIR)/$(NC)"

# HTML build target
html: $(HTML_TARGETS)
	@echo "$(GREEN)✓ HTML documents built in $(HTML_DIR)/$(NC)"

# Man pages target (using pandoc)
man: | check-tools $(MAN_DIR)
	@for file in $(ADOC_FILES); do \
		outname=$$(basename "$$file" .ad); \
		echo "$(YELLOW)Building man page: $$outname.1$(NC)"; \
		$(PANDOC) $(PANDOC_MAN_OPTS) -o $(MAN_DIR)/"$$outname".1 "$$file" 2>/dev/null || true; \
	done
	@echo "$(GREEN)✓ Man pages built in $(MAN_DIR)/$(NC)"

# Info pages target (requires pandoc and makeinfo)
info: html | check-tools $(INFO_DIR)
	@for html in $(HTML_DIR)/*.html; do \
		if [ -f "$$html" ]; then \
			base=$$(basename "$$html" .html); \
			echo "$(YELLOW)Building info page: vk-$$base.info$(NC)"; \
			$(PANDOC) $(PANDOC_TEXINFO_OPTS) "$$html" -o $(INFO_DIR)/vk-$$base.texi 2>/dev/null && \
			if [ -n "$(HAVE_MAKEINFO)" ]; then \
				$(MAKEINFO) $(INFO_DIR)/vk-$$base.texi -o $(INFO_DIR)/vk-$$base.info 2>/dev/null || \
				echo "  $(YELLOW)Note: makeinfo failed; skipping $(INFO_DIR)/vk-$$base.info$(NC)"; \
			else \
				echo "  $(YELLOW)Note: makeinfo not available; skipping $(INFO_DIR)/vk-$$base.info$(NC)"; \
			fi || true; \
		fi; \
	done
	@echo "$(GREEN)✓ Info pages built in $(INFO_DIR)/$(NC)"

# Build all formats
all: pdf html man info
	@echo ""
	@echo "$(GREEN)✓ All documentation built successfully!$(NC)"
	@echo ""
	@echo "Output locations:"
	@echo "  PDF:  $(PDF_DIR)/"
	@echo "  HTML: $(HTML_DIR)/"
	@echo "  Man:  $(MAN_DIR)/"
	@echo "  Info: $(INFO_DIR)/"

# Clean build artifacts
clean:
	@echo "$(YELLOW)Cleaning build artifacts...$(NC)"
	@rm -rf $(BUILD_DIR)
	@echo "$(GREEN)✓ Clean complete$(NC)"

.DEFAULT_GOAL := help
