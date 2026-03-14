# Image Makefile for Valkyrie OS

# Inherit settings from root Makefile
IMAGE_SCRIPTS := ../scripts/scons/make_image.py
KERNEL_EXE := ../kernel/$(KERNEL_OUTPUT_NAME)
USER_LIBS := ../usr/libmath/libmath.so
USER_APPS := ../usr/sh/sh
ROOT_DIR := root

# Final output path (matching build/{arch}_{config}/valkyrieos.img|iso)
IMAGE_OUT := $(STAGING_DIR)/$(OUTPUT_FILE).$(OUTPUT_FORMAT)

.PHONY: all clean

all: $(IMAGE_OUT)

$(IMAGE_OUT): $(KERNEL_EXE) $(USER_LIBS) $(USER_APPS)
	@mkdir -p $(STAGING_DIR)
	python3 $(IMAGE_SCRIPTS) \
		--arch $(ARCH) \
		--config $(CONFIG) \
		--fs $(IMAGE_FS) \
		--size $(IMAGE_SIZE) \
		--format $(OUTPUT_FORMAT) \
		--toolchain $(TOOLCHAIN) \
		--kernel-exe $(KERNEL_EXE) \
		--kernel-name $(KERNEL_OUTPUT_NAME) \
		--output $@ \
		--libs $(USER_LIBS) \
		--apps $(USER_APPS) \
		--root-dir $(ROOT_DIR)

clean:
	rm -rf $(STAGING_DIR)
