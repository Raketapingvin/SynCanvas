assembly_source_files := $(wildcard src/impl/x86_64/boot/*.asm)
assembly_object_files := $(patsubst src/impl/x86_64/boot/%.asm, build/x86_64/boot/%.o, $(assembly_source_files))

c_source_files := $(shell find src/impl/kernel -name "*.c")
c_object_files := $(patsubst src/impl/kernel/%.c, build/kernel/%.o, $(c_source_files))

x86_64_object_files := $(assembly_object_files) $(c_object_files)

build/x86_64/boot/%.o: src/impl/x86_64/boot/%.asm
	mkdir -p $(dir $@)
	nasm -f elf64 $(patsubst build/x86_64/boot/%.o, src/impl/x86_64/boot/%.asm, $@) -o $@

build/kernel/%.o: src/impl/kernel/%.c
	mkdir -p $(dir $@)
	gcc -c -I src/intf -ffreestanding -mcmodel=large -mno-red-zone -m64 -fno-builtin -fno-stack-protector -nostdlib -nodefaultlibs $(patsubst build/kernel/%.o, src/impl/kernel/%.c, $@) -o $@


.PHONY: build-x86_64
build-x86_64: $(x86_64_object_files)
	mkdir -p dist/x86_64
	ld -n -o dist/x86_64/kernel.bin -T targets/x86_64/linker.ld $(x86_64_object_files)
	cp dist/x86_64/kernel.bin targets/x86_64/iso/boot/kernel.bin
	grub-mkrescue /usr/lib/grub/i386-pc -o dist/x86_64/kernel.iso targets/x86_64/iso

.PHONY: run
run:
	qemu-system-x86_64 -cdrom dist/x86_64/kernel.iso -device rtl8139 -audiodev pa,id=spk -machine pcspk-audiodev=spk -device piix3-usb-uhci -drive file=disk.img,format=raw,index=0,media=disk
