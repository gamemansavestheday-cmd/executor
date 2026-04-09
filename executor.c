/*
 * executor - 0.1.1 (the version that doesn't suck)
 * 
 * i have no idea what half this code does but it works on my machine so fuck you
 * rewrote because Python was slow as shit and the old sandbox was a security incident waiting to happen
 * now the blueprint is a braindead text DSL. AI spits commands, we execute them. No more exec() russian roulette.
 * Safer, faster, and i actually know what the fuck is happening under the hood (most of the time)
 * PLEASE ONLY FUCKING COMPILE WITH gcc -o executor executor.c -Wall -Wextra -O3 -std=gnu99 -march=native -Wno-format-truncation
 */

#define _GNU_SOURCE
#define _POSIX_C_SOURCE 200809L

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <errno.h>
#include <stdbool.h>

#ifdef _WIN32
    #include <direct.h>
    #include <windows.h>
    #define mkdir _mkdir
    #define PATH_SEP '\\'
    #define sleep_ms(x) Sleep(x)
#else
    #include <unistd.h>
    #define mkdir(path) mkdir(path, 0755)
    #define PATH_SEP '/'
    #define sleep_ms(x) sleep((x)/1000)
#endif
static void set_cwd(const char *new_cwd);
static int current_indent_level = 0;
static void create_project_folder(const char *name);
static void create_file(const char *path, const char *content, size_t content_len);
static void concatenate_files(const char *output, const char *file1, const char *file2);
static void pad_bootsector(const char *path); // FORWARD DECLARATIONS please do not remove these so the fucking compiler doesnt implode and touch us all if the compiler
// doesnt know these functions exist itll butt booty molest us

#define MAX_LINE 4096
#define MAX_CONTENT 1048576  // 1MB should be enough for your AI slop
#define MAX_CMD 8192

// um global current working directory for the blueprint steps (starts as ".")
static char current_cwd[1024] = ".";

// ---------------------------------------------------------------------------
// TOOL CHECKING (require_tool)
// ---------------------------------------------------------------------------
static bool tool_exists(const char *tool) {
    char cmd[1024];
#ifdef _WIN32
    // windows is retarded so we use where.exe
    snprintf(cmd, sizeof(cmd), "where \"%s\" >nul 2>&1", tool);
    if (system(cmd) == 0) return true;
    snprintf(cmd, sizeof(cmd), "where \"%s.exe\" >nul 2>&1", tool);
#else
    snprintf(cmd, sizeof(cmd), "command -v \"%s\" >/dev/null 2>&1", tool);
#endif
    return system(cmd) == 0;
}

static void require_tool(const char *tool) {
    printf("--- [REQUIRE_TOOL] Checking for %s...\n", tool);
    if (!tool_exists(tool)) {
        printf("TOOL MISSING: %s\n", tool);
        printf("   Install command (copy paste this you degenerate):\n");
#ifdef _WIN32
        printf("   winget install %s   or   choco install %s\n", tool, tool);
#else
        printf("   sudo apt install %s   or   sudo dnf install %s   or   sudo pacman -S %s   or whatever your distro uses\n", tool, tool, tool);
#endif
        exit(1);  // die immediately, no mercy
    }
    printf("Tool '%s' found.\n", tool);
}

// ---------------------------------------------------------------------------
// FILESYSTEM SHIT - fixed the stupid truncation warnings + added set_cwd
// i went full retard on the snprintf because gcc was being a whiny bitch
// ---------------------------------------------------------------------------
static void create_project_folder(const char *name) {
    char fullpath[4096];
    snprintf(fullpath, sizeof(fullpath) - 1, "%s%c%s", current_cwd, PATH_SEP, name);
    fullpath[sizeof(fullpath)-1] = '\0';   // force null termination because gcc has trust issues
    
    if (mkdir(fullpath) == 0 || errno == EEXIST) {
        printf("created folder: %s/\n", name);
    } else {
        printf("failed to create folder %s: %s\n", name, strerror(errno));
        exit(1);
    }
}

