#Both clang & gcc work fine - clang seems to output faster code
#The emulation core requires SDL2.
#Run "make" to build

MESENFLAGS=

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

SDL2LIB := $(shell sdl2-config --libs)
SDL2INC := $(shell sdl2-config --cflags)

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

ifeq ($(MESENOS),osx)
	LINKOPTIONS += -framework Foundation -framework Cocoa -framework GameController -framework CoreHaptics -Wl,-rpath,/opt/local/lib
endif

CXXFLAGS = -fPIC -Wall --std=c++17 $(MESENFLAGS) $(SDL2INC) -I $(realpath ./) -I $(realpath ./Core) -I $(realpath ./Utilities) -I $(realpath ./Sdl) -I $(realpath ./Linux) -I $(realpath ./MacOS) -I $(realpath ./GDB)
OBJCXXFLAGS = $(CXXFLAGS)
CFLAGS = -fPIC -Wall $(MESENFLAGS)

OBJFOLDER := obj.$(MESENPLATFORM)
DEBUGFOLDER := bin/$(MESENPLATFORM)/Debug
RELEASEFOLDER := bin/$(MESENPLATFORM)/Release
ifeq ($(DEBUG), 0)
	OUTFOLDER = $(RELEASEFOLDER)
else
	OUTFOLDER = $(DEBUGFOLDER)
endif



CORESRC := $(shell find Core -name '*.cpp')
COREOBJ := $(CORESRC:.cpp=.o)

UTILSRC := $(shell find Utilities -name '*.cpp' -o -name '*.c')
UTILOBJ := $(addsuffix .o,$(basename $(UTILSRC)))

SDLSRC := $(shell find Sdl -name '*.cpp')
SDLOBJ := $(SDLSRC:.cpp=.o)

ifeq ($(MESENOS),linux)
	LINUXSRC := $(shell find Linux -name '*.cpp')
else
	LINUXSRC :=
endif
LINUXOBJ := $(LINUXSRC:.cpp=.o)

ifeq ($(MESENOS),osx)
	MACOSSRC := $(shell find MacOS -name '*.mm')
else
	MACOSSRC :=
endif
MACOSOBJ := $(MACOSSRC:.mm=.o)

ifeq ($(SYSTEM_LIBEVDEV), true)
	LIBEVDEVLIB := $(shell pkg-config --libs libevdev)
	LIBEVDEVINC := $(shell pkg-config --cflags libevdev)
else
	LIBEVDEVSRC := $(shell find Linux/libevdev -name '*.c')
	LIBEVDEVOBJ := $(LIBEVDEVSRC:.c=.o)
	LIBEVDEVINC := -I../
endif

ifeq ($(MESENOS),linux)
	X11LIB := -lX11
else
	X11LIB :=
endif

FSLIB := -lstdc++fs

GDBSRC := $(filter-out GDB/DapMain.cpp, $(shell find GDB -name '*.cpp'))
GDBOBJ := $(GDBSRC:.cpp=.o)

GDBMAINSRC := GDB/DapMain.cpp
GDBMAINOBJ := $(GDBMAINSRC:.cpp=.o)

GDBBIN := mesen-gdb

ifeq ($(MESENOS),osx)
	LIBEVDEVOBJ :=
	LIBEVDEVINC :=
	LIBEVDEVSRC :=
	FSLIB :=
endif

all: $(OUTFOLDER)/$(GDBBIN)

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

%.o: %.cpp
	$(CXX) $(CXXFLAGS) -c $< -o $@

%.o: %.mm
	$(CXX) $(OBJCXXFLAGS) -c $< -o $@

$(OUTFOLDER)/$(GDBBIN): $(GDBMAINOBJ) $(GDBOBJ) $(UTILOBJ) $(COREOBJ) $(SDLOBJ) $(LIBEVDEVOBJ) $(LINUXOBJ) $(MACOSOBJ)
	mkdir -p $(OUTFOLDER)
	$(CXX) $(CXXFLAGS) $(LINKOPTIONS) -o $@ $(GDBMAINOBJ) $(GDBOBJ) $(LINUXOBJ) $(MACOSOBJ) $(LIBEVDEVOBJ) $(UTILOBJ) $(SDLOBJ) $(COREOBJ) -pthread $(FSLIB) $(SDL2LIB) $(LIBEVDEVLIB) $(X11LIB)

clean:
	rm -r -f $(COREOBJ)
	rm -r -f $(UTILOBJ)
	rm -r -f $(LINUXOBJ) $(LIBEVDEVOBJ)
	rm -r -f $(SDLOBJ)
	rm -r -f $(MACOSOBJ)
	rm -r -f $(GDBOBJ) $(GDBMAINOBJ)
	rm -r -f $(OUTFOLDER)/$(GDBBIN)
