/*
 * Copyright (C) 2010 Sascha Hauer <s.hauer@pengutronix.de>
 *
 * This program is free software; you can redistribute it and/or modify it under
 * the terms of the GNU General Public License as published by the Free Software
 * Foundation; either version 2 of the License, or (at your option) any later
 * version.
 */
#include <stdlib.h>
#include "microcom.h"

static int cmd_speed(int argc, char *argv[])
{
	unsigned long speed;
	int ret;

	if (argc < 2) {
		printf("current speed: %lu\n", current_speed);
		return 0;
	}

	speed = strtoul(argv[1], NULL, 0);

	ret = ios->set_speed(ios, speed);
	if (ret) {
		fprintf(stderr, "invalid speed %lu\n", speed);
		return ret;
	}

	current_speed = speed;
	return 0;
}

static int cmd_flow(int argc, char *argv[])
{
	char *flow;

	if (argc < 2) {
		switch (current_flow) {
		default:
		case FLOW_NONE:
			flow = "none";
			break;
		case FLOW_SOFT:
			flow = "soft";
			break;
		case FLOW_HARD:
			flow = "hard";
			break;
		}
		printf("current flow: %s\n", flow);
		return 0;
	}

	switch (*argv[1]) {
	case 'n':
		current_flow = FLOW_NONE;
		break;
	case 's':
		current_flow = FLOW_SOFT;
		break;
	case 'h':
		current_flow = FLOW_HARD;
		break;
	default:
		printf("unknown flow type \"%s\"\n", argv[1]);
		return 1;
	}

	ios->set_flow(ios, current_flow);

	return 0;
}

static int current_dtr = 0;
static int current_rts = 0;

static int cmd_set_handshake_line(int argc, char *argv[])
{
	int enable;
	int ret = 0;
	int pin = 0;

	if (!ios->set_handshake_line) {
		printf("function not supported \"%s\"\n", argv[0]);
		return 1;
	}

	if (!strncmp(argv[0], "dtr", 3))
		pin = PIN_DTR;
	else if (!strncmp(argv[0], "rts", 3))
		pin = PIN_RTS;

	if (!pin) {
		printf("unknown pin \"%s\"\n", argv[0]);
		return 1;
	}

	if (argc < 2) {
		switch (pin) {
		case PIN_DTR:
			printf("current dtr: \"%d\"\n", current_dtr);
			break;
		case PIN_RTS:
			printf("current rts: \"%d\"\n", current_rts);
			break;
		}
		return 0;
	}

	if (!strncmp(argv[1], "1", strlen(argv[1]))) {
		enable = 1;
	} else if (!strncmp(argv[1], "0", strlen(argv[1]))) {
		enable = 0;
	} else {
		printf("unknown dtr state \"%s\"\n", argv[1]);
		return 1;
	}

	printf("setting %s: \"%d\"\n", argv[0], enable);
	ret = ios->set_handshake_line(ios, pin, enable);
	if (ret)
		return ret;

	switch (pin) {
	case PIN_DTR:
		current_dtr = enable;
		break;
	case PIN_RTS:
		current_rts = enable;
		break;
	}

	return 0;
}

static int cmd_exit(int argc, char *argv[])
{
	return MICROCOM_CMD_START;
}

static int cmd_break(int argc, char *argv[])
{
	ios->send_break(ios);
	return MICROCOM_CMD_START;
}

static int cmd_quit(int argc, char *argv[])
{
	microcom_exit(0);
	return 0;
}

static int cmd_help(int argc, char *argv[])
{
	struct cmd *cmd;

	if (argc == 1) {
		for_each_command(cmd) {
			if (cmd->info)
				printf("%s - %s\n", cmd->name, cmd->info);
			else
				printf("%s\n", cmd->name);
		}
	} else {
		microcom_cmd_usage(argv[1]);
	}

	return 0;
}

static int cmd_execute(int argc, char *argv[])
{
	if (argc < 2)
		return MICROCOM_CMD_USAGE;

	return do_script(argv[1]);
}

static int cmd_log(int argc, char *argv[])
{
	int ret;

	if (argc < 2)
		return MICROCOM_CMD_USAGE;

	ret = log_open(argv);

	return ret;
}

static int cmd_comment(int argc, char *argv[])
{
	return 0;
}

static struct cmd cmds[] = {
	{
		.name = "speed",
		.fn = cmd_speed,
		.info = "set terminal speed",
		.help = "speed <newspeed>"
	}, {
		.name = "exit",
		.fn = cmd_exit,
		.info = "exit from command processing",
	}, {
		.name = "flow",
		.fn = cmd_flow,
		.info = "set flow control",
		.help = "flow hard|soft|none",
	}, {
		.name = "dtr",
		.fn = cmd_set_handshake_line,
		.info = "set dtr value",
		.help = "dtr 1|0",
	}, {
		.name = "rts",
		.fn = cmd_set_handshake_line,
		.info = "set rts value",
		.help = "rts 1|0",
	}, {
		.name = "break",
		.fn = cmd_break,
		.info = "send break",
	}, {
		.name = "quit",
		.fn = cmd_quit,
		.info = "quit microcom",
	}, {
		.name = "help",
		.fn = cmd_help,
		.info = "show help",
	}, {
		.name = "x",
		.fn = cmd_execute,
		.info = "execute a script",
		.help = "x <scriptfile>",
	}, {
		.name = "log",
		.fn = cmd_log,
		.info = "log to file or |executable",
		.help = "log <logfile>",
	}, {
		.name = "#",
		.fn = cmd_comment,
		.info = "comment",
	},
};

void commands_init(void)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(cmds); i++)
		register_command(&cmds[i]);
}
