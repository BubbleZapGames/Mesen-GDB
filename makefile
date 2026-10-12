#Both clang & gcc work fine - clang seems to output faster code
#The game window, sound and keyboard require SDL2; HEADLESS=true builds without
#them (and without libevdev/X11), for a binary that only runs --headless.
#SDL2_PREFIX links SDL2 statically from a prefix; macOS requires it (build one
#with tools/build-sdl2-static.sh). TARGET=windows cross-compiles mesen-gdb.exe
#with MinGW-w64 (GCC, posix threads); MINGW is its tool prefix. It is headless
#unless SDL2_PREFIX names SDL's MinGW package (.../SDL2-2.30.12/x86_64-w64-mingw32).
#Run "make" to build

MESENFLAGS=

ifeq ($(TARGET),windows)
	USE_GCC := true
	MINGW ?= x86_64-w64-mingw32-
	HEADLESS ?= $(if $(SDL2_PREFIX),false,true)
endif

ifeq ($(USE_GCC),true)
	CXX := g++
	CC := gcc
	PROFILE_GEN_FLAG := -fprofile-generate
	PROFILE_USE_FLAG := -fprofile-use
else
	CXX := clang++
	CC := clang
	PROFILE_GEN_FLAG := -fprofile-instr-generate=$(CURDIR)/pgo.profraw
	PROFILE_USE_FLAG := -fprofile-instr-use=$(CURDIR)/pgo.profdata
endif

ifeq ($(TARGET),windows)
	# The -posix compilers: the core needs std::thread, which GCC's win32
	# thread model lacks.
	CXX := $(MINGW)g++-posix
	CC := $(MINGW)gcc-posix
endif


LINKCHECKUNRESOLVED := -Wl,-z,defs

LINKOPTIONS :=
MESENOS :=
UNAME_S := $(shell uname -s)

ifeq ($(UNAME_S),Linux)
	MESENOS := linux
endif

ifeq ($(UNAME_S),Darwin)
	MESENOS := osx
	LTO := false
	STATICLINK := false
	# std::filesystem needs 10.15; 11.0 is the first release on Apple Silicon.
	MESENFLAGS += -mmacosx-version-min=11.0
endif

MESENFLAGS += -m64

MACHINE := $(shell uname -m)
ifeq ($(MACHINE),x86_64)
	MESENPLATFORM := $(MESENOS)-x64
endif
ifneq ($(filter %86,$(MACHINE)),)
	MESENPLATFORM := $(MESENOS)-x64
endif
# TODO: this returns `aarch64` on one of my machines...
ifneq ($(filter arm%,$(MACHINE)),)
	MESENPLATFORM := $(MESENOS)-arm64
endif
ifeq ($(MACHINE),aarch64)
	MESENPLATFORM := $(MESENOS)-arm64
	ifeq ($(USE_GCC),true)
		#don't set -m64 on arm64 for gcc (unrecognized option)
		MESENFLAGS=
	endif
endif

PICFLAG := -fPIC
EXE :=
ifeq ($(TARGET),windows)
	MESENOS := windows
	MESENPLATFORM := windows-x64
	MESENFLAGS := -m64
	# All code is position independent on Windows.
	PICFLAG :=
	EXE := .exe
	# How `make test` runs a Windows binary on this machine.
	TESTRUNNER ?= wine
endif

HEADLESS ?= false

# SDL2. A prefix (SDL2_PREFIX) is linked statically, which Windows and macOS
# require: their binaries carry SDL in them. Linux otherwise uses the
# system's shared SDL.
ifneq ($(HEADLESS),true)
	ifneq ($(SDL2_PREFIX),)
		SDL2_CONFIG ?= $(SDL2_PREFIX)/bin/sdl2-config
		# Without SDL2main, which would replace mesen-gdb's own main(), and
		# without -mwindows, which would detach the console the CLI's stdin
		# and stdout use.
		SDL2LIB := $(filter-out -mwindows -lSDL2main -lmingw32,$(shell $(SDL2_CONFIG) --static-libs))
		SDL2INC := $(filter-out -Dmain=SDL_main,$(shell $(SDL2_CONFIG) --cflags)) -DSDL_MAIN_HANDLED
	else ifeq ($(MESENOS),windows)
$(error TARGET=windows needs SDL2_PREFIX for the game window, or HEADLESS=true)
	else ifeq ($(MESENOS),osx)
$(error macOS links SDL2 statically: build it with tools/build-sdl2-static.sh <prefix>, then make SDL2_PREFIX=<prefix>)
	else
		SDL2_CONFIG ?= sdl2-config
		SDL2LIB := $(shell $(SDL2_CONFIG) --libs)
		SDL2INC := $(shell $(SDL2_CONFIG) --cflags)
	endif
