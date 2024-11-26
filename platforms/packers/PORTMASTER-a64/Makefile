PROJECT_ROOT ?= $(shell realpath ../../..)
BUILD_DIR ?= $(PROJECT_ROOT)/build/platforms/ma30
PLATFORMS_DIR ?= $(shell realpath "`dirname "$(BUILD_DIR)"`")
ASSETS_DIR := $(PROJECT_ROOT)/assets
PACKER_DIR := $(shell pwd)

ASSETS := $(filter-out $(ASSETS_DIR)/lores, $(wildcard $(ASSETS_DIR)/*))
PACKAGE_FILES := $(shell find $(PACKER_DIR)/package -type f | sed 's/ /\\ /g')

PACKAGE_ROOT := $(BUILD_DIR)/planetmerge
BINARY_DIR_PKG := $(PACKAGE_ROOT)/planetmerge
ASSET_DIR_PKG := $(BINARY_DIR_PKG)/assets
ZIP_PACKAGE := $(PLATFORMS_DIR)/planet_merge_portmaster_a64.zip

.PHONY: package

package: $(ZIP_PACKAGE)
	@echo Project root: $(PROJECT_ROOT)
	@echo Target path: $(BUILD_DIR)
	@echo ZIP package: $(ZIP_PACKAGE)

$(ZIP_PACKAGE): $(ASSETS) $(BUILD_DIR)/planets $(PACKAGE_FILES)
	mkdir -p "$(BINARY_DIR_PKG)"
	mkdir -p "$(ASSET_DIR_PKG)"
	@echo Assets: $(ASSETS)
	cp -u $(ASSETS) "$(ASSET_DIR_PKG)/"
	cp -u "$(BUILD_DIR)/planets" "$(BINARY_DIR_PKG)/planets.aarch64"
	cp -u $(PACKAGE_FILES) "$(PACKAGE_ROOT)/"
	(cd "$(PACKAGE_ROOT)"; zip -r $(ZIP_PACKAGE) *)
