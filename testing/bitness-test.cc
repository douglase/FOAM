/*
 bitness-test.cc -- reminder about sizeof(type) vs sizeof(type*)
 
 Copyright (C) 2010 Tim van Werkhoven <t.i.m.vanwerkhoven@xs4all.nl>
 
 This file is part of FOAM.
 
 FOAM is free software: you can redistribute it and/or modify
 it under the terms of the GNU General Public License as published by
 the Free Software Foundation, either version 2 of the License, or
 (at your option) any later version.
 
 FOAM is distributed in the hope that it will be useful,
 but WITHOUT ANY WARRANTY; without even the implied warranty of
 MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 GNU General Public License for more details.
 
 You should have received a copy of the GNU General Public License
 along with FOAM.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <stdio.h>
#define __STDC_FORMAT_MACROS
#include <stdint.h>

int main() {
	int *dataint = new int(32);
	uint32_t *data32 = new uint32_t(32);
	uint64_t *data64 = new uint64_t(32);
	
	printf("sizeof int: %zu\n", sizeof (int));
	printf("sizeof int*: %zu\n", sizeof (int*));

	printf("sizeof *dataint: %zu\n", sizeof *dataint);
	printf("sizeof dataint: %zu\n", sizeof dataint);

	printf("sizeof uint32_t: %zu\n", sizeof (uint32_t));
	printf("sizeof uint32_t*: %zu\n", sizeof (uint32_t*));
	
	printf("sizeof uint64_t: %zu\n", sizeof (uint64_t));
	printf("sizeof uint64_t*: %zu\n", sizeof (uint64_t*));
	
	printf("This is a %zu bit system.\n", sizeof (int*)*8);
	return 0;
}
