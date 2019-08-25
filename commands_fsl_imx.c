/*
 * Copyright (C) 2010 Sascha Hauer <s.hauer@pengutronix.de>
 *
 * This program is free software; you can redistribute it and/or modify it under
 * the terms of the GNU General Public License as published by the Free Software
 * Foundation; either version 2 of the License, or (at your option) any later
 * version.
 */
#include <stdio.h>
#include <sys/select.h>
#include <stdint.h>
#include <sys/stat.h>
#include "microcom.h"

static int available(int fd)
{
	fd_set rfds;
	struct timeval tv = {
		.tv_sec = 0,
		.tv_usec = 1
	};
	int ret;

	/* Watch stdin (fd 0) to see when it has input. */
	FD_ZERO(&rfds);
	FD_SET(fd, &rfds);
	ret = select(fd + 1, &rfds, NULL, NULL, &tv);
	if (ret == -1) {
		perror("select");
		return -EINVAL;
	}

	if (ret)
		return 1;
	else
		return 0;
}

static int get_ack(int fd)
{
	unsigned char expect[] = { 0x56, 0x78, 0x78, 0x56 };
	int i, ret;
	unsigned char r;

	for (i = 0; i < sizeof(expect); i++) {
		ret = read(ios->fd, &r, 1);
		if (ret == -1) {
			perror("read failed\n");
			return -1;
		}
		if (r != expect[i])
			return -1;;
	}

	return 0;
}

static int sync_com(int fd)
{
	unsigned char buf = 0x1;
	int ret, i;

	for (i = 0; i < 16; i++) {
		while (1) {
			printf("wr %d\n", i++);
			ret = write(ios->fd, &buf, 1);
			if (ret < 0)
				perror("write");
			usleep(100000);
			if (available(fd))
				break;
		}
		ret = get_ack(fd);
		if (!ret)
			return ret;
		printf("no ack. try again\n");
	}

	printf("failed to connect\n");

	return -EINVAL;
}

static int read_mem(int fd, uint32_t address, void *_buf, int size, int accesssize)
{
	unsigned char buf[] = {0x1, 0x1,			/* read command */
				0x0, 0x0, 0x0, 0x0,		/* address */
				0x20,				/* data size */
				0x0, 0x0, 0x0, 0x0,		/* count */
				0x0, 0x0, 0x0, 0x0, 0x0};	/* fill */
	int i = 0, ret;
	uint8_t *buf8 = _buf;
	uint16_t *buf16 = _buf;
	uint32_t *buf32 = _buf;

	buf[2] = (address >> 24) & 0xff;
	buf[3] = (address >> 16) & 0xff;
	buf[4] = (address >>  8) & 0xff;
	buf[5] = (address >>  0) & 0xff;

	switch (accesssize) {
	case 8:
	case 16:
	case 32:
		buf[6] = accesssize;
		break;
	default:
		return -EINVAL;
	}

	size -= 1;

	buf[7] = (size >> 24) & 0xff;
	buf[8] = (size >> 16) & 0xff;
	buf[9] = (size >>  8) & 0xff;
	buf[10] = (size >>  0) & 0xff;

	for (i = 0; i < sizeof(buf); i++) {
		ret = write(ios->fd, &buf[i], 1);
		if (ret < 0) {
			perror("write");
			return -EINVAL;
		}
	}

	usleep(100000);

	ret = get_ack(fd);
	if (ret)
		return ret;

	i = 0;

	while (i < size) {
		uint8_t temp;

		switch (accesssize) {
		case 8:
			ret = read(fd, buf8, 1);
			if (ret < 0)
				return -EINVAL;
			buf8++;
			i++;
			break;
		case 16:
			ret = read(fd, &temp, 1);
			if (ret < 0)
				return -EINVAL;
			*buf16 = temp;
			ret = read(fd, &temp, 1);
			if (ret < 0)
				return -EINVAL;
			*buf16 |= temp << 8;
			buf16++;
			i += 2;
			break;
		case 32:
			ret = read(fd, &temp, 1);
			if (ret < 0)
				return -EINVAL;
			*buf32 = temp;
			ret = read(fd, &temp, 1);
			if (ret < 0)
				return -EINVAL;
			*buf32 |= temp << 8;
			ret = read(fd, &temp, 1);
			if (ret < 0)
				return -EINVAL;
			*buf32 |= temp << 16;
			ret = read(fd, &temp, 1);
			if (ret < 0)
				return -EINVAL;
			*buf32 |= temp << 24;
			buf32++;
			i += 4;
			break;
		}
	}
	return 0;
}

