#include <stdexcept>
#include <string.h>
#include <libxml/tree.h>
#include "NxGen.h"

using namespace std;


NxGen::NxGen( int a_fd_in, string & a_adara_out_file, string &a_nexus_out_file, bool a_strict, bool a_gather_stats, unsigned long a_chunk_size, unsigned short a_event_buf_chunk_count, unsigned short a_ancillary_buf_chunk_count, unsigned long a_cache_size, unsigned short a_compression_level )
 : StreamParser( a_fd_in, a_adara_out_file, a_strict, a_gather_stats, a_chunk_size*a_event_buf_chunk_count, a_chunk_size*a_ancillary_buf_chunk_count ),
   m_gen_nexus(false), m_nexus_filename(a_nexus_out_file), m_chunk_size(a_chunk_size), m_h5nx(a_compression_level), m_pulse_info_slab_size(0)
{
    if ( !a_nexus_out_file.empty() )
    {
        m_gen_nexus = true;
        m_h5nx.H5NXset_cache_size( a_cache_size );
    }
}

NxGen::~NxGen()
{
    // TODO Delete allocated objects, closeout file if still open
}


SFS::PVInfoBase*
NxGen::makePVInfo( const string & a_name, SFS::Identifier a_device_id, SFS::Identifier a_pv_id, SFS::PVType a_type, const std::string & a_units )
{
    switch ( a_type )
    {
    case SFS::PVT_INT:  // TODO ADARA only supports uint32_t currently
    case SFS::PVT_ENUM:
    case SFS::PVT_UINT:
        return new NxPVInfo<uint32_t>( a_name, a_device_id, a_pv_id, a_type, a_units, *this );
    case SFS::PVT_FLOAT: // TOSO ADARA only supports double currently
    case SFS::PVT_DOUBLE:
        return new NxPVInfo<double>( a_name, a_device_id, a_pv_id, a_type, a_units, *this );
    }

    LOG(ERROR) << "makePVInfo invalid PV type: " << a_type << endl;
    throw runtime_error("makePVInfo failed.");
}

SFS::BankInfo*
NxGen::makeBankInfo( uint16_t a_id, uint16_t a_pixel_count, uint32_t a_buf_reserve, uint32_t a_idx_buf_reserve )
{
    NxBankInfo* bi = new NxBankInfo( a_id, a_pixel_count, a_buf_reserve, a_idx_buf_reserve );

    if ( m_gen_nexus)
    {
        // Instrument bank group
        string instr_path = string("entry/instrument/") + bi->m_name;
        makeGroup( instr_path, "NXdetector" );
        makeDataset( instr_path, "event_time_offset", NeXus::FLOAT32, "microsecond" );
        makeDataset( instr_path, "event_id", NeXus::UINT32 );
        makeDataset( instr_path, "event_index", NeXus::UINT64 );

        // Event data group
        string event_path = string("entry/") + bi->m_eventname;
        makeGroup( event_path, "NXevent_data" );
        makeLink( instr_path + "/event_time_offset", event_path + "/event_time_offset" );
        makeLink( instr_path + "/event_id", event_path + "/event_id" );
        makeLink( instr_path + "/event_index", event_path + "/event_index" );

        // Link pulse time to bank event times
        makeLink( "entry/DASlogs/frequency/time", instr_path + "/event_time_zero" );
        makeLink( "entry/DASlogs/frequency/time", event_path + "/event_time_zero" );
    }

    return bi;
}


SFS::MonitorInfo*
NxGen::makeMonitorInfo( uint16_t a_id, uint32_t a_buf_reserve )
{
    NxMonitorInfo* mi = new NxMonitorInfo( a_id, a_buf_reserve );

    if ( m_gen_nexus)
    {
        // create instrument/bank# group
        string path = "entry/" + mi->m_name;

        makeGroup( path, "NXentry" );
        makeDataset( path, "event_time_offset", NeXus::FLOAT32, "microsecond" );
        makeDataset( path, "event_index", NeXus::UINT64 );

        makeLink( "entry/DASlogs/frequency/time", path + "/event_time_zero" );
    }

    return mi;
}


