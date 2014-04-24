#ifndef __POSIX_PARSER_H
#define __POSIX_PARSER_H

#include "ADARAParser.h"

namespace ADARA {

class POSIXParser : public Parser {
public:
	POSIXParser(unsigned int inital_buffer_size = 1024 * 1024,
		    unsigned int max_pkt_size = 8 * 1024 * 1024) :
		Parser(inital_buffer_size, max_pkt_size) { }

	/* Returns false if we hit EOF or a callback asked to stop. We return
	 * true if we got we got EAGAIN/EINTR from reading the fd. We throw
	 * exceptions on error, but may hold those until we complete all
	 * packets in the buffer. The max_read parameter, if non-zero,
	 * limits the amount of maximum amount of data read and parsed
	 * from the file descriptor. The max_packets parameter, if non-zero,
	 * limits the number of packets parsed. read() will stop if either
	 * limit is reached.
	 */
        bool read(int fd, unsigned int max_packets = 0,
		  unsigned int max_read = 0);

	using ADARA::Parser::rxPacket;
};

} /* namespace ADARA */

#endif /* __POSIX_PARSER_H */
