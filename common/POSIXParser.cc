#include <string.h>
#include <unistd.h>
#include <errno.h>

#include "POSIXParser.h"

using namespace ADARA;

bool POSIXParser::read(int fd, unsigned int max_packets, unsigned int max_read)
{
	unsigned long len, to_read = max_read ?: ~0UL;
	unsigned int to_parse = max_packets ?: ~0U;
	ssize_t rc;

	while (to_read && to_parse) {
		len = bufferFillLength();
		if (len > to_read)
			len = to_read;
		if (len) {
			rc = ::read(fd, bufferFillAddress(), len);
	                if (rc < 0) {
				switch (errno) {
				case EINTR:
				case EAGAIN:
					/* We didn't get any data,
					 * but we're OK
					 */
					return true;
				case EPIPE:
				case ECONNRESET:
				case ETIMEDOUT:
				case EHOSTUNREACH:
				case ENETUNREACH:
					/* The host went away, but that
					 * shouldn't be fatal.
					 */
					return false;
				default:
					/* TODO consider if we should throw an
					 * exception at all.
					 */
					int err = errno;
					std::string msg("POSIXParser::read(): ");
					msg += strerror(err);
					throw std::runtime_error(msg);
				}
			}

			if (rc == 0)
				return false;

			/* Because the internal buffer size is represented by
			 * an unsigned int, and read() will not return more
			 * data than we request, we know that rc will fit,
			 * so cast away the conversion warning.
			 */
			bufferBytesAppended((unsigned int) rc);
			to_read -= rc;
		}

		rc = bufferParse(to_parse);
		if (rc < 0)
			return false;

		/* We've established that rc is not negative, and we cannot
		 * fit 2^21 packets in a buffer smaller than 32 GB, so we're
		 * safe to cast away the warning here.
		 */
		to_parse -= (unsigned int) rc;
	}

	return true;
}
