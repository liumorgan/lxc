/*
 * lxc: linux Container library
 *
 * (C) Copyright IBM Corp. 2007, 2008
 *
 * Authors:
 * Daniel Lezcano <dlezcano at fr.ibm.com>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
 */
#include <stdio.h>
#include <string.h>
#include <fcntl.h>
#include <errno.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/types.h>
#include <sys/param.h>
#include <sys/stat.h>
#include <sys/file.h>

#include <lxc/lxc.h>
#include <lxc/log.h>
#include "commands.h"

lxc_log_define(lxc_state, lxc);

static char *strstate[] = {
	"STOPPED", "STARTING", "RUNNING", "STOPPING",
	"ABORTING", "FREEZING", "FROZEN",
};

const char *lxc_state2str(lxc_state_t state)
{
	if (state < STOPPED || state > MAX_STATE - 1)
		return NULL;
	return strstate[state];
}

lxc_state_t lxc_str2state(const char *state)
{
	int i, len;
	len = sizeof(strstate)/sizeof(strstate[0]);
	for (i = 0; i < len; i++)
		if (!strcmp(strstate[i], state))
			return i;

	ERROR("invalid state '%s'", state);
	return -1;
}

int lxc_rmstate(const char *name)
{
	char file[MAXPATHLEN];
	snprintf(file, MAXPATHLEN, LXCPATH "/%s/state", name);
	unlink(file);
	return 0;
}

lxc_state_t __lxc_getstate(const char *name)
{
	int fd, err;
	char file[MAXPATHLEN];

	snprintf(file, MAXPATHLEN, LXCPATH "/%s/state", name);

	fd = open(file, O_RDONLY);
	if (fd < 0) {
		SYSERROR("failed to open %s", file);
		return -1;
	}

	if (flock(fd, LOCK_SH)) {
		SYSERROR("failed to take the lock to %s", file);
		close(fd);
		return -1;
	}

	err = read(fd, file, strlen(file));
	if (err < 0) {
		SYSERROR("failed to read file %s", file);
		close(fd);
		return -1;
	}
	file[err] = '\0';

	close(fd);
	return lxc_str2state(file);
}

static int freezer_state(const char *name)
{
	char freezer[MAXPATHLEN];
	char status[MAXPATHLEN];
	FILE *file;
	int err;

	snprintf(freezer, MAXPATHLEN,
		 LXCPATH "/%s/freezer.state", name);

	file = fopen(freezer, "r");
	if (!file)
		return -1;

	err = fscanf(file, "%s", status);
	fclose(file);

	if (err == EOF) {
		SYSERROR("failed to read %s", freezer);
		return -1;
	}

	return lxc_str2state(status);
}

lxc_state_t lxc_getstate(const char *name)
{
	struct lxc_command command = {
		.request = { .type = LXC_COMMAND_STATE },
	};

	int ret;

	ret = lxc_command(name, &command);
	if (ret < 0) {
		ERROR("failed to send command");
		return -1;
	}

	if (!ret) {
		WARN("'%s' has stopped before sending its state", name);
		return -1;
	}

	if (command.answer.ret < 0) {
		ERROR("failed to get state for '%s': %s",
			name, strerror(-command.answer.ret));
		return -1;
	}

	DEBUG("'%s' is in '%s' state", name, lxc_state2str(command.answer.ret));

	return command.answer.ret;
}

lxc_state_t lxc_state(const char *name)
{
	int state = freezer_state(name);
	if (state != FROZEN && state != FREEZING)
		state = lxc_getstate(name);
	return state;
}

/*----------------------------------------------------------------------------
 * functions used by lxc-start mainloop
 * to handle above command request.
 *--------------------------------------------------------------------------*/
extern int lxc_state_callback(int fd, struct lxc_request *request,
			struct lxc_handler *handler)
{
	struct lxc_answer answer;
	int ret;

	answer.ret = handler->state;

	ret = send(fd, &answer, sizeof(answer), 0);
	if (ret < 0) {
		WARN("failed to send answer to the peer");
		goto out;
	}

	if (ret != sizeof(answer)) {
		ERROR("partial answer sent");
		goto out;
	}

out:
	return ret;
}