static void set_cwd(const char *new_cwd) {
    if (chdir(new_cwd) != 0) {
        printf("failed to chdir to %s: %s\n", new_cwd, strerror(errno));
        exit(1);
    }
    strncpy(current_cwd, new_cwd, sizeof(current_cwd)-1);
    current_cwd[sizeof(current_cwd)-1] = '\0';
    printf("set working directory to: %s\n", current_cwd);
}

static void create_file(const char *path, const char *content, size_t content_len) {
    char fullpath[1024];
    snprintf(fullpath, sizeof(fullpath) - 1, "%s%c%s", current_cwd, PATH_SEP, path);
    fullpath[sizeof(fullpath)-1] = '\0';
    
    FILE *f = fopen(fullpath, "wb");
    if (!f) {
        printf("failed to create file %s: %s\n", path, strerror(errno));
        exit(1);
    }
    if (content_len > 0) {
        fwrite(content, 1, content_len, f); // now binary safe you fucking animal
    }
    fclose(f);
    printf("created file: %s\n", path); 
}

static void concatenate_files(const char *output, const char *file1, const char *file2) {
    // for when you want to slap bootloader + kernel together like a true schizo
    char outpath[1024], p1[1024], p2[1024];
    snprintf(outpath, sizeof(outpath) - 1, "%s%c%s", current_cwd, PATH_SEP, output);
    snprintf(p1, sizeof(p1) - 1, "%s%c%s", current_cwd, PATH_SEP, file1);
    snprintf(p2, sizeof(p2) - 1, "%s%c%s", current_cwd, PATH_SEP, file2);
    
    outpath[sizeof(outpath)-1] = '\0';
    p1[sizeof(p1)-1] = '\0';
    p2[sizeof(p2)-1] = '\0';
    
    FILE *out = fopen(outpath, "wb");
    if (!out) exit(1);
    
    FILE *in = fopen(p1, "rb");
    if (in) { 
        while (!feof(in)) { 
            char b[4096]; 
            size_t r = fread(b,1,sizeof(b),in); 
            fwrite(b,1,r,out); 
        } 
        fclose(in); 
    }
    
    in = fopen(p2, "rb");
    if (in) { 
        while (!feof(in)) { 
            char b[4096]; 
            size_t r = fread(b,1,sizeof(b),in); 
            fwrite(b,1,r,out); 
        } 
        fclose(in); 
    }
    
    fclose(out);
    printf("concatenated %s + %s -> %s\n", file1, file2, output);
}

// ---------------------------------------------------------------------------
// RUN COMMAND (the real meat)
// ---------------------------------------------------------------------------
static void run_command(const char *cmdline) {
    printf("\n$ %s\n", cmdline);
    
    FILE *pipe = popen(cmdline, "r");
    if (!pipe) {
        printf("failed to run command: %s\n", strerror(errno));
        exit(1);
    }
    
    char buffer[1024];
    while (fgets(buffer, sizeof(buffer), pipe)) {
        printf("%s", buffer);
    }
    
    int ret = pclose(pipe);
    if (ret != 0) {
        printf("command failed with code %d\n", ret);
        exit(1);
    }
}

// ---------------------------------------------------------------------------
// OS DEV API - the cool stuff you actually cared about
// ---------------------------------------------------------------------------
static void pad_bootsector(const char *path) {
    char fullpath[1024];
    snprintf(fullpath, sizeof(fullpath), "%s%c%s", current_cwd, PATH_SEP, path);
    
    FILE *f = fopen(fullpath, "rb");
    if (!f) {
        printf("pad_bootsector: cant open %s\n", path);
        exit(1);
    }
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);
    
    if (size > 510) {
        printf("bootsector too big (max 510 bytes)\n");
        fclose(f);
        exit(1);
    }
    
    unsigned char data[512];
    memset(data, 0, sizeof(data));
    fread(data, 1, size, f);
    fclose(f);
    
    // pad with zeros and add magic boot signature
    data[510] = 0x55;
    data[511] = 0xAA;
    
    f = fopen(fullpath, "wb");
    fwrite(data, 1, 512, f);
    fclose(f);
    printf("Padded bootsector: %s (512 bytes with AA55 signature)\n", path);
}

