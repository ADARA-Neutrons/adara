#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <time.h>

#include "POSIXParser.h"
#include "ADARAUtils.h"

// Need to reconcile different Logging Libraries between SMS and STS...! ;-b
// #include "Logging.h"
// static LoggerPtr logger(Logger::getLogger("SMS.POSIXParser"));

using namespace ADARA;

// NOTE: This is POSIXParser::read()... ;-o
bool POSIXParser::read(int fd, std::string & log_info,
		unsigned int max_packets, unsigned int max_read)
{
	// DEBUG("read() entry");

	struct timespec start_time;
	struct timespec end_time;

	clock_gettime(CLOCK_REALTIME, &start_time);

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
		last_last_loop_count = last_loop_count;
		last_loop_count = loop_count;
		len = bufferFillLength();
		if (len > to_read)
			len = to_read;
		if (len) {
			read_count++;
			last_last_read_count = last_read_count;
			last_read_count = read_count;
			// NOTE: This is Standard C Library read()... ;-o
			rc = ::read(fd, bufferFillAddress(), len);
			last_last_bytes_read = last_bytes_read;
			last_bytes_read = rc;
			if (rc < 0) {
				switch (errno) {
				case EINTR:
				case EAGAIN:
					/* We didn't get any data, but we're OK */
					log_info = "read() no data but OK exit";
					clock_gettime(CLOCK_REALTIME, &end_time);
					last_last_elapsed = last_elapsed;
					last_elapsed = calcDiffSeconds( end_time, start_time );
					return true;
				case EPIPE:
				case ECONNRESET:
				case ETIMEDOUT:
				case EHOSTUNREACH:
				case ENETUNREACH:
					/* The host went away, but that shouldn't be fatal. */
					log_info = "read() host went away exit";
					clock_gettime(CLOCK_REALTIME, &end_time);
					last_last_elapsed = last_elapsed;
					last_elapsed = calcDiffSeconds( end_time, start_time );
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
				log_info = "read() read returned 0 exit";
				clock_gettime(CLOCK_REALTIME, &end_time);
				last_last_elapsed = last_elapsed;
				last_elapsed = calcDiffSeconds( end_time, start_time );
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
			last_last_total_bytes = last_total_bytes;
			last_total_bytes = total_bytes;
			// DEBUG("read() Read " << rc << " Bytes"
				// << " to_read=" << to_read
				// << " total_bytes=" << total_bytes);
		}

		// Always parse as many packets as possible, don't leave any behind.
		rc = bufferParse(log_info, max_parse);
		last_last_pkts_parsed = last_pkts_parsed;
		last_pkts_parsed = rc;
		if (rc < 0) {
			log_info.append("; read() bufferParse() error exit");
			clock_gettime(CLOCK_REALTIME, &end_time);
			last_last_elapsed = last_elapsed;
			last_elapsed = calcDiffSeconds( end_time, start_time );
			return false;
		}

		/* We've established that rc is not negative, and we cannot
		 * fit 2^21 packets in a buffer smaller than 32 GB, so we're
		 * safe to cast away the warning here.
		 */
		total_packets += (unsigned int) rc;
		last_last_total_packets = last_total_packets;
		last_total_packets = total_packets;
		// DEBUG("read() Parsed " << rc << " Packets"
			// << " total_packets=" << total_packets);
	}

	log_info = "read() true exit";
	// DEBUG("read() true exit" << " total_bytes=" << total_bytes
		// << " total_packets=" << total_packets
		// << " read_count=" << read_count
		// << " loop_count=" << loop_count);

	clock_gettime(CLOCK_REALTIME, &end_time);
	last_last_elapsed = last_elapsed;
	last_elapsed = calcDiffSeconds( end_time, start_time );

	return true;
}
