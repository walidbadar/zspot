/*
 * Copyright (c) 2026 Muhammad Waleed Badar
 *
 * SPDX-License-Identifier: GPL-3.0-only
 */

#include "lrc.h"

#include <ctype.h>
#include <stdlib.h>
#include <string.h>

void lrc_split(struct ui_lyrics *lyrics, char *text, bool synced, uint16_t max_lines)
{
	lyrics->synced = synced;
	lyrics->count = 0;

	for (char *line = text; line != NULL && lyrics->count < max_lines;) {
		char *next = strchr(line, '\n');
		uint32_t time_ms = 0;

		if (next != NULL) {
			*next++ = '\0';
		}

		if (synced) {
			char *end;
			uint32_t minutes;

			/* Lines without a time stamp are tags such as "[ar:...]". */
			if (line[0] != '[' || !isdigit((unsigned char)line[1])) {
				line = next;
				continue;
			}
			minutes = strtoul(&line[1], &end, 10);
			time_ms = minutes * 60000 + (uint32_t)(strtod(*end == ':' ? end + 1 : end,
								      &end) * 1000);
			line = *end == ']' ? end + 1 : end;
		}
		while (*line == ' ') {
			line++;
		}

		lyrics->lines[lyrics->count].time_ms = time_ms;
		lyrics->lines[lyrics->count].text = line;
		lyrics->count++;
		line = next;
	}
}
