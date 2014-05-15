#include <string.h>
#include <unistd.h>
#include <errno.h>

#include "POSIXParser.h"

// Need to reconcile different Logging Libraries between SMS and STS...! ;-b
// #include "Logging.h"
// static LoggerPtr logger(Logger::getLogger("SMS.POSIXParser"));

using namespace ADARA;

// NOTE: This is POSIXParser::read()... ;-o
bool POSIXParser::read(int fd, unsigned int max_packets, unsigned int max_read)
{
	// DEBUG("read() entry");

	unsigned long len, to_read = max_read ?: ~0UL;
	unsigned int max_parse = ~0U;
	unsigned long to_parse = max_packets ?: max_parse;
	unsigned long total_bytes = 0;
	unsigned int total_packets = 0;
	unsigned int read_count = 0;
	unsigned int loop_count = 0;
	ssize_t rc;

	// DEBUG("read() to_read=" << to_read << " to_parse=" << to_parse);

	while (to_read && total_packets < to_parse && read_count < 10) {
		loop_count++;
		len = bufferFillLength();
		if (len > to_read)
			len = to_read;
		if (len) {
			read_count++;
			// NOTE: This is Standard C Library read()... ;-o
			rc = ::read(fd, bufferFillAddress(), len);
			if (rc < 0) {
				switch (errno) {
				case EINTR:
				case EAGAIN:
					/* We didn't get any data,
					 * but we're OK
					 */
					// DEBUG("read() no data but OK exit");
					return true;
				case EPIPE:
				case ECONNRESET:
				case ETIMEDOUT:
				case EHOSTUNREACH:
				case ENETUNREACH:
					/* The host went away, but that
					 * shouldn't be fatal.
					 */
					// DEBUG("read() host went away exit");
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

			if (rc == 0) {
				// DEBUG("read() read returned 0 exit");
				return false;
			}

			/* Because the internal buffer size is represented by
			 * an unsigned int, and read() will not return more
			 * data than we request, we know that rc will fit,
			 * so cast away the conversion warning.
			 */
			bufferBytesAppended((unsigned int) rc);
			to_read -= rc;

			total_bytes += rc;
			// DEBUG("read() Read " << rc << " Bytes"
				// << " to_read=" << to_read
				// << " total_bytes=" << total_bytes);
		}

		// Always parse as many packets as possible, don't leave any behind.
		rc = bufferParse(max_parse);
		if (rc < 0) {
			// DEBUG("read() bufferParse() error exit");
			return false;
		}

		/* We've established that rc is not negative, and we cannot
		 * fit 2^21 packets in a buffer smaller than 32 GB, so we're
		 * safe to cast away the warning here.
		 */
		total_packets += (unsigned int) rc;
		// DEBUG("read() Parsed " << rc << " Packets"
			// << " total_packets=" << total_packets);
	}

	// DEBUG("read() true exit"
		// << " total_bytes=" << total_bytes
		// << " total_packets=" << total_packets
		// << " read_count=" << read_count
		// << " loop_count=" << loop_count);

	return true;
}