void
NxGen::initialize()
{
    if (!m_gen_nexus)
        return;

    m_h5nx.H5NXcreate_file( m_nexus_filename );

    makeGroup( "entry", "NXentry" );
    makeGroup( "entry/instrument", "NXinstrument" );
    makeGroup( "entry/DASlogs", "NXgroup" );
    makeGroup( "entry/DASlogs/frequency", "NXgroup" );
    makeGroup( "entry/DASlogs/proton_charge", "NXgroup" );

    makeDataset( "entry/DASlogs/frequency", "time", NeXus::FLOAT64, "seconds" );
    makeDataset( "entry/DASlogs/frequency", "value", NeXus::FLOAT64, "Hz" );

    makeDataset( "entry/DASlogs/proton_charge", "value", NeXus::FLOAT64, "picoCoulombs" );
    makeLink( "entry/DASlogs/frequency/time", "entry/DASlogs/proton_charge/time" );
}


void
NxGen::finalize()
{
    if (!m_gen_nexus)
        return;

    struct timespec start = getStartTime();
    struct timespec end = getEndTime();
    float duration = calcDiffSeconds( end, start );

    writeScalar( "entry/", "duration", duration, "second" );
    writeScalar( "entry/", "raw_frames", m_pulse_info.charge_stats.count(), "" );
    writeScalar( "entry/", "total_counts", m_events_counted, "" );
    writeScalar( "entry/", "total_uncounted_counts", m_events_uncounted, "" );
    writeScalar( "entry/", "proton_charge", m_total_charge, "picoCoulomb" );

    string time = timeToISO8601( start );
    m_h5nx.H5NXmake_dataset_string( "entry/", "start_time", time );

    time = timeToISO8601( end );
    m_h5nx.H5NXmake_dataset_string( "entry/", "end_time", time );

    m_h5nx.H5NXclose_file();
}


void
NxGen::processBeamLineInfo( const std::string &a_id, const std::string &a_shortname, const std::string &a_longname )
{
    if (!m_gen_nexus)
        return;

    writeString( "entry/instrument", "beamline", a_id );

    if ( a_longname.size())
    {
        writeString( "entry/instrument", "name", a_longname );

        if ( a_shortname.size())
            writeStringAttribute( "entry/instrument/name", "short_name", a_shortname );
    }
}


