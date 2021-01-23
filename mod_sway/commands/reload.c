#define _POSIX_C_SOURCE 200809L
#include <string.h>
#include "sway/commands.h"
#include "sway/config.h"
#include "sway/ipc-server.h"
#include "sway/server.h"
#include "sway/tree/arrange.h"
#include "sway/tree/view.h"
#include "list.h"
#include "log.h"

static void rebuild_textures_iterator(struct sway_container *con, void *data) {
	container_update_marks_textures(con);
	container_update_title_textures(con);
}

static void do_reload(void *data) {
	if (!load_main_config(true)) {
		sway_log(SWAY_ERROR, "Error(s) reloading config");
		return;
	}

	ipc_event_workspace(NULL, NULL, "reload");

	config_update_font_height(true);
	root_for_each_container(rebuild_textures_iterator, NULL);

	arrange_root();
}

struct cmd_results *cmd_reload(int argc, char **argv) {
	struct cmd_results *error = NULL;
	if ((error = checkarg(argc, "reload", EXPECTED_EQUAL_TO, 0))) {
		return error;
	}

	// The reload command frees a lot of stuff, so to avoid use-after-frees
	// we schedule the reload to happen using an idle event.
	wl_event_loop_add_idle(server.wl_event_loop, do_reload, NULL);

	return cmd_results_new(CMD_SUCCESS, NULL);
}
