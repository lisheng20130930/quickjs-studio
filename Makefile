CC ?= cc
CFLAGS ?= -O0 -g3
CFLAGS += -std=c11 -Wall -Wextra -Werror

MINGW_CC ?= x86_64-w64-mingw32-gcc
MINGW_CXX ?= x86_64-w64-mingw32-g++-posix
MINGW_AR ?= x86_64-w64-mingw32-ar

QJS_DIR := third_party/quickjs
QJS_LIB := $(QJS_DIR)/libquickjs.a
QJS_SOURCES := $(QJS_DIR)/quickjs.c $(QJS_DIR)/quickjs-debugger.c \
	$(QJS_DIR)/quickjs-debugger-internal.inc $(QJS_DIR)/quickjs-debugger.h
HOST_SOURCES := examples/host.c agent/qjs_debug_agent.c
WINDOWS_DIR := build/windows/bin
WINDOWS_QJS_LIB := $(QJS_DIR)/libquickjs_win64.a
WINDOWS_STUDIO_SOURCES := src/main.c src/studio_ui.c src/studio_navigation.c \
	src/studio_workspace.c src/studio_debug.c src/editor.c src/project.c src/symbol_search.c \
	src/debug_session.c src/input_dialog.c src/project_settings.c third_party/cjson/cJSON.c
WINDOWS_STUDIO_HEADERS := src/editor.h src/project.h src/symbol_search.h src/debug_session.h \
	src/input_dialog.h src/project_settings.h src/studio_internal.h
WINDOWS_INCLUDES := -Isrc -Ithird_party/scintilla/include \
	-Ithird_party/lexilla/include -Ithird_party/cjson
WINDOWS_CFLAGS := -std=c11 -O0 -g3 -Wall -Wextra -Werror \
	-D_WIN32_WINNT=0x0601

.PHONY: all test clean windows windows-deps windows-clean

all: $(QJS_LIB) build/tests/test_debugger build/tests/test_symbol_search build/bin/qjs-host

$(QJS_LIB): $(QJS_SOURCES)
	$(MAKE) -C $(QJS_DIR) libquickjs.a

build/tests/test_debugger: tests/test_debugger.c $(QJS_LIB)
	@mkdir -p $(@D)
	$(CC) $(CFLAGS) -isystem $(QJS_DIR) $< $(QJS_LIB) -lm -ldl -lpthread -o $@

build/tests/test_symbol_search: tests/test_symbol_search.c src/symbol_search.c src/symbol_search.h
	@mkdir -p $(@D)
	$(CC) $(CFLAGS) -Isrc tests/test_symbol_search.c src/symbol_search.c -o $@

build/bin/qjs-host: $(HOST_SOURCES) agent/qjs_debug_agent.h $(QJS_LIB)
	@mkdir -p $(@D)
	$(CC) $(CFLAGS) -Iagent -isystem $(QJS_DIR) $(HOST_SOURCES) \
		$(QJS_LIB) -lm -ldl -lpthread -o $@

test: all
	./build/tests/test_debugger
	./build/tests/test_symbol_search

windows: $(WINDOWS_DIR)/quickjs-studio.exe \
	$(WINDOWS_DIR)/qjs-host.exe \
	$(WINDOWS_DIR)/Scintilla.dll $(WINDOWS_DIR)/lexilla.dll
	@echo "QuickJS Studio Windows build completed"
	@echo "IDE: $(abspath $(WINDOWS_DIR)/quickjs-studio.exe)"
	@echo "Example host: $(abspath $(WINDOWS_DIR)/qjs-host.exe)"

windows-deps: third_party/scintilla/bin/Scintilla.dll \
	third_party/lexilla/bin/lexilla.dll

$(WINDOWS_QJS_LIB): $(QJS_SOURCES)
	MINGW_CC=$(MINGW_CC) MINGW_AR=$(MINGW_AR) \
		python3 $(QJS_DIR)/build_quickjs.py

third_party/scintilla/bin/Scintilla.dll:
	$(MAKE) -C third_party/scintilla/win32 windir=Windows \
		CXX=$(MINGW_CXX) WINDRES=x86_64-w64-mingw32-windres

third_party/lexilla/bin/lexilla.dll:
	$(MAKE) -C third_party/lexilla/src windir=Windows \
		CXX=$(MINGW_CXX) WINDRES=x86_64-w64-mingw32-windres \
		LDFLAGS="-mwindows -Wl,--kill-at -shared -static"

$(WINDOWS_DIR)/quickjs-studio.exe: $(WINDOWS_STUDIO_SOURCES) \
	$(WINDOWS_STUDIO_HEADERS) | $(WINDOWS_DIR)
	$(MINGW_CC) $(WINDOWS_CFLAGS) $(WINDOWS_INCLUDES) \
		$(WINDOWS_STUDIO_SOURCES) -mwindows -lcomctl32 -lcomdlg32 \
		-lshell32 -lole32 -lgdi32 -luser32 -o $@

$(WINDOWS_DIR)/qjs-host.exe: $(HOST_SOURCES) agent/qjs_debug_agent.h \
	$(WINDOWS_QJS_LIB) | $(WINDOWS_DIR)
	$(MINGW_CC) $(WINDOWS_CFLAGS) -Iagent -isystem $(QJS_DIR) \
		$(HOST_SOURCES) $(WINDOWS_QJS_LIB) -static -lm -lwinpthread -o $@

$(WINDOWS_DIR)/Scintilla.dll: third_party/scintilla/bin/Scintilla.dll | $(WINDOWS_DIR)
	cp $< $@

$(WINDOWS_DIR)/lexilla.dll: third_party/lexilla/bin/lexilla.dll | $(WINDOWS_DIR)
	cp $< $@

$(WINDOWS_DIR):
	mkdir -p $@

windows-clean:
	rm -rf build/windows
	rm -rf $(QJS_DIR)/obj_win64 $(WINDOWS_QJS_LIB)

clean:
	$(MAKE) -C $(QJS_DIR) clean
	rm -rf build