#define DISP_LINE_LEN       16

static int memory_display(char *addr, unsigned long offs, unsigned long nbytes, int size)
{
	unsigned long linebytes, i;
	unsigned char *cp;

	/* Print the lines.
	 *
	 * We buffer all read data, so we can make sure data is read only
	 * once, and all accesses are with the specified bus width.
	 */
	do {
		char	linebuf[DISP_LINE_LEN];
		uint	*uip = (uint   *)linebuf;
		ushort	*usp = (ushort *)linebuf;
		u_char	*ucp = (u_char *)linebuf;
		uint	count = 52;

		printf("%08lx:", offs);
		linebytes = (nbytes > DISP_LINE_LEN) ? DISP_LINE_LEN : nbytes;

		for (i = 0; i < linebytes; i += size) {
			if (size == 4) {
				count -= printf(" %08x", (*uip++ = *((uint *)addr)));
			} else if (size == 2) {
				count -= printf(" %04x", (*usp++ = *((ushort *)addr)));
			} else {
				count -= printf(" %02x", (*ucp++ = *((u_char *)addr)));
			}
			addr += size;
			offs += size;
		}

		while(count--)
			printf(" ");

		cp = (unsigned char *)linebuf;
		for (i = 0; i < linebytes; i++) {
			if ((*cp < 0x20) || (*cp > 0x7e))
				printf(".");
			else
				printf("%c", *cp);
			cp++;
		}
		printf("\n");
		nbytes -= linebytes;
	} while (nbytes > 0);

	return 0;
}

static int md(int argc, char *argv[])
{
	void *buf;
	uint32_t addr;
	int accesssize = 32;
	int size = 256;
	int ret = 0;

	if (argc < 2)
		return 1;

	addr = strtoul(argv[1], NULL, 0);

	buf = malloc(size);

	ret = read_mem(ios->fd, addr, buf, size, accesssize);
	if (ret)
		goto out;

	memory_display(buf, 0x0, size, accesssize >> 3);
out:
	free(buf);

	return ret;
}

static int write_mem(uint32_t address, uint32_t val, int accesssize)
{
	unsigned char buf[] = { 0x2, 0x2,	/* write command */
		0x0, 0x0, 0x0, 0x0,		/* address */
		0x0,				/* data size */
		0x0, 0x0, 0x0, 0x0,		/* fill */
		0x0, 0x0, 0x0, 0x0,		/* value */
		0x0,				/* fill */
	};
	unsigned char expect[] = {0x12, 0x8a, 0x8a, 0x12};
	int i, ret;
	unsigned char r;

	buf[2] = (address >> 24) & 0xff;
	buf[3] = (address >> 16) & 0xff;
	buf[4] = (address >>  8) & 0xff;
	buf[5] = (address >>  0) & 0xff;

	switch (accesssize) {
	case 8:
		buf[11] = val & 0xff;
		buf[6] = accesssize;
		break;
	case 16:
		buf[11] = (val >> 8) & 0xff;
		buf[12] = val & 0xff;
		buf[6] = accesssize;
		break;
	case 32:
		buf[11] = (val >> 24) & 0xff;
		buf[12] = (val >> 16) & 0xff;
		buf[13] = (val >>  8) & 0xff;
		buf[14] = (val >>  0) & 0xff;
		buf[6] = accesssize;
		break;
	default:
		return -EINVAL;
	}

	for (i = 0; i < ARRAY_SIZE(buf); i++) {
		ret = write(ios->fd, &buf[i], 1);
		if (ret < 0) {
			perror("write");
			return -EINVAL;
		}
	}

	ret = get_ack(ios->fd);
	if (ret)
		return ret;

	for (i = 0; i < sizeof(expect); i++) {
		ret = read(ios->fd, &r, 1);
		if (ret == -1) {
			perror("read failed\n");
			return -1;
		}
		if (r != expect[i])
			return -1;;
	}

	return 0;
}

