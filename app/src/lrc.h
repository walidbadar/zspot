/*
 * Copyright (c) 2026 Muhammad Waleed Badar
 *
 * SPDX-License-Identifier: GPL-3.0-only
 */

/*
 * Lyrics as text into lines: plain ones, or LRC ("[mm:ss.xx] words") with
 * the time each line is sung at.
 */

#ifndef ZSPOT_SAMPLE_LRC_H_
#define ZSPOT_SAMPLE_LRC_H_

#include <stdbool.h>
#include <stdint.h>

#include "ui.h"

/**
 * Splits @p text into the lines of @p lyrics, which has room for
 * @p max_lines; further ones are dropped. The lines point into @p text,
 * which is cut up for it. With @p synced, lines without a time stamp, such
 * as the tags "[ar:...]", are left out.
 */
void lrc_split(struct ui_lyrics *lyrics, char *text, bool synced, uint16_t max_lines);

#endif /* ZSPOT_SAMPLE_LRC_H_ */