endif

DEBUG ?= 0

ifeq ($(DEBUG),0)
	MESENFLAGS += -O3
	ifneq ($(LTO),false)
		MESENFLAGS += -DHAVE_LTO
		ifneq ($(USE_GCC),true)
			MESENFLAGS += -flto=thin
		else
			MESENFLAGS += -flto=auto
		endif
	endif
else
	MESENFLAGS += -O0 -g
	# Note: if compiling with a sanitizer, you will likely need to `LD_PRELOAD` the library `libMesenCore.so` will be linked against.
	ifneq ($(SANITIZER),)
		ifeq ($(SANITIZER),address)
			# Currently, `-fsanitize=address` is not supported together with `-fsanitize=thread`
			MESENFLAGS += -fsanitize=address
		else ifeq ($(SANITIZER),thread)
			# Currently, `-fsanitize=address` is not supported together with `-fsanitize=thread`
			MESENFLAGS += -fsanitize=thread
		else
$(warning Unrecognised $$(SANITIZER) value: $(SANITIZER))
		endif
		# `-Wl,-z,defs` is incompatible with the sanitizers in a shared lib, unless the sanitizer libs are linked dynamically; hence `-shared-libsan` (not the default for Clang).
		# It seems impossible to link dynamically against two sanitizers at the same time, but that might be a Clang limitation.
		ifneq ($(USE_GCC),true)
			MESENFLAGS += -shared-libsan
		endif
	endif
endif

ifeq ($(PGO),profile)
	MESENFLAGS += ${PROFILE_GEN_FLAG}
endif

ifeq ($(PGO),optimize)
	MESENFLAGS += ${PROFILE_USE_FLAG}
endif

ifneq ($(STATICLINK),false)
	LINKOPTIONS += -static-libgcc -static-libstdc++ 
endif

ifeq ($(MESENOS),windows)
	# One .exe, without the MinGW runtime DLLs (libwinpthread among them).
	LINKOPTIONS += -static
	WINLIBS := -lwinmm
	ifeq ($(DEBUG),0)
		# SDL's static library carries ~10 MB of DWARF debug info.
		LINKOPTIONS += -Wl,--strip-debug
	endif
endif

# -MMD -MP: each object's headers go in a .d beside it, so changing a header
# rebuilds what includes it.
DEPFLAGS := -MMD -MP
CXXFLAGS = $(PICFLAG) -Wall --std=c++17 $(MESENFLAGS) $(SDL2INC) -I $(realpath ./) -I $(realpath ./Core) -I $(realpath ./Utilities) -I $(realpath ./Sdl) -I $(realpath ./Linux) -I $(realpath ./MacOS) -I $(realpath ./GDB)
OBJCXXFLAGS = $(CXXFLAGS)
CFLAGS = $(PICFLAG) -Wall $(MESENFLAGS)

# Objects per platform, so a cross build never links the host's.
OBJFOLDER := obj.$(MESENPLATFORM)
DEBUGFOLDER := bin/$(MESENPLATFORM)/Debug
RELEASEFOLDER := bin/$(MESENPLATFORM)/Release
ifeq ($(DEBUG), 0)
	OUTFOLDER = $(RELEASEFOLDER)
else
	OUTFOLDER = $(DEBUGFOLDER)
endif



# The object file for each source: obj.<platform>/<source path>.o
objs = $(addprefix $(OBJFOLDER)/,$(addsuffix .o,$(basename $(1))))

CORESRC := $(shell find Core -name '*.cpp')
COREOBJ := $(call objs,$(CORESRC))

UTILSRC := $(shell find Utilities -name '*.cpp' -o -name '*.c')
UTILOBJ := $(call objs,$(UTILSRC))

ifneq ($(HEADLESS),true)
	SDLSRC := $(shell find Sdl -name '*.cpp')
endif
SDLOBJ := $(call objs,$(SDLSRC))

ifeq ($(MESENOS)-$(HEADLESS),linux-false)
	LINUXSRC := $(shell find Linux -name '*.cpp')
else
	LINUXSRC :=
endif
LINUXOBJ := $(call objs,$(LINUXSRC))

# MacOS/ (upstream's keyboard and gamepad code) is not built: nothing uses it.
MACOSOBJ :=

ifeq ($(SYSTEM_LIBEVDEV), true)
	LIBEVDEVLIB := $(shell pkg-config --libs libevdev)
	LIBEVDEVINC := $(shell pkg-config --cflags libevdev)
else
	LIBEVDEVSRC := $(shell find Linux/libevdev -name '*.c')
	LIBEVDEVOBJ := $(call objs,$(LIBEVDEVSRC))
	LIBEVDEVINC := -I../
endif

