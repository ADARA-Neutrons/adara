// A simple program to read an ADARA stream file and replay it at a
// user-defined speed (which is assumed to be something less than
// "as fast as the hard disk will go".
//
// Default is to read from stdin and write to stdout
// (We're assuming the data will be piped to 'nc' or something similar
// so that other ADARA live clients can connect to it.)
//
// Works by parsing the ADARA stream just enough to pull the timestamps
// from the packet headers and then comparing them to the computer's
// actual time.


#include "ADARAParser.h"
#include "ADARAUtil.h"

#include <time.h>

#include <iostream>
#include <fstream>

class SimpleParser : public ADARA::Parser
{
public:
    SimpleParser( std::ostream &outstream = std::cout, float tf = 1.0);

    // Override some protected base class functions with 
    // public versions
    uint8_t *bufferFillAddress(void) const {
        return ADARA::Parser::bufferFillAddress();
    }

    unsigned int bufferFillLength(void) const {
        return ADARA::Parser::bufferFillLength();
    }

    void bufferBytesAppended(unsigned int count) {
        return ADARA::Parser::bufferBytesAppended( count);
    }
    
    int bufferParse(std::string & log_info, unsigned int max_packets = 0) {
        return ADARA::Parser::bufferParse( log_info, max_packets );
    }

protected:
    
    using ADARA::Parser::rxPacket;
    virtual bool rxPacket( const ADARA::Packet &pkt);
    
private:
    std::ostream &m_out;
    
    // This requires some explanation:  We want to control how fast the
    // packets are replayed.  Each packet has a timestamp, so we know how
    // fast they were originally generated.  We need to subtract the first
    // packet's timestamp from the current packet's timestamp to get a
    // delta.  We then subtract the time the first packet was output from the
    // current time and get an 'output delta'.  We then multiply the 'output
    // delta' by the time factor.  When this modified output delta is greater
    // than the timestamp delta, it's time to write out the packet.
    //
    // Multiplying the original output delta by the time factor allows us to
    // adjust the rate the packets are written. Thus, if the factor is 2, we
    // will wind up with packets being output at twice the speed they were
    // originally generated.  A factor of 3, and the packets come out 3 times
    // faster.  A factor of 0.5, and the packets come out at *half* their
    // original speed.
    float m_timeFactor;
    struct timespec m_firstPktTime;
    struct timespec m_firstOutTime;
    
    // Starts false, set to true after we process the first packet
    // (Used so we know we have to initialize m_first*Time)
    bool m_firstPktReceived;  

    time_t m_last_stats;  // the last time the stats were printed
#define STATS_INTERVAL 5 // in seconds
    unsigned int m_packets_processed;
    unsigned long m_total_packets_processed;
    unsigned long m_total_bytes_processed;
  
    // returns elapsed time in nanoseconds!
    inline int64_t fastElapsed( const timespec &start) const
    {
        struct timespec now;
        clock_gettime(CLOCK_REALTIME, &now);
        return fastElapsed( start, now);
    }
        
    inline int64_t fastElapsed( const timespec &start, const timespec &end) const
    {
        int64_t rv;
        if (end.tv_sec == start.tv_sec)
        {
            rv = end.tv_nsec - start.tv_nsec;
        }
        else
        {
            rv = ( ( end.tv_sec - start.tv_sec ) * NANO_PER_SECOND_LL ) +
                  ( end.tv_nsec - start.tv_nsec );
        }
        return rv;
    }
    
    // returns true if it's time to output the next packet
    bool timesUp( const struct timespec &pktTime);
    
    // prints (to stderr) some basic stats about the packet processing...
    void printStats( );
    
    // No copy constructor or assignment operator
    SimpleParser( const SimpleParser &p);
    SimpleParser & operator= (const SimpleParser &p);
    
};


SimpleParser::SimpleParser( std::ostream &outstream, float tf)
    : m_out( outstream),
      m_timeFactor( tf),
      m_firstPktReceived( false),
      m_last_stats( time(NULL)),
      m_packets_processed( 0),
      m_total_packets_processed( 0),
      m_total_bytes_processed( 0)
{
}

bool SimpleParser::rxPacket( const ADARA::Packet &pkt)
{
    if (m_firstPktReceived == false)
    {
       // This is the first packet - need to init the m_first*Time variables
       m_firstPktTime = pkt.timestamp();
       clock_gettime( CLOCK_REALTIME, &m_firstOutTime);
       m_firstPktReceived = true;
    }
    
    // Wait until it's time to output this packet   
    while (! timesUp( pkt.timestamp()))
    {
        usleep( 100); // TODO: do a better job of figuring out how long to sleep
    }
    
    // Output the packet data
    m_out.write( (const char *)pkt.packet(), pkt.packet_length());
    
    // Keep track of some packet statistics
    m_packets_processed++;
    m_total_packets_processed++;
    m_total_bytes_processed += pkt.packet_length();
    
    if (time( NULL) > (m_last_stats + STATS_INTERVAL))
    {
        printStats();
        m_packets_processed = 0;
        m_last_stats = time(NULL);
    }
     
    return false;  // false indicates no error
}


#if 1

bool SimpleParser::timesUp( const struct timespec &pktTime)
{
    bool rv = false;
    int64_t pktDelta = fastElapsed( m_firstPktTime, pktTime);
    int64_t outputDelta = fastElapsed( m_firstOutTime);  
    
    outputDelta *= m_timeFactor;
    
    // The packet strem will occasionally have packets 'from the past'
    // (ie: times prior to m_firstPktTime).  This happens at run start when 
    // SMS dumps out all the device descriptor and variable value packets
    // (which it has cached).  There's other places where this can
    // happen, too.  Regardless, if the pktDelta is < 0, then we will
    // output the packet without waiting.
    
    if ( (pktDelta < 0) || (outputDelta > pktDelta))
    {
        rv = true;
    }
    
    return rv;
}
#else
// this will cause us to output packets as fast as possible (for debugging)
bool SimpleParser::timesUp( const timespec &outTime)
{
    return true;
}
#endif

// prints (to stderr) some basic stats about the packet processing...
void SimpleParser::printStats()
{
    unsigned packetsPerSec = m_packets_processed / (time(NULL) - m_last_stats);
    std::cerr << "Packets per second: " << packetsPerSec
              << " / Total packets: " << m_total_packets_processed
              << " / Total MB: " << m_total_bytes_processed / (float)(1024*1024)
              << std::endl;
}

// TODO: Need to add code to handle a command line option for the time factor
int main(int argc, char **argv)
{
    SimpleParser p;
    
    uint8_t * parseBuf;
    unsigned int bufLen;
    
    if (argc > 1)
    {
        std::cerr << "Usage:" << std::endl
                  << "  " << argv[0] << " takes no parameters.  It reads from"
                  << " stdin and writes to stdout." << std::endl;
        return 1;
    }
   
    while (std::cin.good())
    {
        parseBuf = p.bufferFillAddress();
        bufLen = p.bufferFillLength();
        
        std::cin.read( (char *)parseBuf, bufLen);
        
        // Some basic error checking code that will probably never get called
        if (std::cin.gcount() == 0)
        {
            std::cerr << "Read 0 bytes from standard input.  Position: "
                      << std::cin.tellg() << std::endl;
            usleep(100 * 1000);  // sleep for a bit and hope the whatever
                                 // caused the problem sorts itself out...
        }
        
        p.bufferBytesAppended(std::cin.gcount());

		std::string log_info;
        p.bufferParse(log_info);
		std::cerr << "Buffer Parse returned: " << log_info << std::endl;
    }
    
    return 0;
}
