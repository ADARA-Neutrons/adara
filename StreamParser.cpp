#include "StreamParser.h"
#include "TransCompletePkt.h"
#include <iomanip>
#include <glog/logging.h>
#include <libxml/tree.h>
#include <boost/algorithm/string.hpp>


using namespace std;

namespace STS {


#define ADARA_IN_BUF_SIZE   0x1000000
//#define ADARA_OUT_BUF_SIZE  0x40000

StreamParser::StreamParser( int a_fd_in, int a_fd_out, string & a_adara_out_file, bool a_strict, bool a_gather_stats, uint32_t a_chunk_size, uint16_t a_buf_chunk_count )
  : Parser(ADARA_IN_BUF_SIZE,ADARA_IN_BUF_SIZE),
    m_total_charge(0.0), m_events_counted(0), m_events_uncounted(0),
    m_initialized(false), m_fd(a_fd_in), m_fd_out(a_fd_out), m_processing_state(WAITING_FOR_RUN_START), m_pkt_recvd(0), m_pulse_id(0), m_pulse_count(0),
    m_chunk_size(a_chunk_size), m_event_buf_chunk_count(a_buf_chunk_count), m_event_buf_write_thresh((a_buf_chunk_count-1)*a_chunk_size),
    m_ancillary_buf_chunk_count(10), m_ancillary_buf_write_thresh(10*a_chunk_size),
    m_strict(a_strict), m_gen_adara(false), m_gather_stats(a_gather_stats), m_skipped_pkt_count(0)
{
    if ( !a_adara_out_file.empty() )
    {
        m_gen_adara = true;
        m_ofs_adara.open( a_adara_out_file.c_str(), ios_base::out | ios_base::binary );
    }

    m_banks.reserve(300);
    m_pulse_info.times.reserve(m_chunk_size*m_ancillary_buf_chunk_count);
    m_pulse_info.freqs.reserve(m_chunk_size*m_ancillary_buf_chunk_count);
}

StreamParser::~StreamParser()
{
    if ( m_ofs_adara.is_open())
        m_ofs_adara.close();

    for ( vector<BankInfo*>::iterator ibg = m_banks.begin(); ibg != m_banks.end(); ++ibg )
        if ( *ibg )
            delete *ibg;
}


void
StreamParser::processStream()
{
    // If anything goes wrong with translation, an exception will be thrown to caller of this method

    if ( m_initialized )
        throw std::runtime_error("StreamParser::processStream() can not be called more than once.");

    initialize();
    m_initialized = true;

    m_pulse_info.start_time_nsec = 0;
    m_pulse_info.charge_stats.reset();
    m_pulse_info.freq_stats.reset();
    m_pkt_recvd             = 0;
    m_processing_state      = WAITING_FOR_RUN_START;

    while( m_processing_state < DONE_PROCESSING )
    {
        read( m_fd, ADARA_IN_BUF_SIZE );
    }
}

void
StreamParser::printStats( ostream &a_os ) const
{
    if ( m_gather_stats )
    {
        a_os << endl << "Pkt Type\t\t\tCount     \tTotal KB  \tMin Size  \tMax Size  " << endl;
        for ( map<uint32_t,PktStats>::const_iterator i = m_stats.begin(); i != m_stats.end(); ++i )
        {
            a_os << hex << setw(8) << left << i->first << "\t" << setw(12) << getPktName(i->first) << "\t" << dec << setw(10) << i->second.count << "\t" << setw(10)
                    << (i->second.total_size >> 10) << "\t" << setw(10) << i->second.min_pkt_size << "\t" << setw(10)
                    << i->second.max_pkt_size << endl;
        }
        a_os << endl << "Packets skipped: " << m_skipped_pkt_count << endl;
        a_os << "Pulse charge stats: " << m_pulse_info.charge_stats << endl;
        a_os << "Pulse freq stats: " << m_pulse_info.freq_stats << endl;
    }
    else
    {
        a_os << "Statistics were not gathered." << endl;
    }
}


const char*
StreamParser::getPktName( uint32_t a_pkt_id ) const
{
    switch ( a_pkt_id )
    {
    case 0x0000:
        return "Raw Event";
    case 0x0100:
        return "RTDL";
    case 0x0200:
        return "Src List";
    case 0x400000:
        return "Bank Event";
    case 0x400100:
        return "Beam Mon";
    case 0x400200:
        return "Pix Map";
    case 0x400300:
        return "Run Stat";
    case 0x400400:
        return "Run Info";
    case 0x400500:
        return "Tran Comp";
    case 0x400600:
        return "Cli Hello";
    case 0x400700:
        return "Stat Reset";
    case 0x400800:
        return "Sync";
    case 0x400900:
        return "Heart";
    case 0x400A00:
        return "Geom Info";
    case 0x400B00:
        return "Beam Info";
    case 0x800000:
        return "DDP";
    case 0x800100:
        return "VVP U32";
    case 0x800200:
        return "VVP DBL";
    case 0x800300:
        return "VVP STR";
    }

    return "Unknown";
}

//-----------------------------------------------------------------------------
// General ADARA packet processing methods
//-----------------------------------------------------------------------------

#define PKT_BIT_PIXELMAP    0x0001
#define PKT_BIT_RUNINFO     0x0002
#define PKT_BIT_BEAMINFO    0x0004
#define PKT_BIT_GEOMETRY    0x0008

#define PROCESS_IN_STATES(s)            \
    if ( m_processing_state & (s))      \
    {                                   \
        return Parser::rxPacket(pkt);   \
    }                                   \
    else                                \
    {                                   \
        if ( m_gather_stats )           \
            ++m_skipped_pkt_count;      \
        return false;                   \
    }

#define PROCESS_IN_STATES_ONCE(s,x)     \
    if (( m_processing_state & (s)) && !(m_pkt_recvd & x))    \
    {                                   \
        m_pkt_recvd |= x;               \
        return Parser::rxPacket(pkt);   \
    }                                   \
    else                                \
    {                                   \
        if ( m_gather_stats )           \
            ++m_skipped_pkt_count;      \
        return false;                   \
    }


bool
StreamParser::rxPacket( const ADARA::Packet &pkt )
{
    //cout << "pkt " << hex << pkt.type() << endl;

    if ( m_gather_stats )
        gatherStats( pkt );

    switch (pkt.type())
    {
    // These packets shall always be processed
    case ADARA::PacketType::RUN_STATUS_V0:
        return Parser::rxPacket(pkt);

    // These packets shall be processed ONCE during header and event processing
    // Note: these should arrive before event processing, but it is no guaranteed.
    case ADARA::PacketType::PIXEL_MAPPING_V0:
        PROCESS_IN_STATES_ONCE(PROCESSING_RUN_HEADER|PROCESSING_EVENTS,PKT_BIT_PIXELMAP)

    case ADARA::PacketType::RUN_INFO_V0:
        PROCESS_IN_STATES_ONCE(PROCESSING_RUN_HEADER|PROCESSING_EVENTS,PKT_BIT_RUNINFO)

    case ADARA::PacketType::GEOMETRY_V0:
        PROCESS_IN_STATES_ONCE(PROCESSING_RUN_HEADER|PROCESSING_EVENTS,PKT_BIT_GEOMETRY)

    case ADARA::PacketType::BEAMLINE_INFO_V0:
        PROCESS_IN_STATES_ONCE(PROCESSING_RUN_HEADER|PROCESSING_EVENTS,PKT_BIT_BEAMINFO)

    // These packets shall be processed during header & event processing
    case ADARA::PacketType::DEVICE_DESC_V0:
    case ADARA::PacketType::VAR_VALUE_U32_V0:
    case ADARA::PacketType::VAR_VALUE_DOUBLE_V0:
        PROCESS_IN_STATES(PROCESSING_RUN_HEADER|PROCESSING_EVENTS)

    // These packets shall only be processed during event processing
    case ADARA::PacketType::BANKED_EVENT_V0:
    case ADARA::PacketType::BEAM_MONITOR_EVENT_V0:
        PROCESS_IN_STATES(PROCESSING_EVENTS)

    default:
      if ( m_gather_stats )
          ++m_skipped_pkt_count;
        return false;
    }
}


//-----------------------------------------------------------------------------
// ADARA Run Status packet processing
//-----------------------------------------------------------------------------

bool
StreamParser::rxPacket( const ADARA::RunStatusPkt &pkt )
{
    //cout << "Processing RunStatusPkt (stat: " << pkt.status() << ")" << endl;
    
    writePacket( pkt );

    if ( pkt.status() == ADARA::RunStatus::NEW_RUN )
    {
        if ( m_processing_state == WAITING_FOR_RUN_START )
        {
            m_start_time = pkt.timestamp();
            m_processing_state = PROCESSING_RUN_HEADER;
        }
    }
    else if ( pkt.status() == ADARA::RunStatus::END_RUN )
    {
        if ( m_processing_state == PROCESSING_EVENTS )
        {
            m_end_time = pkt.timestamp();
            finalizeStreamProcessing();
            m_processing_state = DONE_PROCESSING;
        }
        else
            throw runtime_error("Recvd end run pkt while in wrong state.");
    }

    return false;
}

//-----------------------------------------------------------------------------
// ADARA Pixel Mapping packet processing
//-----------------------------------------------------------------------------

bool
StreamParser::rxPacket( const ADARA::PixelMappingPkt &pkt )
{
    //cout << "Processing PixelMappingPkt" << endl;

    writePacket( pkt );

    processPixelMappingPkt( pkt );

    // The receipt of a pixel mapping packet allows state to progress to event processing
    m_processing_state = PROCESSING_EVENTS;

    return false;
}

void
StreamParser::processPixelMappingPkt( const ADARA::PixelMappingPkt &pkt )
{
    const uint32_t *rpos = (const uint32_t*)pkt.payload();
    const uint32_t *epos = (const uint32_t*)(pkt.payload() + pkt.payload_length());
    uint32_t        bank_logical_id;
    uint16_t        bank_id;
    uint16_t        pix_count;

    while( rpos < epos )
    {
        bank_logical_id = *rpos++;
        bank_id = (uint16_t)(*rpos >> 16);
        pix_count = (uint16_t)(*rpos & 0xFFFF);
        rpos++;

        if ( bank_id >= m_banks.size() )
            m_banks.resize(bank_id+1,0);

        if ( !m_banks[bank_id] )
        {
            m_banks[bank_id] = makeBankInfo( bank_id, pix_count, m_event_buf_chunk_count*m_chunk_size, m_ancillary_buf_chunk_count*m_chunk_size );
        }

        rpos += pix_count;
    }
}

//-----------------------------------------------------------------------------
// ADARA Banked Event packet processing
//-----------------------------------------------------------------------------

bool
StreamParser::rxPacket( const ADARA::BankedEventPkt &pkt )
{
    //cout << "Processing BankedEventPkt" << endl;

    writePacket( pkt );

    processPulseID( pkt.pulseId() );

    const uint32_t *rpos = (const uint32_t*)pkt.payload();
    const uint32_t *epos = (const uint32_t*)(pkt.payload() + pkt.payload_length());

    //cout << "Bank Event Pkt, size: " << pkt.payload_length() << ", b: " << rpos << ", e: " << epos << endl;

    processPulseInfo( pkt );

    rpos += 4; // Skip over pulse info

    uint32_t source_id;
    uint32_t bank_count;
    uint32_t bank_id;
    uint32_t event_count;

    // Process banks per-source
    while ( rpos < epos )
    {
        source_id = *rpos++;
        rpos += 2; // TODO For now, skip over source-specific pulse info. Should eventually process this data
        bank_count = *rpos++;

        // Process events per-bank
        while( bank_count-- )
        {
            bank_id = *rpos++;
            event_count = *rpos++;
            processBankEvents( bank_id, event_count, rpos );
            rpos += event_count << 1;
        }
    }

    return false;
}

void
StreamParser::processPulseInfo( const ADARA::BankedEventPkt &pkt )
{
    // accumulate pulse charge
    m_total_charge += pkt.pulseCharge();
    m_pulse_info.charges.push_back(pkt.pulseCharge());
    m_pulse_info.charge_stats.push(pkt.pulseCharge());

    if ( m_pulse_info.start_time_nsec )
    {
        uint64_t pulse_time = timespec_to_nsec( pkt.timestamp()) - m_pulse_info.start_time_nsec;
        m_pulse_info.times.push_back( pulse_time*1.0e-9 );
        m_pulse_info.freqs.push_back(1.0e9/(pulse_time-m_pulse_info.last_time));
        m_pulse_info.freq_stats.push(m_pulse_info.freqs.back()); // Freq stats ignore first point since it can't be calculated
        m_pulse_info.last_time = pulse_time;
    }
    else
    {
        m_pulse_info.start_time_ts = pkt.timestamp();
        m_pulse_info.start_time_nsec = timespec_to_nsec( m_pulse_info.start_time_ts );
        m_pulse_info.last_time = 0;
        m_pulse_info.times.push_back(0);
        m_pulse_info.freqs.push_back(0);
    }

    // Is is time to write pulse info?
    if ( m_pulse_info.times.size() == m_ancillary_buf_write_thresh )
    {
        pulseBuffersReady( m_pulse_info );

        m_pulse_info.times.clear();
        m_pulse_info.freqs.clear();
        m_pulse_info.charges.clear();
    }
}

void
StreamParser::processBankEvents( uint32_t bank_id, uint32_t event_count, const uint32_t *rpos )
{
    //cout << "Bank " << bank_id << ", events: " << event_count << endl;

    if ( bank_id < m_banks.size() )
    {
        BankInfo *bi = m_banks[bank_id];
        size_t sz = bi->m_tof_buffer.size();

        bi->m_tof_buffer.resize( sz + event_count );
        bi->m_pid_buffer.resize( sz + event_count );

        float           *tof_ptr = &bi->m_tof_buffer[sz];
        uint32_t        *pid_ptr = &bi->m_pid_buffer[sz];
        const uint32_t  *epos = rpos + (event_count<<1);

        while ( rpos != epos )
        {
            *tof_ptr++ = *rpos++ * 0.1;
            *pid_ptr++ = *rpos++;
        }

        // Detect gaps in event data and fill event index if present
        if ( bi->m_last_pulse_with_data < ( m_pulse_count - 1 ))
        {
            uint64_t count = ( m_pulse_count - 1 ) - bi->m_last_pulse_with_data;

            handleBankPulseGap( *bi, count );
        }

        // Cache event index until large enough to write
        bi->m_index_buffer.push_back( bi->m_event_count );
        bi->m_event_count += event_count;

        bi->m_last_pulse_with_data = m_pulse_count;

        // Check to see if buffers are ready to write
        if ( bi->m_tof_buffer.size() >= m_event_buf_write_thresh || bi->m_index_buffer.size() >= m_ancillary_buf_write_thresh )
        {
            bankBuffersReady( *bi );

            bi->m_tof_buffer.clear();
            bi->m_pid_buffer.clear();
            bi->m_index_buffer.clear();
        }

        m_events_counted += event_count;
    }
    else
    {
        m_events_uncounted += event_count;
    }
}

void
StreamParser::handleBankPulseGap( BankInfo &a_bi, uint64_t a_count )
{
    // If the gap (count) is small enough (fits within size threshold),
    // then just insert values into index buffer
    if ( a_bi.m_index_buffer.size() + a_count < m_ancillary_buf_write_thresh )
        a_bi.m_index_buffer.insert( a_bi.m_index_buffer.end(), a_count, a_bi.m_event_count );
    else
    {
        // Otherwise, if the gap is too large - flush buffers & fill gap
        bankBuffersReady( a_bi );
        bankPulseGap( a_bi, a_count );

        a_bi.m_tof_buffer.clear();
        a_bi.m_pid_buffer.clear();
        a_bi.m_index_buffer.clear();
    }
}

//-----------------------------------------------------------------------------
// ADARA Beam Monitor packet processing
//-----------------------------------------------------------------------------

bool
StreamParser::rxPacket( const ADARA::BeamMonitorPkt &pkt )
{
    //cout << "Processing BeamMonitorPkt" << endl;

    writePacket( pkt );

    processPulseID( pkt.pulseId() );

    const uint32_t *rpos = (const uint32_t*)pkt.payload();
    const uint32_t *epos = (const uint32_t*)(pkt.payload() + pkt.payload_length());

    //cout << "Beam Monitor Pkt, size: " << pkt.payload_length() << ", b: " << rpos << ", e: " << epos << endl;

    // TODO What do we do with the pulse info from beam monitor packets?
    rpos += 4; // Skip over pulse info

    uint16_t  monitor_id;
    uint32_t event_count;

    // Process events per-bank
    while( rpos < epos )
    {
        monitor_id = *rpos >> 22;
        event_count = *rpos++ & 0x003FFFFF;

        // TODO What do we do with the pulse info from beam monitor packets?
        rpos += 2; // Skip over source info (source ID & tof offset)

        processMonitorEvents( monitor_id, event_count, rpos );
        rpos += event_count;
    }

    return false;
}

void
StreamParser::processMonitorEvents( uint16_t monitor_id, uint32_t event_count, const uint32_t *rpos )
{
    if ( monitor_id >= m_monitors.size())
        m_monitors.resize(monitor_id+1,0);

    MonitorInfo *mi = m_monitors[monitor_id];

    if ( !mi )
    {
        mi = makeMonitorInfo( monitor_id, m_event_buf_chunk_count*m_chunk_size );
        m_monitors[monitor_id] = mi;
    }

    size_t sz = mi->m_tof_buffer.size();

    mi->m_tof_buffer.resize( sz + event_count );

    float           *tof_ptr = &mi->m_tof_buffer[sz];
    const uint32_t  *epos = rpos + event_count;

    while ( rpos != epos )
    {
        // TODO What to do with bit 31?
        *tof_ptr++ = ((*rpos++)&0x1fffff) * 0.1; //TODO Revisit mask used here
    }

    // Detect gaps in event data and fill event index if present
    if ( mi->m_last_pulse_with_data < ( m_pulse_count - 1 ))
    {
        uint64_t count = ( m_pulse_count - 1 ) - mi->m_last_pulse_with_data;

        handleMonitorPulseGap( *mi, count );
    }

    // Cache event index until large enough to write
    mi->m_index_buffer.push_back( mi->m_event_count );
    mi->m_event_count += event_count;
    mi->m_last_pulse_with_data = m_pulse_count;

    // Check to see if buffers are ready to write
    if ( mi->m_tof_buffer.size() >= m_event_buf_write_thresh || mi->m_index_buffer.size() >= m_ancillary_buf_write_thresh )
    {
        monitorBuffersReady( *mi );

        mi->m_index_buffer.clear();
        mi->m_tof_buffer.clear();
    }
}

void
StreamParser::handleMonitorPulseGap( MonitorInfo &a_mi, uint64_t a_count )
{
    // If the gap (count) is small enough (fits within size threshold),
    // then just insert values into index buffer
    if ( a_mi.m_index_buffer.size() + a_count < m_ancillary_buf_write_thresh )
        a_mi.m_index_buffer.insert( a_mi.m_index_buffer.end(), a_count, a_mi.m_event_count );
    else
    {
        // Otherwise, if the gap is too large - flush current buffered data & fill index directly
        monitorBuffersReady( a_mi );
        monitorPulseGap( a_mi, a_count );

        a_mi.m_tof_buffer.clear();
        a_mi.m_index_buffer.clear();
    }
}

//-----------------------------------------------------------------------------
// ADARA Run Info packet processing
//-----------------------------------------------------------------------------

bool
StreamParser::rxPacket( const ADARA::RunInfoPkt &pkt )
{
    //cout << "Processing RunInfoPkt" << endl;

    writePacket( pkt );

    processRunInfo( pkt.info() );


    if ( m_strict )
    {
        // Verify we received all required fields in Run Info pkt

        string msg;
        if ( !m_facility_name.size())
            msg = "Required facility_name missing from RunInfo.";
        else if ( !m_proposal_id.size())
            msg = "Required proposal_id missing from RunInfo.";
        else if ( m_run_number == 0 )
            msg = "Required run_number missing from RunInfo.";

        if ( msg.size())
            throw TranslationException( TS_PERM_ERROR, msg );
    }

    return false;
}


//-----------------------------------------------------------------------------
// ADARA Geometry packet processing
//-----------------------------------------------------------------------------

bool
StreamParser::rxPacket( const ADARA::GeometryPkt &pkt )
{
    //cout << "Processing GeometryPkt" << endl;

    writePacket( pkt );

    processGeometry( pkt.info() );

    return false;
}


//-----------------------------------------------------------------------------
// ADARA Beam Line Info packet processing
//-----------------------------------------------------------------------------

bool
StreamParser::rxPacket( const ADARA::BeamlineInfoPkt &pkt )
{
    //cout << "Processing BeamlineInfoPkt" << endl;

    writePacket( pkt );

    m_short_name =  pkt.shortName();

    processBeamLineInfo( pkt.id(), pkt.shortName(), pkt.longName() );

    return false;
}

//-----------------------------------------------------------------------------
// ADARA Device Descriptor packet processing
//-----------------------------------------------------------------------------

bool
StreamParser::rxPacket( const ADARA::DeviceDescriptorPkt &pkt )
{
    //cout << "Processing DeviceDescriptorPkt" << endl;

    writePacket( pkt );

    processDeviceDescriptor( pkt.devId(), pkt.description());

    return false;
}


void
StreamParser::processDeviceDescriptor( Identifier a_device_id, const std::string & a_xml )
{
    xmlDocPtr doc = xmlReadMemory( a_xml.c_str(), a_xml.length(), 0, 0, 0 /* XML_PARSE_NOERROR | XML_PARSE_NOWARNING */ );
    if ( doc )
    {
        Identifier  pv_id = 0;
        string      pv_name;
        string      pv_units;
        PVType      pv_type = PVT_INT;
        short       found;

        //const char *name;

        xmlNode *root = xmlDocGetRootElement( doc );

        for ( xmlNode* lev1 = root->children; lev1 != 0; lev1 = lev1->next )
        {
            //name = (const char*)lev1->name;
            //cout << name << endl;

            if ( xmlStrcmp( lev1->name, (const xmlChar*)"process_variables" ) == 0)
            {
                xmlNode *pvnode;

                for ( xmlNode *pvsnode = lev1->children; pvsnode; pvsnode = pvsnode->next )
                {
                    //name = (const char*)pvsnode->name;
                    //cout << name << endl;
                    if ( xmlStrcmp( pvsnode->name, (const xmlChar*)"process_variable" ) == 0)
                    {
                        pv_units = "";
                        found = 0;

                        for ( pvnode = pvsnode->children; pvnode; pvnode = pvnode->next )
                        {
                            if ( xmlStrcmp( pvnode->name, (const xmlChar*)"pv_name" ) == 0)
                            {
                                found |= 1;
                                pv_name = (char*)pvnode->children->content;
                            }
                            else if ( xmlStrcmp( pvnode->name, (const xmlChar*)"pv_id" ) == 0)
                            {
                                found |= 2;
                                pv_id = boost::lexical_cast<Identifier>((char*)pvnode->children->content);
                            }
                            else if ( xmlStrcmp( pvnode->name, (const xmlChar*)"pv_type" ) == 0)
                            {
                                found |= 4;
                                pv_type = toPVType( (char*)pvnode->children->content );
                            }
                            else if ( xmlStrcmp( pvnode->name, (const xmlChar*)"pv_units" ) == 0)
                            {
                                pv_units = (char*)pvnode->children->content;
                            }
                        }

                        if ( found == 7 )
                        {
                            PVKey   key(a_device_id,pv_id);

                            if ( m_pvs.find(key) == m_pvs.end() )
                            {
                                m_pvs[key] = makePVInfo( pv_name, a_device_id, pv_id, pv_type, pv_units );
                                //cout << "Defining PV " << a_device_id << "." << pv_id << endl;
                            }
                        }
                        else
                        {
                           cout << "Skipping PV " << a_device_id << "." << pv_id << endl;
                        }
                    }
                }
            }
            else if ( xmlStrcmp( lev1->name, (const xmlChar*)"enumerations" ) == 0)
            {
                // TODO Handle enumeration definitions
            }
        }
        xmlFreeDoc( doc );
    }
}


//-----------------------------------------------------------------------------
// ADARA Variable (uint32) packet processing
//-----------------------------------------------------------------------------

bool
StreamParser::rxPacket( const ADARA::VariableU32Pkt &pkt )
{
    //cout << "Processing VariableU32Pkt" << endl;

    writePacket( pkt );

    pvValueUpdate<uint32_t>( pkt.devId(), pkt.varId(), pkt.value(), pkt.timestamp() );

    return false;
}

//-----------------------------------------------------------------------------
// ADARA Variable (double) packet processing
//-----------------------------------------------------------------------------

bool
StreamParser::rxPacket( const ADARA::VariableDoublePkt &pkt )
{
    //cout << "Processing VariableDoublePkt" << endl;

    writePacket( pkt );

    pvValueUpdate<double>( pkt.devId(), pkt.varId(), pkt.value(), pkt.timestamp() );

    return false;
}


//-----------------------------------------------------------------------------
// ADARA support methods
//-----------------------------------------------------------------------------

void
StreamParser::processPulseID( uint64_t a_pulse_id )
{
    if ( a_pulse_id != m_pulse_id || !m_pulse_count )
    {
        m_pulse_id = a_pulse_id;
        ++m_pulse_count;
    }
}

//-----------------------------------------------------------------------------
// Nexus/HDF5 support methods
//-----------------------------------------------------------------------------


void
StreamParser::finalizeStreamProcessing()
{
    // Write any remaining data in bank buffers

    for ( vector<BankInfo*>::iterator ibi = m_banks.begin(); ibi != m_banks.end(); ++ibi )
    {
        if ( !*ibi )
            continue;

        // Flush bank buffers
        if ( (*ibi)->m_tof_buffer.size() || (*ibi)->m_index_buffer.size() )
            bankBuffersReady( **ibi );

        // Detect gaps in bank data and fill event index if present
        if ( (*ibi)->m_last_pulse_with_data < m_pulse_count )
        {
            uint64_t count = m_pulse_count - (*ibi)->m_last_pulse_with_data;

            handleBankPulseGap( **ibi, count );
        }

        bankFinalize( **ibi );
    }

    // Write any remaining data in monitor buffers

    for ( vector<MonitorInfo*>::iterator imi = m_monitors.begin(); imi != m_monitors.end(); ++imi )
    {
        if ( !*imi )
            continue;

        // Flush monitor buffers
        if ( (*imi)->m_tof_buffer.size() || (*imi)->m_index_buffer.size() )
            monitorBuffersReady( **imi );

        // Detect gaps in monitor data and fill event index if present
        if ( (*imi)->m_last_pulse_with_data < m_pulse_count )
        {
            uint64_t count = m_pulse_count - (*imi)->m_last_pulse_with_data;

            handleMonitorPulseGap( **imi, count );
        }

        monitorFinalize( **imi );
    }
    

    // Write remaining pulse info and statistics

    if ( m_pulse_info.times.size())
        pulseBuffersReady( m_pulse_info );

    pulseFinalize( m_pulse_info );

    // Write any remaining data in PV buffers

    for ( map<PVKey,PVInfoBase*>::iterator ipv = m_pvs.begin(); ipv != m_pvs.end(); ++ipv )
    {
        if ( ipv->second->m_time_buffer.size() > 0 )
            ipv->second->flushBuffers( true );
    }

    // Let adapter do anything else it wants to
    finalize();
}

PVType
StreamParser::toPVType( const char *a_source ) const
{
    // Note: only integer, enum_xxx, and double are officially defined

    if ( boost::iequals( a_source, "integer" ))
        return PVT_INT;
    else if ( boost::iequals( a_source, "unsigned" ))
        return PVT_UINT;
    else if ( boost::iequals( a_source, "double" ))
        return PVT_DOUBLE;
    else if ( boost::iequals( a_source, "float" ))
        return PVT_FLOAT;
    else if ( boost::istarts_with( a_source, "enum_" ))
        return PVT_ENUM;

    LOG(ERROR) << "Invalid PV type: " << a_source << endl;
    throw runtime_error("Invalid PV type.");
}


} // End namespace STS
