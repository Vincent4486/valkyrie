// SPDX-License-Identifier: BSD-3-Clause
// Simple Valkyrie OS builder with ncurses TUI.

#include <curses.h>
#include <errno.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CFG_PATH ".config"
#define MAX_FIELD 256

typedef struct {
	char config[MAX_FIELD];      // debug | release
	char arch[MAX_FIELD];        // i686 | x64 | aarch64
	char imageFS[MAX_FIELD];     // fat12 | fat16 | fat32 | ext2
	char buildType[MAX_FIELD];   // full | kernel | usr | image
	char imageSize[MAX_FIELD];   // e.g. 250m
	char toolchain[MAX_FIELD];   // path
	char outputFile[MAX_FIELD];  // valkyrieos
	char outputFormat[MAX_FIELD];// img
	char kernelName[MAX_FIELD];  // valkyrix
} BuildConfig;

static const char *CONFIG_CHOICES[] = {"debug", "release", NULL};
static const char *ARCH_CHOICES[] = {"i686", "x64", "aarch64", NULL};
static const char *FS_CHOICES[] = {"fat12", "fat16", "fat32", "ext2", NULL};
static const char *BUILD_CHOICES[] = {"full", "kernel", "usr", "image", NULL};

// ---------------------------------------------------------------------------
// Small helpers
// ---------------------------------------------------------------------------

static void safe_copy(char *dst, const char *src) {
	if (!src) {
		dst[0] = '\0';
		return;
	}
	strncpy(dst, src, MAX_FIELD - 1);
	dst[MAX_FIELD - 1] = '\0';
}

static char *trim(char *s) {
	while (*s == ' ' || *s == '\t' || *s == '\n' || *s == '\r') s++;
	char *end = s + strlen(s);
	while (end > s && (end[-1] == ' ' || end[-1] == '\t' || end[-1] == '\n' || end[-1] == '\r')) {
		*--end = '\0';
	}
	if (end > s) {
		char quote = *s;
		if ((quote == '\'' || quote == '"') && end[-1] == quote) {
			s++;
			end[-1] = '\0';
		}
	}
	return s;
}

static void set_defaults(BuildConfig *cfg) {
	safe_copy(cfg->config, "debug");
	safe_copy(cfg->arch, "i686");
	safe_copy(cfg->imageFS, "fat32");
	safe_copy(cfg->buildType, "full");
	safe_copy(cfg->imageSize, "250m");
	safe_copy(cfg->toolchain, "toolchain/");
	safe_copy(cfg->outputFile, "valkyrieos");
	safe_copy(cfg->outputFormat, "img");
	safe_copy(cfg->kernelName, "valkyrix");
}

// ---------------------------------------------------------------------------
// Config load/save
// ---------------------------------------------------------------------------

static void load_config(BuildConfig *cfg) {
	set_defaults(cfg);
	FILE *f = fopen(CFG_PATH, "r");
	if (!f) return;

	char line[512];
	while (fgets(line, sizeof(line), f)) {
		char *eq = strchr(line, '=');
		if (!eq) continue;
		*eq = '\0';
		char *key = trim(line);
		char *val = trim(eq + 1);

		if (strcmp(key, "config") == 0) safe_copy(cfg->config, val);
		else if (strcmp(key, "arch") == 0) safe_copy(cfg->arch, val);
		else if (strcmp(key, "imageFS") == 0) safe_copy(cfg->imageFS, val);
		else if (strcmp(key, "buildType") == 0) safe_copy(cfg->buildType, val);
		else if (strcmp(key, "imageSize") == 0) safe_copy(cfg->imageSize, val);
		else if (strcmp(key, "toolchain") == 0) safe_copy(cfg->toolchain, val);
		else if (strcmp(key, "outputFile") == 0) safe_copy(cfg->outputFile, val);
		else if (strcmp(key, "outputFormat") == 0) safe_copy(cfg->outputFormat, val);
		else if (strcmp(key, "kernelName") == 0) safe_copy(cfg->kernelName, val);
	}
	fclose(f);
}

static int save_config(const BuildConfig *cfg) {
	FILE *f = fopen(CFG_PATH, "w");
	if (!f) return -1;
	fprintf(f, "config = '%s'\n", cfg->config);
	fprintf(f, "arch = '%s'\n", cfg->arch);
	fprintf(f, "imageFS = '%s'\n", cfg->imageFS);
	fprintf(f, "buildType = '%s'\n", cfg->buildType);
	fprintf(f, "imageSize = '%s'\n", cfg->imageSize);
	fprintf(f, "toolchain = '%s'\n", cfg->toolchain);
	fprintf(f, "outputFile = '%s'\n", cfg->outputFile);
	fprintf(f, "outputFormat = '%s'\n", cfg->outputFormat);
	fprintf(f, "kernelName = '%s'\n", cfg->kernelName);
	fclose(f);
	return 0;
}

