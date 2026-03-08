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

# AsciiDoc attributes (customize as needed)
ASCIIDOCTOR_OPTS := -a toc=left -a icons=font -a experimental=true
PDF_OPTS := $(ASCIIDOCTOR_OPTS) -b pdf
HTML_OPTS := $(ASCIIDOCTOR_OPTS) -b html5

# Commands
ASCIIDOCTOR := asciidoctor
A2X := a2x
PANDOC := pandoc

# Detect availability
HAVE_ASCIIDOCTOR := $(shell command -v $(ASCIIDOCTOR) 2>/dev/null)
HAVE_ASCIIDOCTOR_PDF := $(shell $(ASCIIDOCTOR) -v 2>/dev/null | grep -q 'asciidoctor-pdf' && echo 1 || echo 0)
HAVE_A2X := $(shell command -v $(A2X) 2>/dev/null)
HAVE_PANDOC := $(shell command -v $(PANDOC) 2>/dev/null)

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
	@echo "  $(YELLOW)man$(NC)          - Build man pages (requires a2x)"
	@echo "  $(YELLOW)info$(NC)         - Build info pages (requires pandoc)"
	@echo "  $(YELLOW)clean$(NC)        - Remove all build artifacts"
	@echo "  $(YELLOW)help$(NC)         - Show this help message"
	@echo ""
	@echo "Build artifacts in: $(BUILD_DIR)/"
	@echo ""
	@echo "Tool Status:"
	@if [ -n "$(HAVE_ASCIIDOCTOR)" ]; then echo "  $(GREEN)✓$(NC) AsciiDoctor"; else echo "  $(RED)✗$(NC) AsciiDoctor (required)"; fi
	@if [ "$(HAVE_ASCIIDOCTOR_PDF)" = "1" ]; then echo "  $(GREEN)✓$(NC) asciidoctor-pdf"; else echo "  $(RED)✗$(NC) asciidoctor-pdf (required for PDF)"; fi
	@if [ -n "$(HAVE_A2X)" ]; then echo "  $(GREEN)✓$(NC) a2x"; else echo "  $(RED)✗$(NC) a2x (optional, for man pages)"; fi
	@if [ -n "$(HAVE_PANDOC)" ]; then echo "  $(GREEN)✓$(NC) Pandoc"; else echo "  $(RED)✗$(NC) Pandoc (optional, for info pages)"; fi

# Check for required tools
check-tools:
	@if [ -z "$(HAVE_ASCIIDOCTOR)" ]; then \
		echo "$(RED)ERROR: asciidoctor not found$(NC)"; \
		echo "  Install: gem install asciidoctor"; \
		exit 1; \
	fi

check-tools-pdf: check-tools
	@if [ "$(HAVE_ASCIIDOCTOR_PDF)" != "1" ]; then \
		echo "$(RED)ERROR: asciidoctor-pdf converter not found$(NC)"; \
		echo "  Install: gem install asciidoctor-pdf"; \
		exit 1; \
	fi

# Create build directories
$(BUILD_DIR) $(PDF_DIR) $(HTML_DIR) $(MAN_DIR) $(INFO_DIR):
	@mkdir -p $@

# PDF targets
$(PDF_DIR)/%.pdf: %.ad | check-tools-pdf $(PDF_DIR)
	@echo "$(YELLOW)Building PDF: $@$(NC)"
	@$(ASCIIDOCTOR) $(PDF_OPTS) -o $@ $<

# HTML targets
$(HTML_DIR)/%.html: %.ad | check-tools $(HTML_DIR)
	@echo "$(YELLOW)Building HTML: $@$(NC)"
	@$(ASCIIDOCTOR) $(HTML_OPTS) -o $@ $<

# PDF build target
pdf: $(PDF_TARGETS)
	@echo "$(GREEN)✓ PDF documents built in $(PDF_DIR)/$(NC)"

# HTML build target
html: $(HTML_TARGETS)
	@echo "$(GREEN)✓ HTML documents built in $(HTML_DIR)/$(NC)"

# Man pages target (requires a2x and asciidoc)
man: | check-tools $(MAN_DIR)
	@if [ -z "$(HAVE_A2X)" ]; then \
		echo "$(RED)ERROR: a2x not found (part of asciidoc package)$(NC)"; \
		echo "  Install: apt-get install asciidoc (Debian/Ubuntu) or brew install asciidoc (macOS)"; \
		exit 1; \
	fi
	@for file in $(ADOC_FILES); do \
		if grep -q "^:man-section:" "$$file"; then \
			section=$$(grep "^:man-section:" "$$file" | cut -d: -f3); \
			if [ "$$section" != "0" ]; then \
				outname=$$(basename "$$file" .ad).$$section; \
				echo "$(YELLOW)Building man page: $$outname$(NC)"; \
				$(A2X) -d manpage -f manpage -o - "$$file" > $(MAN_DIR)/"$$outname" 2>/dev/null || true; \
			fi; \
		fi; \
	done
	@echo "$(GREEN)✓ Man pages built in $(MAN_DIR)/$(NC)"

# Info pages target (requires pandoc)
info: pdf | $(INFO_DIR)
	@if [ -z "$(HAVE_PANDOC)" ]; then \
		echo "$(RED)ERROR: pandoc not found$(NC)"; \
		echo "  Install: brew install pandoc (macOS) or apt-get install pandoc (Debian/Ubuntu)"; \
		exit 1; \
	fi
	@for pdf in $(PDF_DIR)/*.pdf; do \
		if [ -f "$$pdf" ]; then \
			base=$$(basename "$$pdf" .pdf); \
			echo "$(YELLOW)Building info page: vk-$$base.info$(NC)"; \
			$(PANDOC) "$$pdf" -t texinfo -o $(INFO_DIR)/vk-$$base.texi 2>/dev/null && \
			makeinfo $(INFO_DIR)/vk-$$base.texi -o $(INFO_DIR)/vk-$$base.info 2>/dev/null || \
			echo "  $(YELLOW)Note: makeinfo not available; info page generation skipped$(NC)"; \
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
