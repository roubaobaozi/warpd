/*
 * warpd - A modal keyboard-driven pointing system.
 *
 * © 2019 Raheman Vaiya (see: LICENSE).
 */

#include "warpd.h"

struct cfg *cfg;
char config_dir[512];

static int dragging = 0;

void toggle_drag()
{
	dragging = !dragging;

	if (dragging)
		mouse_down(1);
	else
		mouse_up(1);
}

static int oneshot_mode;

static void activation_loop(int mode)
{
	struct input_event *ev = NULL;

	dragging = 0;

	while (1) {
		switch (mode) {
		case MODE_NORMAL:
			ev = normal_mode(ev);

			if (input_event_eq(ev, cfg->hint))
				mode = MODE_HINT;
			else if (input_event_eq(ev, cfg->grid))
				mode = MODE_GRID;
			else if (input_event_eq(ev, cfg->screen))
				mode = MODE_SCREEN_SELECTION;
			else if (input_event_eq(ev, cfg->exit) || !ev)
				goto exit;

			break;
		case MODE_HINT:
			if (hint_mode() < 0)
				goto exit;

			ev = NULL;
			mode = MODE_NORMAL;
			break;
		case MODE_GRID:
			ev = grid_mode();
			if (input_event_eq(ev, cfg->grid_exit))
				ev = NULL;
			mode = MODE_NORMAL;
			break;
		case MODE_SCREEN_SELECTION:
			screen_selection_mode();
			mode = MODE_NORMAL;
			break;
		}
	}

exit:
	if (dragging)
		toggle_drag();
	return;
}

static void normalize_dimensions()
{
	int sw, sh;
	screen_t scr;

	mouse_get_position(&scr, NULL, NULL);
	screen_get_dimensions(scr, &sw, &sh);

	cfg->speed = (cfg->speed * sh) / 1080;
	cfg->cursor_size = (cfg->cursor_size * sh) / 1080;
	cfg->grid_size = (cfg->grid_size * sh) / 1080;
	cfg->grid_border_size = (cfg->grid_border_size * sh) / 1080;
}

static void oneshot_loop()
{
	normalize_dimensions();

	init_mouse();
	init_hint_mode();

	activation_loop(oneshot_mode);
}

static void main_loop()
{
	normalize_dimensions();

	init_mouse();
	init_hint_mode();

	struct input_event activation_events[5] = {0};

	input_parse_string(&activation_events[0], cfg->activation_key);
	input_parse_string(&activation_events[1], cfg->hint_activation_key);
	input_parse_string(&activation_events[2], cfg->grid_activation_key);
	input_parse_string(&activation_events[3], cfg->hint_oneshot_key);
	input_parse_string(&activation_events[4], cfg->screen_activation_key);

	while (1) {
		int mode = 0;
		struct input_event *ev = input_wait(activation_events, sizeof(activation_events)/sizeof(activation_events[0]));

		if (input_event_eq(ev, cfg->activation_key))
			mode = MODE_NORMAL;
		else if (input_event_eq(ev, cfg->grid_activation_key))
			mode = MODE_GRID;
		else if (input_event_eq(ev, cfg->hint_activation_key))
			mode = MODE_HINT;
		else if (input_event_eq(ev, cfg->screen_activation_key))
			mode = MODE_SCREEN_SELECTION;
		else if (input_event_eq(ev, cfg->hint_oneshot_key)) {
			hint_mode();
			continue;
		}

		activation_loop(mode);
	}
}

static void lock()
{
	char path[1024];
	int fd;
	sprintf(path, "%s/lock", config_dir);

	if ((fd = open(path, O_CREAT | O_RDWR, 0600)) == -1) {
		perror("flock open");
		exit(1);
	}

	if (flock(fd, LOCK_EX | LOCK_NB) == -1) {
		fprintf(
		    stderr,
		    "ERROR: Another instance of warpd is already running.\n");
		exit(-1);
	}
}

static void daemonize()
{
	char path[1024];

	if (fork())
		exit(0);
	if (fork())
		exit(0);

	sprintf(path, "%s/warpd.log", config_dir);
	printf("daemonizing, log output stored in %s.\n", path);

	int fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, S_IRUSR | S_IWUSR);
	if (fd < 0) {
		perror("open");
		exit(-1);
	}

	close(1);
	close(2);

	dup2(fd, 1);
	dup2(fd, 2);
}

static void print_keys_loop()
{
	size_t i;
	for (i = 0; i < 256; i++) {
		const char *name = input_lookup_name(i);

		if (name)
			printf("%s\n", name);
	}
}

static void print_version()
{
	printf("warpd v" VERSION " (built from: " COMMIT ")\n");
}

const char *resolve_config_path()
{
	static char path[1024];

	if (getenv("XDG_CONFIG_HOME")) {
		sprintf(path, "%s/warpd", getenv("XDG_CONFIG_HOME"));
		mkdir(path, 0700);
	} else {
		sprintf(path, "%s/.config", getenv("HOME"));
		mkdir(path, 0700);
		strcat(path, "/warpd");
		mkdir(path, 0700);
	}

	strcat(path, "/config");

	return path;
}

int main(int argc, char *argv[])
{
	int foreground_flag = 0;
	const char *config_path;

	if (argc > 1 && (!strcmp(argv[1], "-v") ||
			 !strcmp(argv[1], "--version"))) {
		print_version();
		return 0;
	}

	if (argc > 1 && (!strcmp(argv[1], "-l") ||
			 !strcmp(argv[1], "--list-keys"))) {
		start_main_loop(print_keys_loop);
		return 0;
	}

	if (argc > 1 && (!strcmp(argv[1], "-f") ||
			 !strcmp(argv[1], "--foreground")))
		foreground_flag++;

	cfg = parse_cfg(resolve_config_path());

	if (argc > 1 && !strcmp(argv[1], "--hint")) {
		oneshot_mode = MODE_HINT;
		start_main_loop(oneshot_loop);
		exit(0);
	}

	if (argc > 1 && !strcmp(argv[1], "--normal")) {
		oneshot_mode = MODE_NORMAL;
		start_main_loop(oneshot_loop);
		exit(0);
	}

	if (argc > 1 && !strcmp(argv[1], "--grid")) {
		oneshot_mode = MODE_GRID;
		start_main_loop(oneshot_loop);
		exit(0);
	}

	lock();
	if (!foreground_flag)
		daemonize();

	setvbuf(stdout, NULL, _IOLBF, 0);
	printf("Starting warpd: " VERSION "\n");

	start_main_loop(main_loop);
}