static int mw(int argc, char *argv[])
{
	uint32_t addr, val;
	int accesssize = 32;

	if (argc < 3)
		return 1;

	if (!strcmp(argv[0], "mwb"))
		accesssize = 8;
	if (!strcmp(argv[0], "mwh"))
		accesssize = 16;

	addr = strtoul(argv[1], NULL, 0);
	val = strtoul(argv[2], NULL, 0);

	write_mem(addr, val, accesssize);

	return 0;
}

static int do_header(uint32_t addr)
{
	int i, ret;
	unsigned char buf[] = {
		0x20, 0x0, 0x0, 0x80,
		0x0, 0x0, 0x0, 0x0,
		0x0, 0x0, 0x0, 0x0,
		0x0, 0x0, 0x0, 0x0,
		0x0, 0x0, 0x0, 0x0,
		0x0, 0x0, 0x0, 0x0,
		0x0, 0x0, 0x0, 0x0,
		0x0, 0x0, 0x0, 0x0,
	};
	buf[0] = (addr >>  0) & 0xff;
	buf[1] = (addr >>  8) & 0xff;
	buf[2] = (addr >> 16) & 0xff;
	buf[3] = (addr >> 24) & 0xff;

	for (i = 0; i < ARRAY_SIZE(buf); i++) {
		ret = write(ios->fd, &buf[i], 1);
		if (ret < 0) {
			perror("write");
			return -EINVAL;
		}
	}

	return 0;
}

static int upload_file(uint32_t address, char *name, unsigned char type)
{
	uint32_t size;
	int upfd, ret, i;
	unsigned char buf[] = { 0x4, 0x4,	/* upload command */
		0x0, 0x0, 0x0, 0x0,		/* address */
		0x0,				/* fill */
		0x0, 0x0, 0x0, 0x0,		/* filesize */
		0x0, 0x0, 0x0, 0x0,		/* fill */
		0xaa,				/* filetype */
	};
	struct stat stat;

	upfd = open(name, O_RDONLY | O_CLOEXEC);
	if (upfd < 0) {
		perror("open");
		return 1;
	}

	ret = fstat(upfd, &stat);
	if (ret) {
		perror("stat");
		return 1;
	}

	size = stat.st_size;
	size += 0x20;

	buf[2] = (address >> 24) & 0xff;
	buf[3] = (address >> 16) & 0xff;
	buf[4] = (address >>  8) & 0xff;
	buf[5] = (address >>  0) & 0xff;

	buf[7] = (size >> 24) & 0xff;
	buf[8] = (size >> 16) & 0xff;
	buf[9] = (size >>  8) & 0xff;
	buf[10] = (size >>  0) & 0xff;

	for (i = 11; i < 16; i++)
		buf[i] = type;

	for (i = 0; i < ARRAY_SIZE(buf); i++) {
		ret = write(ios->fd, &buf[i], 1);
		if (ret < 0) {
			perror("write");
			return -EINVAL;
		}
	}

	ret = get_ack(ios->fd);
	if (ret) {
		printf("no ack\n");
		return ret;
	}

	do_header(address + 0x20);

	for (i = 0; i < size - 0x20; i++) {
		unsigned char tmp;
		ret = read(upfd, &tmp, 1);
		if (ret != 1) {
			perror("read");
			goto out;
		}
		ret = write(ios->fd, &tmp, 1);
		if (ret != 1) {
			perror("write");
			goto out;
		}
		if (!(i  % 65536))
			printf("\n    ");
		if (!((i + 1) % 1024))
			printf("#");
		fflush(stdout);
	}

	printf("\n");

out:
	return 0;
}

static int upload(int argc, char *argv[])
{
	uint32_t address;
	unsigned char buf[] = { 0x5, 0x5,	/* status command */
		0x0, 0x0, 0x0, 0x0,
		0x0, 0x0, 0x0, 0x0,
		0x0, 0x0, 0x0, 0x0,
		0x0, 0x0,
	};
	int i, ret, type = 0;

	if (argc < 3)
		return 1;

	if (argc > 3) {
		type = strtoul(argv[3], NULL, 0);
		printf("image type: 0x%02x\n", type);
	}

	address = strtoul(argv[1], NULL, 0);
	upload_file(address, argv[2], type);

	for (i = 0; i < ARRAY_SIZE(buf); i++) {
		ret = write(ios->fd, &buf[i], 1);
		if (ret < 0) {
			perror("write");
			return -EINVAL;
		}
	}

	if (type == 0xaa)
		return MICROCOM_CMD_START;

	return 0;
}