void
NxGen::processRunInfo( const std::string & a_xml )
{
    if (!m_gen_nexus)
        return;

    xmlDocPtr doc = xmlReadMemory( a_xml.c_str(), a_xml.size(), 0, 0, 0 );
    if ( doc )
    {
        string group_path = "entry";

        for ( xmlNode *node = xmlDocGetRootElement(doc)->children; node; node = node->next )
        {
            if ( xmlStrcmp( node->name, (const xmlChar*)"run_number" ) == 0)
            {
                m_run_number = boost::lexical_cast<unsigned long>( (char*)node->children->content );

                writeString( group_path, "run_number", (char*)node->children->content );
                writeString( group_path, "entry_identifier", (char*)node->children->content );
            }
            else if ( xmlStrcmp( node->name, (const xmlChar*)"proposal_id" ) == 0)
            {
                m_proposal_id = (char*)node->children->content;
                writeString( group_path, "experiment_identifier", (char*)node->children->content );
            }
            else if ( xmlStrcmp( node->name, (const xmlChar*)"run_title" ) == 0)
            {
                writeString( group_path, "title", (char*)node->children->content );
            }
            //else if (xmlStrcmp( node->name, (const xmlChar*) "instrument_name") == 0)
            //{
            //    writeString( "entry/instrument", "beamline", (char*)node->children->content );
            //}
            else if (xmlStrcmp( node->name, (const xmlChar*) "facility_name") == 0)
            {
                m_facility_name = (char*) node->children->content;
            }
            else if ( xmlStrcmp( node->name, (const xmlChar*)"sample" ) == 0)
            {
                makeGroup( "entry/sample", "NXsample" );

                for ( xmlNode *sample_node = node->children; sample_node; sample_node = sample_node->next )
                {
                    if ( xmlStrcmp( sample_node->name, (const xmlChar*)"id" ) == 0)
                    {
                        writeString( "entry/sample", "identifier", (char*)sample_node->children->content );
                    }
                    else if ( xmlStrcmp( sample_node->name, (const xmlChar*)"name" ) == 0)
                    {
                        writeString( "entry/sample", "name", (char*)sample_node->children->content );
                    }
                    else if ( xmlStrcmp( sample_node->name, (const xmlChar*)"nature" ) == 0)
                    {
                        writeString( "entry/sample", "nature", (char*)sample_node->children->content );
                    }
                    else if ( xmlStrcmp( sample_node->name, (const xmlChar*)"chemical_formula" ) == 0)
                    {
                        writeString( "entry/sample", "chemical_formula", (char*)sample_node->children->content );
                    }
                    else if ( xmlStrcmp( sample_node->name, (const xmlChar*)"environment" ) == 0)
                    {
                        writeString( "entry/sample", "environment", (char*)sample_node->children->content );
                    }
                }
            }
            else if ( xmlStrcmp( node->name, (const xmlChar*)"users" ) == 0)
            {
                size_t user_count = 0;
                string path;

                for ( xmlNode *user_node = node->children; user_node; user_node = user_node->next )
                {
                    if ( xmlStrcmp( user_node->name, (const xmlChar*)"user" ) == 0)
                    {
                        path = group_path + "/user" + boost::lexical_cast<string>(++user_count);
                        makeGroup( path, "NXuser" );

                        for ( xmlNode *uinfo_node = user_node->children; uinfo_node; uinfo_node = uinfo_node->next )
                        {
                            if ( xmlStrcmp( uinfo_node->name, (const xmlChar*)"id" ) == 0)
                                writeString( path, "facility_user_id", (char*)uinfo_node->children->content );
                            if ( xmlStrcmp( uinfo_node->name, (const xmlChar*)"name" ) == 0)
                                writeString( path, "name", (char*)uinfo_node->children->content );
                            else if (xmlStrcmp( uinfo_node->name, (const xmlChar*)"role" ) == 0)
                                writeString( path, "role", (char*)uinfo_node->children->content );
                        }
                    }
                }
            }
        }

        xmlFreeDoc( doc );
    }
}

void
NxGen::processGeometry( const std::string & a_xml )
{
    if (!m_gen_nexus)
        return;

    makeGroup( "entry/instrument/instrument_xml", "NXnote" );
    writeString( "entry/instrument/instrument_xml", "description", "XML contents of the instrument IDF" );
    writeString( "entry/instrument/instrument_xml", "type", "text/xml" );
    writeString( "entry/instrument/instrument_xml", "data", a_xml );
}

void
NxGen::pulseBuffersReady( SFS::PulseInfo &a_pulse_info )
{
    if (!m_gen_nexus)
        return;

    writeSlab( "entry/DASlogs/frequency/time", a_pulse_info.times, m_pulse_info_slab_size );
    writeSlab( "entry/DASlogs/frequency/value", a_pulse_info.freqs, m_pulse_info_slab_size );
    writeSlab( "entry/DASlogs/proton_charge/value", a_pulse_info.charges, m_pulse_info_slab_size );

    m_pulse_info_slab_size += a_pulse_info.times.size();
}


