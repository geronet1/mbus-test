/*
 * verbose.c
 *
 *  Created on: 17.12.2020
 *      Author: stefan
 */

#include "verbose.h"
#include <stdbool.h>
#include <stdarg.h>
#include <stdio.h>

int Verbose = 0;

void setVerbose(int setting)
{
	Verbose = setting;
}

int verbose(int level, const char * restrict format, ...)
{
	if (Verbose < level)
		return 0;

	va_list args;
	va_start(args, format);
	int ret = vprintf(format, args);
	va_end(args);

	return ret;
}

int getVerbose()
{
	return Verbose;
}
