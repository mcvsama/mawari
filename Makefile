.DEFAULT_GOAL := all

CMAKE ?= cmake
BUILD_DIR ?= build
BUILD_TYPE ?= Release
BUILD_TESTING ?= ON
JOBS ?= $(shell getconf _NPROCESSORS_ONLN 2>/dev/null || printf '2')
CMAKE_ARGS ?=
PREFIX ?= $(HOME)/.local

.PHONY: all install check-deps check-cmake clean help

all: check-deps
	+@"$(CMAKE)" --build "$(BUILD_DIR)" --parallel "$(JOBS)"

install: all
	@"$(CMAKE)" --install "$(BUILD_DIR)" --prefix "$(PREFIX)"
	@if [ -z "$$DESTDIR" ] && [ -n "$$DBUS_SESSION_BUS_ADDRESS" ] && command -v kbuildsycoca6 >/dev/null 2>&1; then \
		kbuildsycoca6 --noincremental || \
			printf '%s\n' 'Mawari was installed, but the KDE menu cache could not be refreshed. Log out and back in if its entry does not appear.' >&2; \
	fi

# CMake validates the compiler, Qt version, development headers and libraries.
# Reuse an existing generator (e.g. Ninja) when the build directory has a cache.
check-deps: check-cmake
	@printf '%s\n' 'Checking the C++23 compiler and required libraries with CMake...'
	@"$(CMAKE)" -S "$(CURDIR)" -B "$(BUILD_DIR)" \
		-DCMAKE_BUILD_TYPE="$(BUILD_TYPE)" \
		-DBUILD_TESTING="$(BUILD_TESTING)" $(CMAKE_ARGS)

check-cmake:
	@command -v "$(CMAKE)" >/dev/null 2>&1 || { \
		printf '%s\n' 'Error: CMake 3.25 or newer is required. Install CMake or set CMAKE=/path/to/cmake.' >&2; \
		exit 1; \
	}

clean: check-cmake
	+@if [ -f "$(BUILD_DIR)/CMakeCache.txt" ]; then \
		"$(CMAKE)" --build "$(BUILD_DIR)" --target clean; \
	else \
		printf '%s\n' "Nothing to clean in $(BUILD_DIR)."; \
	fi

help:
	@printf '%s\n' \
		'make             Check dependencies, configure and build Mawari (Release).' \
		'make install     Build and install in ~/.local, including the application menu entry and icon.' \
		'make check-deps  Check the compiler and libraries without building Mawari.' \
		'make clean       Remove compiled files, keeping the CMake configuration.' \
		'' \
		'Options: BUILD_DIR=build BUILD_TYPE=Release BUILD_TESTING=ON JOBS=<CPU count>' \
		'         PREFIX=$$HOME/.local CMAKE=cmake CMAKE_ARGS="<extra CMake options>"' \
		'Example: make BUILD_TESTING=OFF JOBS=4'