void
NxGen::pulseFinalize( SFS::PulseInfo &a_pulse_info )
{
    if (!m_gen_nexus)
        return;

    writeScalar( "entry/DASlogs/frequency", "minimum_value", a_pulse_info.freq_stats.min(), "seconds" );
    writeScalar( "entry/DASlogs/frequency", "maximum_value", a_pulse_info.freq_stats.max(), "seconds" );
    writeScalar( "entry/DASlogs/frequency", "average_value", a_pulse_info.freq_stats.mean(), "seconds" );
    writeScalar( "entry/DASlogs/frequency", "average_value_error", a_pulse_info.freq_stats.stdDev(), "seconds" );

    writeScalar( "entry/DASlogs/proton_charge", "minimum_value", a_pulse_info.charge_stats.min(), "picoCoulombs" );
    writeScalar( "entry/DASlogs/proton_charge", "maximum_value", a_pulse_info.charge_stats.max(), "picoCoulombs" );
    writeScalar( "entry/DASlogs/proton_charge", "average_value", a_pulse_info.charge_stats.mean(), "picoCoulombs" );
    writeScalar( "entry/DASlogs/proton_charge", "average_value_error", a_pulse_info.charge_stats.stdDev(), "picoCoulombs" );
}


void
NxGen::bankBuffersReady( SFS::BankInfo &a_bank )
{
    if (!m_gen_nexus)
        return;

    NxBankInfo *bi = dynamic_cast<NxBankInfo*>(&a_bank);

    if ( bi )
    {
        writeSlab( bi->m_tof_slab_path, a_bank.m_tof_buffer, bi->m_event_slab_size );
        writeSlab( bi->m_pid_slab_path, a_bank.m_pid_buffer, bi->m_event_slab_size );

        bi->m_event_slab_size += a_bank.m_tof_buffer.size();

        writeSlab( bi->m_index_slab_path, a_bank.m_index_buffer, bi->m_index_slab_size );

        bi->m_index_slab_size += a_bank.m_index_buffer.size();
    }
}

void
NxGen::bankPulseGap( SFS::BankInfo &a_bank, uint64_t a_count )
{
    if (!m_gen_nexus)
        return;

    NxBankInfo *bi = dynamic_cast<NxBankInfo*>(&a_bank);

    if ( bi )
    {
        fillSlab( bi->m_index_slab_path, bi->m_event_count, a_count, bi->m_index_slab_size );
        bi->m_index_slab_size += a_count;
    }
}

void
NxGen::bankFinalize( SFS::BankInfo &a_bank )
{
    if (!m_gen_nexus)
        return;

    NxBankInfo *bi = dynamic_cast<NxBankInfo*>(&a_bank);

    if ( bi )
    {
        string total_path = "entry/instrument/" + bi->m_name;
        writeScalar( total_path, "total_counts", bi->m_event_count, "" );
        makeLink( total_path + "/total_counts", "entry/" + bi->m_eventname + "/total_counts" );
    }
}


void
NxGen::monitorBuffersReady( SFS::MonitorInfo &a_monitor )
{
    if (!m_gen_nexus)
        return;

    NxMonitorInfo *mi = dynamic_cast<NxMonitorInfo*>(&a_monitor);

    if ( mi )
    {
        writeSlab( mi->m_tof_slab_path, a_monitor.m_tof_buffer, mi->m_event_slab_size );
        mi->m_event_slab_size += a_monitor.m_tof_buffer.size();

        writeSlab( mi->m_index_slab_path, a_monitor.m_index_buffer, mi->m_index_slab_size );
        mi->m_index_slab_size += a_monitor.m_index_buffer.size();
    }
}

void
NxGen::monitorPulseGap( SFS::MonitorInfo &a_monitor, uint64_t a_count )
{
    if (!m_gen_nexus)
        return;

    NxMonitorInfo *mi = dynamic_cast<NxMonitorInfo*>(&a_monitor);

    if ( mi )
    {
        fillSlab( mi->m_index_slab_path, mi->m_event_count, a_count, mi->m_index_slab_size );
        mi->m_index_slab_size += a_count;
    }
}

void
NxGen::monitorFinalize( SFS::MonitorInfo &a_monitor )
{
    if (!m_gen_nexus)
        return;

    NxMonitorInfo *mi = dynamic_cast<NxMonitorInfo*>(&a_monitor);

    if ( mi )
    {
        writeScalar( string("entry/") + mi->m_name, "total_counts", mi->m_event_count, "" );
    }
}