// ---------------------------------------------------------------------------
// Dependency Handler
// ---------------------------------------------------------------------------
/*
 * i dont fucking know how to handle dependencies either so i just went full schizo mode
 * windows? handled. fedora? handled. debian? handled. arch? handled. void? handled. suse? handled.
 * if your distro isnt here then youre probably using gentoo and you deserve the pain
 */
static void handle_dependencies(const char *manager, char **deps, int dep_count) {
    printf("--- [DEPENDENCIES] AI wants me to install some shit using '%s' ---\n", manager);
    printf("--- [DETECTION MODE] figuring out what the fuck OS you're on ---\n");

    bool is_windows = false;
#ifdef _WIN32
    is_windows = true;
    printf("oh great a windows user...\n");
#endif

    // AUTO DETECT LINUX DISTRO BECAUSE AI KEEPS FUCKING UP THE MANAGER NAME
    const char *detected_manager = NULL;

    if (access("/etc/arch-release", F_OK) == 0 || access("/etc/pacman.conf", F_OK) == 0) {
        printf("DETECTED: ARCH LINUX (you beautiful degenerate)\n");
        detected_manager = "pacman";
    }
    else if (access("/etc/fedora-release", F_OK) == 0 || access("/etc/redhat-release", F_OK) == 0) {
        printf("DETECTED: FEDORA / RHEL STYLE\n");
        detected_manager = "dnf";
    }
    else if (access("/etc/debian_version", F_OK) == 0 || access("/etc/lsb-release", F_OK) == 0) {
        printf("DETECTED: DEBIAN / UBUNTU STYLE\n");
        detected_manager = "apt";
    }
    else if (access("/etc/void-release", F_OK) == 0) {
        printf("DETECTED: VOID LINUX\n");
        detected_manager = "xbps";
    }
    else if (access("/etc/os-release", F_OK) == 0) {
        // just fallback check inside os-release file
        FILE *osf = fopen("/etc/os-release", "r");
        if (osf) {
            char line[256];
            while (fgets(line, sizeof(line), osf)) {
                if (strstr(line, "ID=arch") || strstr(line, "ID_LIKE=arch")) detected_manager = "pacman";
                else if (strstr(line, "ID=fedora") || strstr(line, "ID=rhel")) detected_manager = "dnf";
                else if (strstr(line, "ID=debian") || strstr(line, "ID=ubuntu")) detected_manager = "apt";
            }
            fclose(osf);
        }
    }

    // if we detected something and the AI gave us garbage, override it
    if (detected_manager && strcmp(manager, detected_manager) != 0) {
        printf("AI gave wrong manager '%s' - forcing detected '%s' because you're on arch you animal\n", manager, detected_manager);
        manager = (char*)detected_manager; // yes this is evil but it works
    }

    // REMAP COMMON DEBIAN PACKAGE NAMES TO ARCH NAMES BECAUSE THE AI IS A FUCKING MORON
    // some ais think the entire planet runs ubuntu, so we fix their shit here
    if (strcmp(manager, "pacman") == 0) {
        for (int i = 0; i < dep_count; i++) {
            if (strcmp(deps[i], "libsdl2-dev") == 0) {
                deps[i] = "sdl2";
                printf("remapped libsdl2-dev -> sdl2 because ai is braindead\n");
            }
            else if (strcmp(deps[i], "libx11-dev") == 0) {
                deps[i] = "libx11";
                printf("remapped libx11-dev -> libx11 because ai is braindead\n");
            }
            else if (strcmp(deps[i], "build-essential") == 0) {
                printf("skipping build-essential on arch (you already have gcc + make)\n");
                deps[i] = "gcc"; // dummy so it doesn't break the loop
            }
            // add more remaps here when the ai inevitably fucks up again
            // example:
            // else if (strcmp(deps[i], "libgl1-mesa-dev") == 0) deps[i] = "mesa";
        }
    }

    if (is_windows) {
        if (strcmp(manager, "winget") == 0 || strcmp(manager, "choco") == 0) {
            char cmd[MAX_CMD] = {0};
            snprintf(cmd, sizeof(cmd), "%s install", manager);
            for (int i = 0; i < dep_count; i++) {
                strncat(cmd, " ", sizeof(cmd) - strlen(cmd) - 1);
                strncat(cmd, deps[i], sizeof(cmd) - strlen(cmd) - 1);
            }
            printf("WINDOWS DEPENDENCY HELL ACTIVATED\n");
            printf("run this exact command in an ADMINISTRATOR terminal:\n");
            printf("   %s\n", cmd);
            printf("then run your blueprint again you fucking animal\n");
            exit(1);
        }
        else if (strcmp(manager, "pip") == 0 || strcmp(manager, "pip3") == 0) {
            char cmd[MAX_CMD] = {0};
            snprintf(cmd, sizeof(cmd), "pip install --upgrade");
            for (int i = 0; i < dep_count; i++) {
                strncat(cmd, " ", sizeof(cmd) - strlen(cmd) - 1);
                strncat(cmd, deps[i], sizeof(cmd) - strlen(cmd) - 1);
            }
            printf("$ %s\n", cmd);
            system(cmd);
            printf("python deps installed on windows (probably)\n");
            return;
        }
        else {
            printf("i have no fucking clue what '%s' is on windows. use winget, choco, or pip you degenerate\n", manager);
            exit(1);
        }
    }

    // LINUX TIME BABY - the part that took me three blackouts to write
    char pm_cmd[MAX_CMD] = {0};
    bool auto_install = false;

    if (strcmp(manager, "apt") == 0 || strcmp(manager, "apt-get") == 0) {
        snprintf(pm_cmd, sizeof(pm_cmd), "sudo apt update && sudo apt install -y");
        for (int i = 0; i < dep_count; i++) {
            strncat(pm_cmd, " ", sizeof(pm_cmd) - strlen(pm_cmd) - 1);
            strncat(pm_cmd, deps[i], sizeof(pm_cmd) - strlen(pm_cmd) - 1);
        }
        printf("detected debian-style system (apt)\n");
        auto_install = true;
    }
    else if (strcmp(manager, "dnf") == 0 || strcmp(manager, "yum") == 0) {
        snprintf(pm_cmd, sizeof(pm_cmd), "sudo dnf install -y");
        for (int i = 0; i < dep_count; i++) {
            strncat(pm_cmd, " ", sizeof(pm_cmd) - strlen(pm_cmd) - 1);
            strncat(pm_cmd, deps[i], sizeof(pm_cmd) - strlen(pm_cmd) - 1);
        }
        printf("detected fedora/rhel-style system (dnf)\n");
        auto_install = true;
    }
    else if (strcmp(manager, "pacman") == 0) {
        snprintf(pm_cmd, sizeof(pm_cmd), "sudo pacman -Syu --noconfirm");
        for (int i = 0; i < dep_count; i++) {
            strncat(pm_cmd, " ", sizeof(pm_cmd) - strlen(pm_cmd) - 1);
            strncat(pm_cmd, deps[i], sizeof(pm_cmd) - strlen(pm_cmd) - 1);
        }
        printf("detected arch-style system (pacman)\n");
        auto_install = true;
    }
    else if (strcmp(manager, "xbps") == 0 || strcmp(manager, "xbps-install") == 0) {
        snprintf(pm_cmd, sizeof(pm_cmd), "sudo xbps-install -Syu");
        for (int i = 0; i < dep_count; i++) {
            strncat(pm_cmd, " ", sizeof(pm_cmd) - strlen(pm_cmd) - 1);
            strncat(pm_cmd, deps[i], sizeof(pm_cmd) - strlen(pm_cmd) - 1);
        }
        printf("detected void linux (xbps)\n");
        auto_install = true;
    }
    else if (strcmp(manager, "zypper") == 0) {
        snprintf(pm_cmd, sizeof(pm_cmd), "sudo zypper install -y");
        for (int i = 0; i < dep_count; i++) {
            strncat(pm_cmd, " ", sizeof(pm_cmd) - strlen(pm_cmd) - 1);
            strncat(pm_cmd, deps[i], sizeof(pm_cmd) - strlen(pm_cmd) - 1);
        }
        printf("detected suse/opensuse (zypper)\n");
        auto_install = true;
    }
    else if (strcmp(manager, "pip") == 0 || strcmp(manager, "pip3") == 0) {
        snprintf(pm_cmd, sizeof(pm_cmd), "pip3 install --upgrade");
        for (int i = 0; i < dep_count; i++) {
            strncat(pm_cmd, " ", sizeof(pm_cmd) - strlen(pm_cmd) - 1);
            strncat(pm_cmd, deps[i], sizeof(pm_cmd) - strlen(pm_cmd) - 1);
        }
        printf("python dependencies - no sudo needed (probably)\n");
        system(pm_cmd);
        printf("pip deps installed\n");
        return;
    }
    else if (strcmp(manager, "npm") == 0) {
        snprintf(pm_cmd, sizeof(pm_cmd), "npm install -g");
        for (int i = 0; i < dep_count; i++) {
            strncat(pm_cmd, " ", sizeof(pm_cmd) - strlen(pm_cmd) - 1);
            strncat(pm_cmd, deps[i], sizeof(pm_cmd) - strlen(pm_cmd) - 1);
        }
        printf("npm global packages - installing...\n");
        system(pm_cmd);
        printf("npm deps installed\n");
        return;
    }
    else {
        printf("i dont know what the fuck '%s' is. use apt, dnf, pacman, xbps, zypper, pip, or npm\n", manager); // how does this even work? its simple if the ai is retarded well... we make the user do it
        exit(1);
    }

    if (auto_install) {
        printf("\n$ %s\n", pm_cmd);
        printf("i'm about to run this with sudo. if you dont want that then ctrl+c right fucking now\n");
        sleep_ms(3000);
        int ret = system(pm_cmd);
        if (ret == 0) {
            printf("system dependencies installed successfully (i hope)\n");
        } else {
            printf("install failed or you cancelled. fix it yourself then rerun the blueprint\n");
            exit(1);
        }
    }
}

