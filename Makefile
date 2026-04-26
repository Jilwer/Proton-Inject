ROOT := $(patsubst %/,%,$(dir $(abspath $(lastword $(MAKEFILE_LIST)))))

.PHONY: all build release clean

all: build

build:
	"$(ROOT)/build.sh"

release:
	"$(ROOT)/release.sh"

clean:
	rm -rf "$(ROOT)/build" \
		"$(ROOT)/target" \
		"$(ROOT)/embedded/assets"/* \
		"$(ROOT)/embedded/loader/target" \
		"$(ROOT)/embedded/injector/target" \
		"$(ROOT)/embedded/injector/injector_exe/target"
