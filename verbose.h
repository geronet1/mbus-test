/*
 * verbose.h
 *
 *  Created on: 17.12.2020
 *      Author: stefan
 */

#ifndef VERBOSE_H_
#define VERBOSE_H_

#include <stdbool.h>

int verbose(int level, const char * restrict, ...);
void setVerbose(int);
int getVerbose();

#endif /* VERBOSE_H_ */