// ---------------------------------------------------------------------------
// BLUEPRINT PARSER (the new safer brain)
// ---------------------------------------------------------------------------
static void execute_blueprint(const char *filename) {
    FILE *f = fopen(filename, "r");
    if (!f) {
        printf("cant open blueprint %s\n", filename);
        exit(1);
    }
    
    printf("--- Starting Executor ---\n");
    
    char line[MAX_LINE];
    char content_buffer[MAX_CONTENT] = {0};
    size_t content_len = 0;
    char current_file_path[512] = {0};
    bool collecting_content = false;
    
    while (fgets(line, sizeof(line), f)) {

        line[strcspn(line, "\r\n")] = 0;
        if (strlen(line) == 0 || line[0] == '#') continue;
        
if (collecting_content) {
            if (strcmp(line, "END_FILE") == 0) {
                create_file(current_file_path, content_buffer, content_len);
                collecting_content = false;
                content_len = 0;
                current_indent_level = 0;   // reset indent after file ends
                continue;
            }

            // apply current indent level to every line
            if (current_indent_level > 0) {
                for (int i = 0; i < current_indent_level; i++) {
                    if (content_len + 1 < MAX_CONTENT) {
                        content_buffer[content_len++] = ' ';
                    }
                }
            }

            // append the actual line
            size_t to_add = strlen(line);
            if (content_len + to_add + 2 < MAX_CONTENT) {
                memcpy(content_buffer + content_len, line, to_add);
                content_len += to_add;
                content_buffer[content_len++] = '\n';
            }
            continue;
        }
        
        // parse COMMAND|arg1|arg2|...
        char *token = strtok(line, "|");
        if (!token) continue;
        
// NEW INDENT COMMAND - because some AIs have dogshit formatting
        if (strcmp(token, "INDENT") == 0) {
            char *level_str = strtok(NULL, "|");
            if (level_str) {
                current_indent_level = atoi(level_str);
                if (current_indent_level < 0) current_indent_level = 0;
                printf("indent level set to %d spaces for next file(s)\n", current_indent_level);
            }
            continue;
        }

        if (strcmp(token, "REQUIRE_TOOL") == 0) {
            char *tool = strtok(NULL, "|");
            if (tool) require_tool(tool);
        }
        else if (strcmp(token, "CREATE_FOLDER") == 0) {
            char *name = strtok(NULL, "|");
            if (name) create_project_folder(name);
        }
        else if (strcmp(token, "SET_CWD") == 0) {
            char *newcwd = strtok(NULL, "|");
            if (newcwd) set_cwd(newcwd);
        }
        else if (strcmp(token, "CREATE_FILE") == 0) {
            char *path = strtok(NULL, "|");
            if (path) {
                strncpy(current_file_path, path, sizeof(current_file_path)-1);
                current_file_path[sizeof(current_file_path)-1] = '\0';
                collecting_content = true;
                content_len = 0;
                printf("starting to read file: %s\n", current_file_path);
            }
        }
        else if (strcmp(token, "RUN_COMMAND") == 0) {
            char fullcmd[MAX_CMD] = {0};
            char *arg = strtok(NULL, "|");
            while (arg) {
                if (fullcmd[0]) strncat(fullcmd, " ", sizeof(fullcmd) - strlen(fullcmd) - 1);
                strncat(fullcmd, arg, sizeof(fullcmd) - strlen(fullcmd) - 1);
                arg = strtok(NULL, "|");
            }
            if (fullcmd[0]) run_command(fullcmd);
        }
        else if (strcmp(token, "PAD_BOOTSECTOR") == 0) {
            char *path = strtok(NULL, "|");
            if (path) pad_bootsector(path);
        }
        else if (strcmp(token, "CONCATENATE_FILES") == 0) {
            char *out = strtok(NULL, "|");
            char *f1 = strtok(NULL, "|");
            char *f2 = strtok(NULL, "|");
            if (out && f1 && f2) concatenate_files(out, f1, f2);
        }
        else if (strcmp(token, "INSTALL_DEPS") == 0) {
            char *manager = strtok(NULL, "|");
            if (!manager) {
                printf("INSTALL_DEPS needs a manager you fucking idiot\n");
                continue;
            }
            
            char *dep_list[64];
            int dep_count = 0;
            char *dep;
            while ((dep = strtok(NULL, "|")) != NULL && dep_count < 64) {
                dep_list[dep_count++] = dep;
            }
            
            if (dep_count == 0) {
                printf("INSTALL_DEPS called with zero packages, skipping like a lazy cunt\n");
                continue;
            }
            
            handle_dependencies(manager, dep_list, dep_count);
        }
        else {
            printf("unknown command ignored fuck you: %s\n", token);
        }
    }
    
    fclose(f);
    printf("\n--- Blueprint finished successfully. ---\n");
}

