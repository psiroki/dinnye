PROJECT_ROOT ?= $(shell realpath ../../..)
BUILD_DIR ?= $(PROJECT_ROOT)/build/platforms/mm
PLATFORMS_DIR ?= $(shell realpath "`dirname "$(BUILD_DIR)"`")
ASSETS_DIR := $(PROJECT_ROOT)/assets
PACKER_DIR := $(shell pwd)

ASSETS := $(filter-out $(ASSETS_DIR)/hi_background.jpg $(ASSETS_DIR)/lores, $(wildcard $(ASSETS_DIR)/*))

SD_ROOT := $(BUILD_DIR)/SD
BINARY_DIR_SD := $(SD_ROOT)/Roms/PORTS/planetmerge
ASSET_DIR_SD := $(BINARY_DIR_SD)/assets
SHORTCUT_DIR_SD := $(SD_ROOT)/Roms/PORTS
ZIP_PACKAGE := $(PLATFORMS_DIR)/planet_merge_rg35xx_garlic.zip

.PHONY: package

package: $(ZIP_PACKAGE)
	@echo Project root: $(PROJECT_ROOT)
	@echo Target path: $(BUILD_DIR)
	@echo ZIP package: $(ZIP_PACKAGE)

$(ZIP_PACKAGE): $(ASSETS) $(BUILD_DIR)/planets $(PACKER_DIR)/Planet\ Merge.sh
	mkdir -p "$(BINARY_DIR_SD)"
	mkdir -p "$(ASSET_DIR_SD)"
	mkdir -p "$(SHORTCUT_DIR_SD)"
	cp -u $(ASSETS) "$(ASSET_DIR_SD)/"
	cp -u "$(BUILD_DIR)/planets" "$(BINARY_DIR_SD)/"
	cp -u "$(PACKER_DIR)/Planet Merge.sh" "$(SHORTCUT_DIR_SD)/"
	(cd "$(SD_ROOT)"; zip -r $(ZIP_PACKAGE) *)
