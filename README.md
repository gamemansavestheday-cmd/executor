# executor

paste ai slop. watch it build real shit.
executor is a minimal, extremely fast blueprint runner written in C.  
you feed it a text file full of commands that an AI generated, and it just fucking does it — creates folders, writes files, runs compilers, assembles bootloaders, pads sectors, launches qemu, whatever degenerate thing you asked for.

# what it actually does
- creates project structures
- writes source files (text or binary)
- runs any shell command (gcc, nasm, pip, npm, qemu, etc.)
- checks for required tools and tells you how to install them
- pads bootsectors with the proper AA55 signature
- concatenates binaries (bootloader + kernel = fun)
- dies immediately if anything goes wrong (the way god intended)

# how to use

1. save the AI output as `myproject.txt`
2. run it:

./executor myproject.txt

how to compile 
linux:

`gcc -o executor src/executor.c -Wall -Wextra -O2 -std=c99`

`gcc -o executor src/executor.c -Wall -Wextra -O3 -std=c99 -march=native`

after build just do chmod +x executor
`./executor myblueprint.txt`

windows (for the poor dumbfucks)
you need mingw-w64 installed first
run mingw with this command `gcc -o executor.exe src/executor.c -Wall -Wextra -O2 -std=c99` to compile
alternative if you only have regular cmd/powershell and winget:
`winget install mingw`

`gcc -o executor.exe src/executor.c -Wall -Wextra -O2 -std=c99`
Then run it with:
`executor.exe myblueprint.txt`

ai guide (tell your retarded ai this:)

YOUR ENTIRE OUTPUT MUST BE RAW TEXT COMMANDS ONLY. NO MARKDOWN. NO EXPLANATIONS.
COMMANDS (use | as separator):
INSTALL_DEPS|manager|package1|package2|package3|...

Examples (use the right one for your OS):

Debian/Ubuntu/Mint
INSTALL_DEPS|apt|gcc|make|qemu-system-x86_64|nasm

Fedora/RHEL
INSTALL_DEPS|dnf|gcc|make|qemu-system-x86_64|nasm

Arch/Manjaro
INSTALL_DEPS|pacman|gcc|make|qemu|nasmd

Void Linux
INSTALL_DEPS|xbps|gcc|make|qemu|nasmd

openSUSE/SUSE
INSTALL_DEPS|zypper|gcc|make|qemu|nasmd

Python packages (works on any OS)
INSTALL_DEPS|pip|flask|requests|numpy

npm global packages
INSTALL_DEPS|npm|typescript|vite

REQUIRE_TOOL|toolname
CREATE_FOLDER|foldername
SET_CWD|newfolder
CREATE_FILE|path/to/file.txt
[then paste the entire file content here]
END_FILE
RUN_COMMAND|command|arg1|arg2|...
PAD_BOOTSECTOR|path/to/boot.bin
CONCATENATE_FILES|output.bin|file1.bin|file2.bin

Example bootloader blueprint:

REQUIRE_TOOL|nasm
REQUIRE_TOOL|qemu-system-x86_64
CREATE_FOLDER|my_bootloader
SET_CWD|my_bootloader
CREATE_FILE|boot.asm
org 0x7c00
bits 16
start:
    mov ah, 0x0e
    mov al, 'B'
    int 0x10
    jmp $
END_FILE
RUN_COMMAND|nasm|-f|bin|boot.asm|-o|boot.bin
PAD_BOOTSECTOR|boot.bin
RUN_COMMAND|qemu-system-x86_64|boot.bin

RULES FOR AI:
- ALWAYS put INSTALL_DEPS in STAGE 1 (pre-flight)
- use the correct manager for the user's OS (if you dont know just use pip or apt)
- never put sudo in the command, the executor handles that
- for windows just use winget or pip, the core will handle it
- you can mix multiple INSTALL_DEPS lines, it will run them one by one
- if the AI (you) fucks up the manager name the executor will call you a degenerate and die
- code everything fully do not truncate, omit, or simplify anything so dont be a dumbass and be lazy and write short code that doesnt add everything and has a placeholder do not do this