// ---------------------------------------------------------------------------
// NEW SHIT FOR 0.1.1- help, guide, dry-run, and direct paste support (used to be 0.1.2 but i decided to not make the bugfix update the 0.1.2)
// ---------------------------------------------------------------------------

static void print_help(void) {
    printf("executor - paste ai slop watch it build real shit\n\n");
    printf("usage:\n");
    printf("  executor <blueprint.txt>          run the blueprint\n");
    printf("  executor -d <blueprint.txt>       dry run - show what it would do\n");
    printf("  executor --dry-run <blueprint.txt> dry run\n");
    printf("  executor \"COMMAND|arg1|arg2\"     run blueprint directly from quotes\n");
    printf("  executor guide                    print the full ai guide\n");
    printf("  executor help                     show this help + safety tips\n\n");
    printf("safety tips you absolute animal:\n");
    printf("  - only run blueprints you trust\n");
    printf("  - dry-run first if you're not sure\n");
    printf("  - it can still rm -rf your shit if the ai is evil\n");
    printf("  - INSTALL_DEPS will run sudo commands\n");
    printf("  - no real sandbox because we are not cowards\n");
}

static void print_guide(void) {
    FILE *f = fopen("guide.txt", "r");
    if (!f) {
        f = fopen("src/guide.txt", "r");  // in case you put it in src/
    }
    if (!f) {
        printf("you dumbass you moved or deleted guide.txt\n");
        printf("put it back in the same folder as the executor binary or in src/guide.txt\n");
        return;
    }
    
    printf("=== executor AI GUIDE ===\n\n");
    char line[4096];
    while (fgets(line, sizeof(line), f)) {
        printf("%s", line);
    }
    fclose(f);
    printf("\n=== end of guide ===\n");
}

