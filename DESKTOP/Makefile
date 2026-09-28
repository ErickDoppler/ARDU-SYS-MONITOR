# ---------------------------------------------------------------------------
#  Multi Screen System Monitor -- desktop agent
#
#  Plain GNU make, so the only build dependency is the compiler. w64devkit
#  already ships make, which means a from-scratch setup needs one download
#  instead of three (CMakeLists.txt is still here for IDE users, but nothing in
#  the bootstrap path depends on CMake or Ninja).
#
#    make            build build/sysmon.exe
#    make selftest   build the console sensor harness
#    make run        build and launch (Windows will prompt for elevation)
#    make clean
#
#  Cross-compiling from Linux works with CXX=x86_64-w64-mingw32-g++.
# ---------------------------------------------------------------------------

CXX      ?= g++
WINDRES  ?= windres
BUILD    := build

# C++20 for designated initialisers and <optional>; -municode because the entry
# point is wWinMain.
CXXFLAGS := -std=c++20 -O2 -Wall -Wextra -Wpedantic -Isrc \
            -DUNICODE -D_UNICODE \
            -D_WIN32_WINNT=0x0A00 -DWINVER=0x0A00 -DNTDDI_VERSION=0x0A000000

# NTDDI_VERSION is not redundant with _WIN32_WINNT: iphlpapi.h gates its include
# of netioapi.h on NTDDI, and without it GetIfTable2 silently does not exist.

LDFLAGS  := -municode -mwindows
LDLIBS   := -lpdh -lpsapi -liphlpapi -lsetupapi -lcomctl32 -lgdi32 -lshell32 \
            -lole32 -loleaut32 -luuid -ltaskschd

SRCS := $(wildcard src/*.cpp) $(wildcard src/sensors/*.cpp)
OBJS := $(patsubst %.cpp,$(BUILD)/%.o,$(SRCS))
RES  := $(BUILD)/app.res.o

TARGET   := $(BUILD)/sysmon.exe
SELFTEST := $(BUILD)/selftest.exe
ICON     := resources/appicon.ico
MKICON   := $(BUILD)/mkicon.exe

.PHONY: all clean run selftest icon

all: $(TARGET)

$(TARGET): $(OBJS) $(RES)
	@echo "  LINK  $@"
	@$(CXX) $(LDFLAGS) -o $@ $^ $(LDLIBS)
	@echo "Built $@"

$(BUILD)/%.o: %.cpp
	@mkdir -p $(dir $@)
	@echo "  CXX   $<"
	@$(CXX) $(CXXFLAGS) -c $< -o $@

# app.rc embeds appicon.ico, so the icon has to exist before the resource is
# compiled -- hence the order-only prerequisite.
$(RES): resources/app.rc | $(ICON)
	@mkdir -p $(dir $@)
	@echo "  RC    $<"
	@$(WINDRES) -I. $< -o $@

# The icon is generated rather than committed as binary art: the executable
# icon, the window icon and the tray icon are then provably the same drawing.
# mkicon links only IconFactory, so it needs no resource of its own and cannot
# depend on the thing it produces.
icon: $(ICON)

$(ICON): src/IconFactory.cpp tools/mkicon.cpp
	@mkdir -p $(BUILD) resources
	@echo "  ICON  $@"
	@$(CXX) -std=c++20 -O2 -municode -Isrc tools/mkicon.cpp src/IconFactory.cpp \
		-o $(MKICON) -lgdi32 -luser32
	@$(MKICON) $@

selftest: $(SELFTEST)

# Console subsystem: it prints sensor readings and the exact bytes the display
# would receive, so it must not be -mwindows.
$(SELFTEST): tools/selftest.cpp src/Protocol.cpp src/SerialPort.cpp $(wildcard src/sensors/*.cpp)
	@mkdir -p $(BUILD)
	@echo "  CXX   $@"
	@$(CXX) $(CXXFLAGS) -o $@ $^ -lpdh -lpsapi -liphlpapi -lsetupapi -lole32 -luuid
	@echo "Built $@"

run: $(TARGET)
	@echo "Launching (Windows will prompt for elevation)"
	@cmd //c start "" "$(subst /,\,$(TARGET))"

clean:
	@rm -rf $(BUILD)
	@echo "Cleaned. ($(ICON) kept -- delete it by hand to force a redraw.)"
