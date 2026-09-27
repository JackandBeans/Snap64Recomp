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
# The ELF's loadable bytes, code and data, from the link address up: CMake
# embeds them in the executable and librecomp copies them into memory at
# start-up, which is how a patch's constants become readable (patch.ld).
BINARY  := build/patches.bin
SOURCES := $(wildcard src/*.c)
OBJECTS := $(SOURCES:src/%.c=build/%.o)

# Mirrors the decomp's own compile line, minus its output paths.
CFLAGS := -G 0 -non_shared -Xcpluscomm -nostdinc -Wab,-r4300_mul -O2 -mips2           -DF3DEX_GBI_2 -D_LANGUAGE_C -DNDEBUG -D_FINALROM           -DBUILD_VERSION=VERSION_I           -I $(DECOMP)/include -I $(DECOMP)/src -I $(DECOMP)/ultralib/include           -I $(DECOMP)/ultralib/include/ido -I $(DECOMP)/ultralib/include/PR           -I $(DECOMP)/ultralib/src -I $(DECOMP)/build/include           -I $(DECOMP)/build -I $(DECOMP)

# Calls into the game stay unresolved on purpose: --emit-relocs then keeps a
# relocation on each one, which is how the recompiler matches them to the
# game's functions. Data addresses come from game_syms.ld, since those are
# fixed locations rather than symbols to be matched.
LDFLAGS := -T game_syms.ld -T patch.ld --emit-relocs --no-check-sections            -nostdlib --unresolved-symbols=ignore-all --noinhibit-exec

all: $(TARGET) $(BINARY)

$(TARGET): $(OBJECTS) patch.ld game_syms.ld
	@mkdir -p $(dir $@)
	$(LD) $(LDFLAGS) -o $@ $(OBJECTS)

# The bytes come from a second link of the same objects with every game
# function defined at its address. In the first link a call into the game,
# or the address of a game function, is left as zero for the recompiler to
# match; the runtime reads these bytes when a mod hooks a function the
# patches replace, and recompiles it live from them, so there they must be
# the real addresses (tools/gen_patch_funcs_ld.py). The two links must lay
# out the same: the check compares every section's size and every patch
# symbol's address.
FUNCS_LD := build/game_funcs.ld
RESOLVED := build/patches_resolved.elf

$(FUNCS_LD): pokemonsnap.syms.toml ../tools/gen_patch_funcs_ld.py
	@mkdir -p $(dir $@)
	python3 ../tools/gen_patch_funcs_ld.py pokemonsnap.syms.toml $@

$(RESOLVED): $(OBJECTS) patch.ld game_syms.ld $(FUNCS_LD) $(TARGET)
	$(LD) -T game_syms.ld -T $(FUNCS_LD) -T patch.ld --no-check-sections -nostdlib --noinhibit-exec -o $@ $(OBJECTS)
	$(CROSS)size -A $(TARGET) | tail -n +3 > build/layout_a.txt
	$(CROSS)size -A $@ | tail -n +3 > build/layout_b.txt
	$(CROSS)nm $(TARGET) | grep -v " [AUw] " | sort > build/syms_a.txt
	$(CROSS)nm $@ | grep -v " [AUw] " | sort > build/syms_b.txt
	cmp build/layout_a.txt build/layout_b.txt
	cmp build/syms_a.txt build/syms_b.txt

$(BINARY): $(RESOLVED)
	$(CROSS)objcopy -O binary $< $@

# The .inc files are included by the page sources (pause_menu_patch.inc and
# anywhere_patch.inc by graphics_menu_patch.c): an edit to one of them must
# rebuild the object too, or the executable carries the old code.
build/%.o: src/%.c $(wildcard src/*.inc)
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c -o $@ $<

clean:
	rm -rf build

.PHONY: all clean
