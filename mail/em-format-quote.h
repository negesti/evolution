/*
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) version 3.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with the program; if not, see <http://www.gnu.org/licenses/>  
 *
 *
 * Authors:
 *		Jeffrey Stedfast <fejj@ximian.com>
 *
 * Copyright (C) 1999-2008 Novell, Inc. (www.novell.com)
 *
 */

#ifndef _EM_FORMAT_QUOTE_H
#define _EM_FORMAT_QUOTE_H

#include "mail/em-format.h"

typedef struct _EMFormatQuote EMFormatQuote;
typedef struct _EMFormatQuoteClass EMFormatQuoteClass;

#define EM_FORMAT_QUOTE_CITE (1<<0)
#define EM_FORMAT_QUOTE_HEADERS (1<<1)

struct _EMFormatQuote {
	EMFormat format;

	struct _EMFormatQuotePrivate *priv;

	char *credits;
	struct _CamelStream *stream;
	guint32 flags;

	guint32 text_html_flags;
	guint32 citation_colour;
};

struct _EMFormatQuoteClass {
	EMFormatClass format_class;
};

GType em_format_quote_get_type (void);

EMFormatQuote *em_format_quote_new (const char *credits, struct _CamelStream *stream, guint32 flags);

#endif /* !_EM_FORMAT_QUOTE_H */