// ---------------------------------------------------------------------------
// Command execution
// ---------------------------------------------------------------------------

static int run_command(const char *cmd) {
	int rc = system(cmd);
	if (rc == -1) return errno ? -errno : -1;
	if (WIFEXITED(rc)) return WEXITSTATUS(rc);
	return rc;
}

// ---------------------------------------------------------------------------
// ncurses UI helpers
// ---------------------------------------------------------------------------

static int menu_select(const char *title, const char *const options[], const char *current) {
	int idx = 0;
	for (int i = 0; options[i]; ++i) {
		if (strcmp(options[i], current) == 0) {
			idx = i;
			break;
		}
	}

	int ch;
	while (1) {
		clear();
		mvprintw(1, 2, "%s", title);
		mvprintw(2, 2, "Use arrows, Enter to select, q to cancel.");
		for (int i = 0; options[i]; ++i) {
			if (i == idx) attron(A_REVERSE);
			mvprintw(4 + i, 4, "%s", options[i]);
			if (i == idx) attroff(A_REVERSE);
		}
		ch = getch();
		if (ch == KEY_UP && idx > 0) idx--;
		else if (ch == KEY_DOWN && options[idx + 1]) idx++;
		else if (ch == '\n') return idx;
		else if (ch == 'q' || ch == 27) return -1;
	}
}

static void prompt_text(const char *label, char *dest) {
	echo();
	curs_set(1);
	clear();
	mvprintw(1, 2, "%s", label);
	mvprintw(3, 2, "Current: %s", dest);
	mvprintw(5, 2, "Enter new value (leave empty to keep): ");
	char buf[MAX_FIELD];
	getnstr(buf, MAX_FIELD - 1);
	if (strlen(buf) > 0) safe_copy(dest, buf);
	noecho();
	curs_set(0);
}

static void edit_config_menu(BuildConfig *cfg) {
	const char *fields[] = {
		"config", "arch", "imageFS", "buildType", "imageSize",
		"toolchain", "outputFile", "outputFormat", "kernelName", NULL};
	int idx = 0;
	int ch;
	while (1) {
		clear();
		mvprintw(1, 2, "Edit configuration (Enter to modify, q to exit)");
		for (int i = 0; fields[i]; ++i) {
			if (i == idx) attron(A_REVERSE);
			const char *val = "";
			if (strcmp(fields[i], "config") == 0) val = cfg->config;
			else if (strcmp(fields[i], "arch") == 0) val = cfg->arch;
			else if (strcmp(fields[i], "imageFS") == 0) val = cfg->imageFS;
			else if (strcmp(fields[i], "buildType") == 0) val = cfg->buildType;
			else if (strcmp(fields[i], "imageSize") == 0) val = cfg->imageSize;
			else if (strcmp(fields[i], "toolchain") == 0) val = cfg->toolchain;
			else if (strcmp(fields[i], "outputFile") == 0) val = cfg->outputFile;
			else if (strcmp(fields[i], "outputFormat") == 0) val = cfg->outputFormat;
			else if (strcmp(fields[i], "kernelName") == 0) val = cfg->kernelName;
			mvprintw(3 + i, 4, "%-12s : %s", fields[i], val);
			if (i == idx) attroff(A_REVERSE);
		}
		ch = getch();
		if (ch == KEY_UP && idx > 0) idx--;
		else if (ch == KEY_DOWN && fields[idx + 1]) idx++;
		else if (ch == 'q' || ch == 27) return;
		else if (ch == '\n') {
			const char *field = fields[idx];
			if (strcmp(field, "config") == 0) {
				int sel = menu_select("Select config", CONFIG_CHOICES, cfg->config);
				if (sel >= 0) safe_copy(cfg->config, CONFIG_CHOICES[sel]);
			} else if (strcmp(field, "arch") == 0) {
				int sel = menu_select("Select arch", ARCH_CHOICES, cfg->arch);
				if (sel >= 0) safe_copy(cfg->arch, ARCH_CHOICES[sel]);
			} else if (strcmp(field, "imageFS") == 0) {
				int sel = menu_select("Select filesystem", FS_CHOICES, cfg->imageFS);
				if (sel >= 0) safe_copy(cfg->imageFS, FS_CHOICES[sel]);
			} else if (strcmp(field, "buildType") == 0) {
				int sel = menu_select("Select build type", BUILD_CHOICES, cfg->buildType);
				if (sel >= 0) safe_copy(cfg->buildType, BUILD_CHOICES[sel]);
			} else {
				prompt_text(field, (idx == 4) ? cfg->imageSize :
						   (idx == 5) ? cfg->toolchain :
						   (idx == 6) ? cfg->outputFile :
						   (idx == 7) ? cfg->outputFormat : cfg->kernelName);
			}
		}
	}
}

