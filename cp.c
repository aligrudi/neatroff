#include "xroff.h"

static int cp_backed = -1;

int cp_next(void)
{
	int ret = cp_backed >= 0 ? cp_backed : in_next();
	cp_backed = -1;
	return ret;
}

void cp_back(int c)
{
	cp_backed = c;
}