static int fsl_connect(int argc, char *argv[])
{
	int ret;

	printf("trying to connect...\n");
	ret = sync_com(ios->fd);
	if (ret) {
		printf("connect failed\n");
		return -11;
	}
	printf("done\n");
	return 0;
}

static void fsl_sniff_memwrite(void)
{
	unsigned char buf[15];
	int i;
	uint32_t addr, val;

	printf("mw ");
	for (i = 0; i < 15; i++) {
		read(ios->fd, &buf[i], 1);
	}

	addr = (buf[1] << 24) | (buf[2] << 16) | (buf[3] << 8) | (buf[4] << 0);
	val = (buf[10] << 24)| (buf[11] << 16) | (buf[12] << 8) | (buf[13] << 0);
	printf("0x%08x 0x%08x\n", addr, val);
}

static void fsl_sniff_6(void)
{
	unsigned char buf[15];
	int i;

	printf("cmd6\n");
	for (i = 0; i < 15; i++) {
		read(ios->fd, &buf[i], 1);
		printf("%02x ", buf[i]);
	}
	printf("\n");
}

static void fsl_sniff_sts(void)
{
	unsigned char buf[15];
	int i;

	printf("cmd get status\n");
	for (i = 0; i < 15; i++) {
		read(ios->fd, &buf[i], 1);
		printf("%02x ", buf[i]);
	}
	printf("\n");
}

static void fsl_sniff_memread(void)
{
	printf("md (not implemented)\n");
}

static void fsl_sniff_upload(void)
{
	unsigned char buf[15];
	uint32_t addr, size;
	int i;

	printf("upload ");

	for (i = 0; i < 15; i++) {
		read(ios->fd, &buf[i], 1);
	}
	addr = (buf[1] << 24) | (buf[2] << 16) | (buf[3] << 8) | (buf[4] << 0);
	size = (buf[6] << 24) | (buf[7] << 16) | (buf[8] << 8) | (buf[9] << 0);

	printf(" adr: 0x%08x size: 0x%08x type 0x%02x ", addr, size, buf[14]);

	switch(buf[14]) {
	case 0xaa:
		printf("(application)\n");
		break;
	case 0xee:
		printf("(dcd)\n");
		break;
	default:
		printf("(unknown)\n");
		break;
	}

	for (i = 0; i < size; i++) {
		unsigned char tmp;
		read(ios->fd, &tmp, 1);
		printf("%02x ", tmp);
		if (!((i + 1) % 32))
			printf("\n");
	}
	printf("\n");
}

static int fsl_sniff(int argc, char *argv[])
{
	while (1) {
		unsigned char cmd;
		read(ios->fd, &cmd, 1);
		switch (cmd) {
		case 0x2:
			fsl_sniff_memwrite();
			break;
		case 0x1:
			fsl_sniff_memread();
			break;
		case 0x4:
			fsl_sniff_upload();
			break;
		case 0x5:
			fsl_sniff_sts();
			break;
		case 0x6:
			fsl_sniff_6();
			break;
		default:
			printf("unknown cmd 0x%02x\n", cmd);
			break;
		};
	}

	return 0;
}

static struct cmd cmds[] = {
	{
		.name = "md",
		.fn = md,
		.info = "Display memory (i.MX specific)",
		.help = "md <address>",
	}, {
		.name = "mw",
		.fn = mw,
		.info = "write memory (i.MX specific)",
		.help = "mw <address> <value>",
	},  {
		.name = "mwb",
		.fn = mw,
		.info = "write memory byte (i.MX specific)",
		.help = "mwb <address> <value>",
	},  {
		.name = "mwh",
		.fn = mw,
		.info = "write memory 2 byte (i.MX specific)",
		.help = "mwh <address> <value>",
	}, {
		.name = "upload",
		.fn = upload,
		.info = "upload image (i.MX specific)",
		.help = "upload <address> <file> [<imagetype>]\n"
			"use imagetype = 0xaa for application images",
	}, {
		.name = "connect",
		.fn = fsl_connect,
		.info = "sync communication to Processor (i.MX specific)",
		.help = "connect",
	}, {
		.name = "sniff",
		.fn = fsl_sniff,
		.info = "sniff and dissect communication from ATK (i.MX specific)",
		.help = "sniff",
	},
};

void commands_fsl_imx_init(void)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(cmds); i++)
		register_command(&cmds[i]);
}

