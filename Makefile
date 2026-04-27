ROOT := $(patsubst %/,%,$(dir $(abspath $(lastword $(MAKEFILE_LIST)))))
ARGS ?=

.PHONY: all build run release clean

all: build

build:
	"$(ROOT)/scripts/build.sh"

run: build
	"$(ROOT)/build/proton-inject" $(ARGS)

release:
	"$(ROOT)/scripts/release.sh"

clean:
	rm -rf "$(ROOT)/build" \
		"$(ROOT)/target" \
		"$(ROOT)/embedded/assets"/* \
		"$(ROOT)/embedded/loader/target" \
		"$(ROOT)/embedded/injector/target" \
		"$(ROOT)/embedded/injector/injector_exe/target"
