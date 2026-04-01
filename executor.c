/*
 * executor - 0.1.1 (the version that doesn't suck)
 * 
 * i have no idea what half this code does but it works on my machine so fuck you
 * rewrote because Python was slow as shit and the old sandbox was a security incident waiting to happen
 * now the blueprint is a braindead text DSL. AI spits commands, we execute them. No more exec() russian roulette.
 * Safer, faster, and i actually know what the fuck is happening under the hood (most of the time)
 */

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
// FILESYSTEM SHIT
// ---------------------------------------------------------------------------
static void create_project_folder(const char *name) {
    char fullpath[1024];
    snprintf(fullpath, sizeof(fullpath), "%s%c%s", current_cwd, PATH_SEP, name);
    if (mkdir(fullpath) == 0 || errno == EEXIST) {
        printf("Created folder: %s/\n", name);
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
    printf("set working directory to: %s\n", current_cwd);
}

static void create_file(const char *path, const char *content, size_t content_len) {
    char fullpath[1024];
    snprintf(fullpath, sizeof(fullpath), "%s%c%s", current_cwd, PATH_SEP, path);
    
    FILE *f = fopen(fullpath, "wb");
    if (!f) {
        printf("failed to create file %s: %s\n", path, strerror(errno));
        exit(1);
    }
    fwrite(content, 1, content_len, f);  // now binary safe you fucking animal
    fclose(f);
    printf("created file: %s\n", path);
}

// ---------------------------------------------------------------------------
// RUN COMMAND (the real meat)
// ---------------------------------------------------------------------------
static void run_command(const char *cmdline) {
    printf("\n$ %s\n", cmdline);
    
    FILE *pipe = popen(cmdline, "r");
    if (!pipe) {
        printf("failed to run command\n");
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

static void concatenate_files(const char *output, const char *file1, const char *file2) {
    char outpath[1024], p1[1024], p2[1024];
    snprintf(outpath, sizeof(outpath), "%s%c%s", current_cwd, PATH_SEP, output);
    snprintf(p1, sizeof(p1), "%s%c%s", current_cwd, PATH_SEP, file1);
    snprintf(p2, sizeof(p2), "%s%c%s", current_cwd, PATH_SEP, file2);
    
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
    printf("Concatenated %s + %s -> %s\n", file1, file2, output);
}

// ---------------------------------------------------------------------------
// Dependency Handler - now with zero buffer overflow cancer
// ---------------------------------------------------------------------------

/*
 * i dont fucking know how to handle dependencies either so i just went full schizo mode and made this thing detect literally every distro you could possibly be using
 * windows? handled. fedora? handled. debian? handled. arch? handled. void? handled. suse? handled. if your distro isnt here then youre probably using gentoo and you deserve the pain
 */

static void handle_dependencies(const char *manager, char **deps, int dep_count) {
    printf("--- [DEPENDENCIES] AI wants me to install some shit using '%s' ---\n", manager);
    
    bool is_windows = false;
#ifdef _WIN32
    is_windows = true;
    printf("oh great a windows user... fine i'll try to not break your soul\n");
#endif

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
        printf("i dont know what the fuck '%s' is. use apt, dnf, pacman, xbps, zypper, pip, or npm\n", manager);
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
                continue;
            }
            size_t to_add = strlen(line);
            if (content_len + to_add + 2 < MAX_CONTENT) {
                memcpy(content_buffer + content_len, line, to_add);
                content_len += to_add;
                content_buffer[content_len++] = '\n';
            }
            continue;
        }
        
        char *token = strtok(line, "|");
        if (!token) continue;
        
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
                collecting_content = true;
                content_len = 0;
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
// MAIN - the only thing you actually run
// ---------------------------------------------------------------------------
int main(int argc, char **argv) {
    if (argc < 2) {
        printf("Usage: %s <blueprint.txt>\n", argv[0]);
        printf("you absolute fucking idiot\n");
        return 1;
    }
    
    printf("executor - now actually fast you whiny bitch\n");
    
    execute_blueprint(argv[1]);
    return 0;
}