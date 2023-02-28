#include <stdexcept>
#include <stdlib.h>
#include <stdint.h>
#include "utils.h"

static uint64_t checked_multiply(uint64_t x, uint64_t y)
{
	y *= x;
	if (y < x)
		throw std::runtime_error("value overflow");
	return y;
}

uint64_t parse_size(const std::string &val) 
{
	unsigned long tmp;
	const char *v;
	char *end;

	v = val.c_str();
	tmp = strtoul(v, &end, 0);
	if (end == v)
		throw std::runtime_error("no parameter");
	if (!tmp)
		throw std::runtime_error("must be non-zero");
	switch(*end) {
	case 'p': case 'P': /* petabytes */
		tmp = checked_multiply(tmp, 1024);
	case 't': case 'T': /* terabytes */
		tmp = checked_multiply(tmp, 1024);
	case 'g': case 'G': /* gigabytes */
		tmp = checked_multiply(tmp, 1024);
	case 'm': case 'M': /* megabytes */
		tmp = checked_multiply(tmp, 1024);
	case 'k': case 'K': /* kilobytes */
		tmp = checked_multiply(tmp, 1024);
		end++;
		break;
	case '\0':
		/* bytes */
		break;
	default:
		goto invalid_suffix;
	}

	if (*end)
		goto invalid_suffix;

	return tmp;

invalid_suffix:
	throw std::runtime_error("invalid suffix");
}