// ---------------------------------------------------------------------------
// Main UI
// ---------------------------------------------------------------------------

static void tui(BuildConfig *cfg) {
	initscr();
	cbreak();
	noecho();
	keypad(stdscr, TRUE);
	curs_set(0);

	const char *menu_items[] = {
		"Edit configuration",
		"Save",
		"Save and build (C builder)",
		"Clean (C builder)",
		"Exit",
		NULL};
	int idx = 0;
	int ch;

	while (1) {
		clear();
		mvprintw(1, 2, "Valkyrie OS Builder");
		mvprintw(2, 2, "Use arrows and Enter. q to quit.");
		mvprintw(4, 2, "Current: %s | %s | %s | %s", cfg->config, cfg->arch, cfg->imageFS, cfg->buildType);
		for (int i = 0; menu_items[i]; ++i) {
			if (i == idx) attron(A_REVERSE);
			mvprintw(6 + i, 4, "%s", menu_items[i]);
			if (i == idx) attroff(A_REVERSE);
		}

		ch = getch();
		if (ch == KEY_UP && idx > 0) idx--;
		else if (ch == KEY_DOWN && menu_items[idx + 1]) idx++;
		else if (ch == 'q' || ch == 27) break;
		else if (ch == '\n') {
			const char *item = menu_items[idx];
			if (strcmp(item, "Edit configuration") == 0) {
				edit_config_menu(cfg);
			} else if (strcmp(item, "Save") == 0) {
				save_config(cfg);
			} else if (strcmp(item, "Save and build (C builder)") == 0) {
				save_config(cfg);
				endwin();
				printf("Running: ./tools/builder/build --build\n");
				int rc = run_command("./tools/builder/build --build");
				if (rc != 0) fprintf(stderr, "build failed: %d\n", rc);
				return;
			} else if (strcmp(item, "Clean (C builder)") == 0) {
				endwin();
				printf("Running: ./tools/builder/build --clean\n");
				int rc = run_command("./tools/builder/build --clean");
				if (rc != 0) fprintf(stderr, "clean failed: %d\n", rc);
				return;
			} else if (strcmp(item, "Exit") == 0) {
				break;
			}
		}
	}

	endwin();
}

// ---------------------------------------------------------------------------
// CLI handling
// ---------------------------------------------------------------------------

static void usage(const char *prog) {
	fprintf(stderr, "Usage: %s [--menu] [--build] [--clean] [--set key=val ...]\n", prog);
}

int main(int argc, char **argv) {
	BuildConfig cfg;
	load_config(&cfg);

	bool want_menu = (argc == 1);
	bool do_build = false;
	bool do_clean = false;

	for (int i = 1; i < argc; ++i) {
		if (strcmp(argv[i], "--menu") == 0) want_menu = true;
		else if (strcmp(argv[i], "--build") == 0) { do_build = true; want_menu = false; }
		else if (strcmp(argv[i], "--clean") == 0) { do_clean = true; want_menu = false; }
		else if (strcmp(argv[i], "--set") == 0 && i + 1 < argc) {
			char *kv = argv[++i];
			char *eq = strchr(kv, '=');
			if (!eq) continue;
			*eq = '\0';
			const char *key = kv;
			const char *val = eq + 1;
			if (strcmp(key, "config") == 0) safe_copy(cfg.config, val);
			else if (strcmp(key, "arch") == 0) safe_copy(cfg.arch, val);
			else if (strcmp(key, "imageFS") == 0) safe_copy(cfg.imageFS, val);
			else if (strcmp(key, "buildType") == 0) safe_copy(cfg.buildType, val);
			else if (strcmp(key, "imageSize") == 0) safe_copy(cfg.imageSize, val);
			else if (strcmp(key, "toolchain") == 0) safe_copy(cfg.toolchain, val);
			else if (strcmp(key, "outputFile") == 0) safe_copy(cfg.outputFile, val);
			else if (strcmp(key, "outputFormat") == 0) safe_copy(cfg.outputFormat, val);
			else if (strcmp(key, "kernelName") == 0) safe_copy(cfg.kernelName, val);
		} else {
			usage(argv[0]);
			return 1;
		}
	}

	if (want_menu) {
		tui(&cfg);
		save_config(&cfg);
		return 0;
	}

	if (do_clean) {
		save_config(&cfg);
		return run_command("./tools/builder/build --clean");
	}

	if (do_build) {
		save_config(&cfg);
		return run_command("./tools/builder/build --build");
	}

	usage(argv[0]);
	return 1;
}
