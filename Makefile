.PHONY: all configure build test clean rebuild

CMAKE ?= cmake
BUILD_DIR ?= build
BUILD_TYPE ?= Release
CTEST ?= ctest

NPROC := $(shell nproc 2>/dev/null)
ifeq ($(strip $(NPROC)),)
NPROC := $(shell getconf _NPROCESSORS_ONLN 2>/dev/null)
endif
ifeq ($(strip $(NPROC)),)
NPROC := 4
endif

all: build

configure:
	$(CMAKE) -S . -B $(BUILD_DIR) -DCMAKE_BUILD_TYPE=$(BUILD_TYPE)

build: configure
	$(CMAKE) --build $(BUILD_DIR) --parallel $(NPROC)

test: build
	$(CTEST) --test-dir $(BUILD_DIR)

clean:
	$(CMAKE) --build $(BUILD_DIR) --target clean || true

rebuild: clean
	$(MAKE) build