NeXus::NXnumtype
NxGen::toNxType( SFS::PVType a_type ) const
{
    switch( a_type )
    {
    case SFS::PVT_INT:
    case SFS::PVT_ENUM:
        return NeXus::INT32;
    case SFS::PVT_UINT: return NeXus::UINT32;
    case SFS::PVT_FLOAT: return NeXus::FLOAT32;
    case SFS::PVT_DOUBLE: return NeXus::FLOAT64;
    }

    LOG(ERROR) << "Invalid PV type: " << a_type << endl;
    throw runtime_error("Invalid PV type.");
}

void
NxGen::makeGroup( const string &a_path, const string &a_type )
{
    if ( m_h5nx.H5NXmake_group( a_path, a_type ) != SUCCEED )
    {
        LOG(ERROR) << "H5NXmake_group FAILED for " << a_path << endl;
        throw runtime_error("H5NXmake_group failed.");
    }
}


void
NxGen::makeDataset( const std::string &dataset_path, const std::string &dataset_name, int nxdatatype, const string a_units )
{
    if ( m_h5nx.H5NXcreate_dataset_extend( dataset_path, dataset_name, nxdatatype, m_chunk_size ) != SUCCEED )
    {
        LOG(ERROR) << "H5NXcreate_dataset_extend FAILED:" << dataset_path << "," << dataset_name << endl;
        throw runtime_error("H5NXcreate_dataset_extend failed.");
    }

    if ( a_units.size() )
    {
        if ( m_h5nx.H5NXmake_attribute_string( dataset_path + "/" + dataset_name, "units", a_units ) != SUCCEED )
        {
            LOG(ERROR) << "H5NXmake_attribute_string FAILED:" << dataset_path << "," << dataset_name << endl;
            throw runtime_error("H5NXmake_attribute_string failed.");
        }
    }
}

void
NxGen::makeLink( const string &source_path, const string &dest_name )
{
    if ( m_h5nx.H5NXmake_link(source_path, dest_name) != SUCCEED )
    {
        LOG(ERROR) << "H5NXmake_link FAILED:" << source_path << " -> " << dest_name << endl;
        throw runtime_error("H5NXmake_link failed.");
    }
}

void
NxGen::writeString( const string &a_path, const string &a_dataset, const string &a_value )
{
    if ( m_h5nx.H5NXmake_dataset_string( a_path, a_dataset, a_value ) != SUCCEED )
    {
        LOG(ERROR) << "H5NXmake_dataset_string FAILED for " << a_path << "/" << a_dataset << ":" << a_value << endl;
        throw runtime_error("H5NXmake_dataset_string failed.");
    }
}


void
NxGen::writeStringAttribute( const string &a_path, const string &a_attrib, const string &a_value )
{
    if ( m_h5nx.H5NXmake_attribute_string( a_path, a_attrib, a_value ) != SUCCEED )
    {
        LOG(ERROR) << "H5NXmake_attribute_string FAILED for " << a_path << "/" << a_attrib << ":" << a_value << endl;
        throw runtime_error("H5NXmake_attribute_string failed.");
    }
}


template<typename T>
void
NxGen::writeScalar( const std::string & a_path, const std::string & a_name, T a_value, const std::string & a_units )
{
    if ( m_h5nx.H5NXmake_dataset_scalar( a_path, a_name, a_value ) != SUCCEED )
    {
        LOG(ERROR) << "H5NXmake_dataset_scalar FAILED for " << a_path << "/" << a_name << ":" << a_value << endl;
        throw runtime_error("H5NXmake_dataset_scalar failed.");
    }

    if ( a_units.size())
    {
        if ( m_h5nx.H5NXmake_attribute_string( a_path + "/" + a_name, "units", a_units ) != SUCCEED )
        {
            LOG(ERROR) << "H5NXmake_attribute_string FAILED for units: " << a_path << "/" << a_name << ":" << a_units << endl;
            throw runtime_error("H5NXmake_attribute_string failed.");
        }
    }
}



