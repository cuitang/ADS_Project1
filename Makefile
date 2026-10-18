.DEFAULT_GOAL := main

CC := gcc

CPPFLAGS := -Iincludes
CFLAGS := -std=c11 -O2 -Wall -Wextra -Wpedantic \
          -MMD -MP \
          -Wno-parentheses -Wno-misleading-indentation
LDFLAGS :=
LDLIBS :=

BUILD_DIR := build
OBJ_DIR := $(BUILD_DIR)/obj

ifeq ($(OS),Windows_NT)
    EXE := .exe
    MKDIR_P = if not exist "$(subst /,\,$1)" mkdir "$(subst /,\,$1)"
    RM_DIR = if exist "$(subst /,\,$1)" rmdir /S /Q "$(subst /,\,$1)"
else
    EXE :=
    MKDIR_P = mkdir -p "$1"
    RM_DIR = rm -rf "$1"
endif

TARGET := $(BUILD_DIR)/search$(EXE)

SOURCES := \
    src/main.c \
    src/search/dict.c \
    src/search/text.c \
    src/search/query.c \
    src/index/AdsIndex.c \
    src/preprocess/AdsPreprocess.c \
    third_party/porter/porter.c

OBJECTS := $(patsubst %.c,$(OBJ_DIR)/%.o,$(SOURCES))
DEPS := $(OBJECTS:.o=.d)

.PHONY: main clean

main: $(TARGET)
	@echo Built $(TARGET)

$(TARGET): $(OBJECTS)
	@$(call MKDIR_P,$(@D))
	$(CC) $(LDFLAGS) -o $@ $^ $(LDLIBS)

$(OBJ_DIR)/%.o: %.c
	@$(call MKDIR_P,$(@D))
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@

-include $(DEPS)

clean:
	@$(call RM_DIR,$(BUILD_DIR))