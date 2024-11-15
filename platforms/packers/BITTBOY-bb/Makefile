PROJECT_ROOT ?= $(shell realpath ../../..)
BUILD_DIR ?= $(PROJECT_ROOT)/build/platforms/bb
PLATFORMS_DIR ?= $(shell realpath "`dirname "$(BUILD_DIR)"`")
ASSETS_DIR := $(PROJECT_ROOT)/assets
PACKER_DIR := $(shell pwd)

ASSETS_LO_RES := $(wildcard $(ASSETS_DIR)/lores/*)
ASSETS_HI_RES := $(wildcard $(ASSETS_DIR)/*)
LO_RES_NAMES := $(patsubst $(ASSETS_DIR)/lores/%, $(ASSETS_DIR)/%, $(ASSETS_LO_RES))

ASSETS := $(filter-out $(LO_RES_NAMES) $(ASSETS_DIR)/hi_background.jpg $(ASSETS_DIR)/lores, $(ASSETS_HI_RES))
LORES_ASSETS := $(wildcard $(ASSETS_DIR)/lores/*)

SD_ROOT := $(BUILD_DIR)/SD
BINARY_DIR_SD := $(SD_ROOT)/games/planetmerge
ASSET_DIR_SD := $(BINARY_DIR_SD)/assets
SHORTCUT_DIR_SD := $(SD_ROOT)/gmenu2x/sections/games
ICONS_DIR_SD := $(SD_ROOT)/gmenu2x/skins/Default/ICONS
ZIP_PACKAGE := $(PLATFORMS_DIR)/planet_merge_bb_cfw.zip

.PHONY: package

package: $(ZIP_PACKAGE)
	@echo Project root: $(PROJECT_ROOT)
	@echo Target path: $(BUILD_DIR)
	@echo ZIP package: $(ZIP_PACKAGE)

$(ZIP_PACKAGE): $(ASSETS) $(LORES_ASSETS) $(BUILD_DIR)/planets $(PACKER_DIR)/planetmerge $(PACKER_DIR)/planetmerge.png
	mkdir -p "$(BINARY_DIR_SD)"
	mkdir -p "$(ASSET_DIR_SD)"
	mkdir -p "$(SHORTCUT_DIR_SD)"
	mkdir -p "$(ICONS_DIR_SD)"
	@echo Assets: $(ASSETS)
	cp -u $(ASSETS) "$(ASSET_DIR_SD)/"
	cp -u $(LORES_ASSETS) "$(ASSET_DIR_SD)/"
	cp -u "$(BUILD_DIR)/planets" "$(BINARY_DIR_SD)/"
	cp -u "$(PACKER_DIR)/planetmerge" "$(SHORTCUT_DIR_SD)/"
	cp -u "$(PACKER_DIR)/planetmerge.png" "$(ICONS_DIR_SD)/"
	(cd "$(SD_ROOT)"; zip -r $(ZIP_PACKAGE) *)

dumpNames:
	@for file in $(ASSETS); do \
	    echo $$file; \
	done
	@for file in $(LORES_ASSETS); do \
	    echo $$file; \
	done
