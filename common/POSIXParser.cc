#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <time.h>

#include "POSIXParser.h"
#include "ADARAUtils.h"

// Need to reconcile different Logging Libraries between SMS and STC...! ;-b
// #include "Logging.h"
// LOGGER("SMS.POSIXParser");
// Call LOGGER_INIT();

using namespace ADARA;

// NOTE: This is POSIXParser::read()... ;-o
bool POSIXParser::read(int fd, std::string & log_info,
		unsigned int max_packets, unsigned int max_read)
{
	// DEBUG("read() entry");

	struct timespec start_parse_time;
	struct timespec end_parse_time;
	struct timespec start_read_time;
	struct timespec end_read_time;
	struct timespec start_time;
	struct timespec end_time;

	clock_gettime(CLOCK_REALTIME, &start_time);

	unsigned long len, to_read = max_read ?: ~0UL;
	unsigned int max_parse = ~0U;
	unsigned long to_parse = max_packets ?: max_parse;
	ssize_t rc;

	last_last_parse_elapsed_total = last_parse_elapsed_total;
	last_parse_elapsed_total = 0;

	last_last_read_elapsed_total = last_read_elapsed_total;
	last_read_elapsed_total = 0;

	last_last_loop_count = last_loop_count;
	last_loop_count = 0;

	last_last_read_count = last_read_count;
	last_read_count = 0;

	last_last_total_bytes = last_total_bytes;
	last_total_bytes = 0;

	last_last_total_packets = last_total_packets;
	last_total_packets = 0;

	last_last_elapsed = last_elapsed;

	// *No Memory Leaks* if POSIX::read() is called in a *Loop*...! ;-b
	log_info.clear();

	// DEBUG("read() to_read=" << to_read << " to_parse=" << to_parse);

	while (to_read && last_total_packets < to_parse && last_read_count < 10)
	{
		last_loop_count++;
		len = bufferFillLength();
		if (len > to_read)
			len = to_read;
		if (len) {
			last_read_count++;
			clock_gettime(CLOCK_REALTIME, &start_read_time);
			last_last_start_read_time = last_start_read_time;
			last_start_read_time = start_read_time;
			// NOTE: This is Standard C Library read()... ;-o
			rc = ::read(fd, bufferFillAddress(), len);
			clock_gettime(CLOCK_REALTIME, &end_read_time);
			last_last_end_read_time = last_end_read_time;
			last_end_read_time = end_read_time;
			last_last_read_elapsed = last_read_elapsed;
			last_read_elapsed =
				calcDiffSeconds( end_read_time, start_read_time );
			last_read_elapsed_total += last_read_elapsed;
			last_last_bytes_read = last_bytes_read;
			last_bytes_read = rc;
			if (rc < 0) {
				last_last_read_errno = last_read_errno;
				last_read_errno = errno;
				switch (errno) {
				case EINTR:
				case EAGAIN:
					/* We didn't get any data, but we're OK */
					log_info.append("read() no data but OK exit; ");
					clock_gettime(CLOCK_REALTIME, &end_time);
					last_elapsed = calcDiffSeconds( end_time, start_time );
					return true;
				case EPIPE:
				case ECONNRESET:
				case ETIMEDOUT:
				case EHOSTUNREACH:
				case ENETUNREACH:
					/* The host went away, but that shouldn't be fatal. */
					log_info.append("read() host went away exit; ");
					clock_gettime(CLOCK_REALTIME, &end_time);
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
			else {
				last_last_read_errno = last_read_errno;
				last_read_errno = 0;
			}

			if (rc == 0) {
				log_info.append("read() read returned 0 exit; ");
				clock_gettime(CLOCK_REALTIME, &end_time);
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

			last_total_bytes += rc;
			// DEBUG("read() Read " << rc << " Bytes"
				// << " to_read=" << to_read
				// << " last_total_bytes=" << last_total_bytes);
		}

		// Always parse as many packets as possible, don't leave any behind.
		clock_gettime(CLOCK_REALTIME, &start_parse_time);
		rc = bufferParse(log_info, max_parse);
		clock_gettime(CLOCK_REALTIME, &end_parse_time);
		last_last_parse_elapsed = last_parse_elapsed;
		last_parse_elapsed =
			calcDiffSeconds( end_parse_time, start_parse_time );
		last_parse_elapsed_total += last_parse_elapsed;
		last_last_pkts_parsed = last_pkts_parsed;
		last_pkts_parsed = rc;
		if (rc < 0) {
			log_info.append("read() bufferParse() error exit; ");
			clock_gettime(CLOCK_REALTIME, &end_time);
			last_elapsed = calcDiffSeconds( end_time, start_time );
			return false;
		}

		/* We've established that rc is not negative, and we cannot
		 * fit 2^21 packets in a buffer smaller than 32 GB, so we're
		 * safe to cast away the warning here.
		 */
		last_total_packets += (unsigned int) rc;
		// DEBUG("read() Parsed " << rc << " Packets"
			// << " last_total_packets=" << last_total_packets);
	}

	log_info.append("read() true exit; ");
	// DEBUG("read() true exit" << " last_total_bytes=" << last_total_bytes
		// << " last_total_packets=" << last_total_packets
		// << " last_read_count=" << last_read_count
		// << " last_loop_count=" << last_loop_count);

	clock_gettime(CLOCK_REALTIME, &end_time);
	last_elapsed = calcDiffSeconds( end_time, start_time );

	return true;
}
