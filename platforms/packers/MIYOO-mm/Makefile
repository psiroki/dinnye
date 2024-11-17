PROJECT_ROOT ?= $(shell realpath ../../..)
BUILD_DIR ?= $(PROJECT_ROOT)/build/platforms/mm
PLATFORMS_DIR ?= $(shell realpath "`dirname "$(BUILD_DIR)"`")
ASSETS_DIR := $(PROJECT_ROOT)/assets
PACKER_DIR := $(shell pwd)

ASSETS := $(filter-out $(ASSETS_DIR)/hi_background.jpg $(ASSETS_DIR)/lores, $(wildcard $(ASSETS_DIR)/*))

ZIP_PACKAGE := $(PLATFORMS_DIR)/planet_merge_mm_onion.zip

.PHONY: package

package: $(ZIP_PACKAGE)
	@echo Project root: $(PROJECT_ROOT)
	@echo Target path: $(BUILD_DIR)

$(ZIP_PACKAGE): $(ASSETS) $(BUILD_DIR)/planets
	mkdir -p "$(BUILD_DIR)/Roms/PORTS/Games/PlanetMerge/assets"
	mkdir -p "$(BUILD_DIR)/Roms/PORTS/Shortcuts/Puzzle games"
	@echo Assets: $(ASSETS)
	cp -u $(ASSETS) "$(BUILD_DIR)/Roms/PORTS/Games/PlanetMerge/assets/"
	cp -u "$(BUILD_DIR)/planets" "$(BUILD_DIR)/Roms/PORTS/Games/PlanetMerge/"
	cp -u "$(PACKER_DIR)/Planet Merge.notfound" "$(BUILD_DIR)/Roms/PORTS/Shortcuts/Puzzle games"
	(cd "$(BUILD_DIR)"; zip -r "$(ZIP_PACKAGE)" Roms)
