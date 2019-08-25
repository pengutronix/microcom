/*
 * Copyright (C) 2010 Sascha Hauer <s.hauer@pengutronix.de>
 *
 * This program is free software; you can redistribute it and/or modify it under
 * the terms of the GNU General Public License as published by the Free Software
 * Foundation; either version 2 of the License, or (at your option) any later
 * version.
 */
#include <stdio.h>
#include <readline/readline.h>
#include <readline/history.h>
#include "microcom.h"

int parse_line(char *_line, int *argc, char *argv[])
{
	char *line = _line;
	int nargs = 0;

	if (!line)
		goto out;

	while (nargs < MAXARGS) {

		/* skip any white space */
		while (*line == ' ' || *line == '\t')
			++line;

		if (*line == '\0' || *line == ';')    /* end of line, no more args    */
			goto out;

		argv[nargs] = line;   /* begin of argument string     */

		if (*line == '\"') {
			line++;
			argv[nargs] = line;
			while (*line && *line != '\"')
				line++;
			if (!*line) {
				printf("could not find matching '\"'\n");
				return -EINVAL;
			} else
				*line++ = '\0';
		} else {
			/* find end of string */
			while (*line && *line != ' ' && *line != '\t' && *line != ';')
				++line;
		}

		nargs++;


		if (*line == '\0')    /* end of line, no more args    */
			goto out;
		if (*line == ';') {
			*line = '\0';
			goto out;
		}

		*line++ = '\0';         /* terminate current arg         */
	}

        printf("Too many args (max. %d)\n", MAXARGS);
out:
	argv[nargs] = NULL;
	if (argc)
		*argc = nargs;

	return line - _line + 1;
}

struct cmd *commands;

int register_command(struct cmd *cmd)
{
	struct cmd *tmp;

	cmd->next = NULL;

	if (!commands) {
		commands = cmd;
		return 0;		
	}

	tmp = commands;

	while (tmp->next)
		tmp = tmp->next;

	tmp->next = cmd;

	return 0;
}

void microcom_cmd_usage(char *command)
{
	struct cmd *cmd;

	for_each_command(cmd) {
		if (!strcmp(command, cmd->name)) {
			char *str = NULL;
			if (cmd->info)
				str = cmd->info;
			if (cmd->help)
				str = cmd->help;
			if (!str)
				str = "no help available\n";
			printf("usage:\n%s\n", str);
			return;
		}
	}
	printf("no such command\n");
}

static int __do_commandline(const char *prompt)
{
	char *cmd;
	char *argv[MAXARGS + 1];
	int argc = 0, ret = 0, n, len;

	while (1) {
		struct cmd *command;
		cmd = readline(prompt);
		if (!cmd) {
			ret = MICROCOM_CMD_START;
			break;
		}

		if (!strlen(cmd))
			goto done;

		if (prompt)
			add_history(cmd);

		len = strlen(cmd);
		n = 0;
		while (n < len) {
			int handled = 0;
			ret = parse_line(cmd + n, &argc, argv);
			if (ret < 0)
				break;
			n += ret;
			if (!argv[0])
				continue;

			for_each_command(command) {
				if (!strcmp(argv[0], command->name)) {
					ret = command->fn(argc, argv);
					if (ret == MICROCOM_CMD_START) {
						free(cmd);
						return ret;
					}

					if (ret == MICROCOM_CMD_USAGE)
						microcom_cmd_usage(argv[0]);

					handled = 1;
					break;
				}
			}
			if (!handled)
				printf("unknown command \'%s\', try \'help\'\n", argv[0]);
		}
done:
		free(cmd);
	}

	if (cmd)
		free(cmd);
	return ret;
}

int do_commandline(void)
{
	int ret;

	restore_terminal();
	printf("\nEnter command. Try \'help\' for a list of builtin commands\n");

	do {
		ret = __do_commandline("-> ");
	} while (ret != MICROCOM_CMD_START);

	printf("\n----------------------\n");
	init_terminal();

	return 0;
}

int do_script(char *script)
{
	int fd = open(script, O_RDONLY | O_CLOEXEC);
	int stdin = dup(1);
	int ret;

	if (fd < 0) {
		printf("could not open %s: %s\n", script, strerror(errno));
		return -1;
	}

	dup2(fd, 0);
	ret = __do_commandline(NULL);
	dup2(stdin, 0);

	return ret;
}

