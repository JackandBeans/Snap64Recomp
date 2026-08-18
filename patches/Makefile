# Builds the game-side patches into an ELF for N64Recomp to recompile.
#
# The result is linked ahead of the recompiled game, so a function defined
# here replaces the game's own without the ROM changing. Run from this
# directory with DECOMP pointing at the decompilation:
#
#   make DECOMP=$HOME/pokemonsnap

DECOMP ?= $(HOME)/pokemonsnap
CROSS  ?= mips-linux-gnu-

# The game is built with IDO, and the decomp ships it. Using the same
# compiler keeps the patch's ABI and codegen identical to the code it
# replaces, and avoids needing a cross gcc that is not installed.
CC := $(DECOMP)/tools/ido7.1/cc
LD := $(CROSS)ld

TARGET  := build/patches.elf
SOURCES := $(wildcard src/*.c)
OBJECTS := $(SOURCES:src/%.c=build/%.o)

# Mirrors the decomp's own compile line, minus its output paths.
CFLAGS := -G 0 -non_shared -Xcpluscomm -nostdinc -Wab,-r4300_mul -O2 -mips2           -DF3DEX_GBI_2 -D_LANGUAGE_C -DNDEBUG -D_FINALROM           -DBUILD_VERSION=VERSION_I           -I $(DECOMP)/include -I $(DECOMP)/src -I $(DECOMP)/ultralib/include           -I $(DECOMP)/ultralib/include/ido -I $(DECOMP)/ultralib/include/PR           -I $(DECOMP)/ultralib/src -I $(DECOMP)/build/include           -I $(DECOMP)/build -I $(DECOMP)

# Calls into the game stay unresolved on purpose: --emit-relocs then keeps a
# relocation on each one, which is how the recompiler matches them to the
# game's functions. Data addresses come from game_syms.ld, since those are
# fixed locations rather than symbols to be matched.
LDFLAGS := -T game_syms.ld -T patch.ld --emit-relocs --no-check-sections            -nostdlib --unresolved-symbols=ignore-all --noinhibit-exec

all: $(TARGET)

$(TARGET): $(OBJECTS) patch.ld game_syms.ld
	@mkdir -p $(dir $@)
	$(LD) $(LDFLAGS) -o $@ $(OBJECTS)

build/%.o: src/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c -o $@ $<

clean:
	rm -rf build

.PHONY: all clean