static void execute_blueprint_dry(const char *filename) {
    // for now just print the lines - later we can make it prettier
    FILE *f = fopen(filename, "r");
    if (!f) {
        printf("cant open %s for dry run\n", filename);
        return;
    }
    printf("--- DRY RUN MODE ---\n");
    printf("executor would do the following:\n\n");
    
    char line[MAX_LINE];
    while (fgets(line, sizeof(line), f)) {
        line[strcspn(line, "\r\n")] = 0;
        if (strlen(line) == 0 || line[0] == '#') continue;
        printf("WOULD EXECUTE: %s\n", line);
    }
    fclose(f);
    printf("\n--- end of dry run ---\n");
    printf("nothing was actually run. safe.\n");
}

// ---------------------------------------------------------------------------
// MAIN
// ---------------------------------------------------------------------------
int main(int argc, char **argv) {
    if (argc < 2) {
        print_help();
        return 1;
    }

    // handle special commands first
    if (strcmp(argv[1], "help") == 0) {
        print_help();
        return 0;
    }
    if (strcmp(argv[1], "guide") == 0) {
        print_guide();
        return 0;
    }

    bool dry_run = false;
    const char *target = NULL;

    // check for dry-run flags
    if (strcmp(argv[1], "-d") == 0 || strcmp(argv[1], "--dry-run") == 0) {
        dry_run = true;
        if (argc < 3) {
            printf("you forgot the blueprint file you idiot\n");
            return 1;
        }
        target = argv[2];
    } 
    else {
        target = argv[1];
    }

    // if the argument looks like it starts with a command (has | ) treat it as direct paste
    if (strchr(target, '|') != NULL) {
        printf("running direct blueprint paste...\n");
        // for direct paste we just write it to a temp file real quick
        FILE *tmp = fopen("temp_blueprint.txt", "w");
        if (tmp) {
            fprintf(tmp, "%s\n", target);
            fclose(tmp);
            if (dry_run) {
                execute_blueprint_dry("temp_blueprint.txt");
            } else {
                execute_blueprint("temp_blueprint.txt");
            }
            remove("temp_blueprint.txt");  // cleanup
        }
        return 0;
    }

    // normal file mode
    if (dry_run) {
        execute_blueprint_dry(target);
    } else {
        execute_blueprint(target);
    }

    return 0;
}