ifeq ($(MESENOS)-$(HEADLESS),linux-false)
	X11LIB := -lX11
else
	X11LIB :=
endif

FSLIB := -lstdc++fs

GDBSRC := $(filter-out GDB/DapMain.cpp, $(filter-out GDB/test_%.cpp, $(shell find GDB -name '*.cpp')))
GDBOBJ := $(call objs,$(GDBSRC))

GDBMAINSRC := GDB/DapMain.cpp
# Only DapMain differs in a headless build: its own object, so switching
# HEADLESS never links a stale one.
ifeq ($(HEADLESS),true)
	GDBMAINOBJ := $(OBJFOLDER)/GDB/DapMain.headless.o
else
	GDBMAINOBJ := $(call objs,$(GDBMAINSRC))
endif

GDBBIN := mesen-gdb$(EXE)

ifneq ($(MESENOS),linux)
	LIBEVDEVOBJ :=
	LIBEVDEVINC :=
	LIBEVDEVSRC :=
	FSLIB :=
endif

ifeq ($(HEADLESS),true)
	LIBEVDEVOBJ :=
	LIBEVDEVLIB :=
endif

# Records the last HEADLESS value, so switching it relinks: the link's
# inputs change, but not their dates.
BUILDKIND := $(OUTFOLDER)/.headless

all: $(OUTFOLDER)/$(GDBBIN)

$(OBJFOLDER)/%.o: %.c
	@mkdir -p $(@D)
	$(CC) $(CFLAGS) $(DEPFLAGS) -c $< -o $@

$(OBJFOLDER)/%.o: %.cpp
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) $(DEPFLAGS) -c $< -o $@

$(OBJFOLDER)/%.o: %.mm
	@mkdir -p $(@D)
	$(CXX) $(OBJCXXFLAGS) $(DEPFLAGS) -c $< -o $@

$(OBJFOLDER)/GDB/DapMain.headless.o: GDB/DapMain.cpp
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) $(DEPFLAGS) -DMESEN_HEADLESS -c $< -o $@

-include $(shell find $(OBJFOLDER) -name '*.d' 2>/dev/null)

$(BUILDKIND): FORCE
	@mkdir -p $(OUTFOLDER)
	@echo $(HEADLESS) | cmp -s - $@ || echo $(HEADLESS) > $@

.PHONY: FORCE

$(OUTFOLDER)/$(GDBBIN): $(GDBMAINOBJ) $(GDBOBJ) $(UTILOBJ) $(COREOBJ) $(SDLOBJ) $(LIBEVDEVOBJ) $(LINUXOBJ) $(MACOSOBJ) $(BUILDKIND)
	mkdir -p $(OUTFOLDER)
	$(CXX) $(CXXFLAGS) $(LINKOPTIONS) -o $@ $(GDBMAINOBJ) $(GDBOBJ) $(LINUXOBJ) $(MACOSOBJ) $(LIBEVDEVOBJ) $(UTILOBJ) $(SDLOBJ) $(COREOBJ) -pthread $(FSLIB) $(SDL2LIB) $(LIBEVDEVLIB) $(X11LIB) $(WINLIBS)

# Unit tests — only links DAP .cpp files (no emulator core needed)
TESTSRC := GDB/test_main.cpp GDB/test_dap_json.cpp GDB/test_dbg_parser.cpp GDB/test_dap_protocol.cpp \
           GDB/test_cpu_serializer.cpp GDB/test_cli_symbols.cpp
TESTOBJ := $(call objs,$(TESTSRC))
DAPTESTOBJ := $(call objs,Core/Debugger/DAP/DapJson.cpp Core/Debugger/DAP/DapMessageReader.cpp \
              Core/Debugger/DAP/DapMessageWriter.cpp Core/Debugger/DAP/DbgFileParser.cpp \
              Core/Debugger/DAP/SourceMapper.cpp Core/Debugger/DAP/CpuStateSerializer.cpp \
              Utilities/SimpleLock.cpp Utilities/Timer.cpp GDB/cli_symbols.cpp)

test: $(TESTOBJ) $(DAPTESTOBJ)
	$(CXX) $(CXXFLAGS) $(LINKOPTIONS) -o bin/dap-test$(EXE) $(TESTOBJ) $(DAPTESTOBJ) -pthread $(FSLIB) $(WINLIBS)
	$(TESTRUNNER) bin/dap-test$(EXE)

clean:
	rm -r -f $(OBJFOLDER)
	rm -r -f $(OUTFOLDER)/$(GDBBIN) $(BUILDKIND) bin/dap-test$(EXE)
	# Objects from before obj.<platform>, built beside their sources
	find Core Utilities Sdl Linux MacOS GDB -name '*.o' -delete
