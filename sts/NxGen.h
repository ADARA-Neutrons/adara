#ifndef NXGEN_H
#define NXGEN_H

#include <string>
#include <vector>
#include <set>
#include <syslog.h>
#include <time.h>
#include "h5nx.hpp"
#include "stsdefs.h"
#include "StreamParser.h"
#include "ADARAUtils.h"
#include <boost/lexical_cast.hpp>


#define CHARGE_UNITS "picoCoulombs"
#define FREQ_UNITS "Hz"
#define TIME_SEC_UNITS "second"
#define TIME_USEC_UNITS "microsecond"


/*! \brief ADARA Stream Adapter class that provides NeXus file generation
 *
 * The NxGen class is a stream adapter subclass that specializes the ADARA StreamParser class for creating NeXus output
 * files.
 */
class NxGen : public STS::StreamParser
{
private:

    /// BankInfo subclass that adds Nexus-required attributes
    class NxBankInfo : public STS::BankInfo
    {
    public:
        /// NxBankInfo constructor
        NxBankInfo
        (
            uint16_t a_id,              ///< [in] ID of detector bank
            uint32_t a_buf_reserve,     ///< [in] Event buffer initial capacity
            uint32_t a_idx_buf_reserve, ///< [in] Index buffer initial capacity
            NxGen &a_nxgen              ///< [in] Parent NxGen instance
        )
        :
            BankInfo(a_id, a_buf_reserve, a_idx_buf_reserve),
            m_nexus_init(false),
            m_event_cur_size(0),
            m_index_cur_size(0),
            m_nxgen(a_nxgen)
        {
            m_name = std::string("bank")
                + boost::lexical_cast<std::string>(a_id);

            // Entry Instrument Path

            m_instr_path = m_nxgen.m_instrument_path + "/" + m_name;

            // Detector Event Paths

            m_eventname = m_name + std::string("_events");

            m_event_path = m_nxgen.m_entry_path + "/" + m_eventname;

            m_tof_path = m_instr_path + "/" + m_nxgen.m_tof_name;

            m_pid_path = m_instr_path + "/" + m_nxgen.m_pid_name;

            m_index_path = m_instr_path + "/" + m_nxgen.m_index_name;

            m_time_path = m_nxgen.m_daslogs_freq_path
                + std::string("/time");

            // Detector Histogram Paths

            m_histoname = m_name;

            m_histo_path = m_nxgen.m_entry_path + "/" + m_histoname;

            m_data_path = m_instr_path + "/" + m_nxgen.m_data_name;

            m_histo_pid_path = m_instr_path + "/"
                + m_nxgen.m_histo_pid_name;

            m_tofbin_path = m_instr_path + "/" + m_nxgen.m_tofbin_name;
        }

        std::string             m_name;             ///< Name of bank in Nexus file
        std::string             m_instr_path;       ///< Nexus path to "NXdetector" instrument group
        std::string             m_eventname;        ///< Name of bank events entry in Nexus file
        std::string             m_event_path;       ///< Nexus path to "NXevent_data" group
        std::string             m_tof_path;         ///< Nexus path to TOF dataset
        std::string             m_pid_path;         ///< Nexus path to PID dataset
        std::string             m_index_path;       ///< Nexus path to event index dataset
        std::string             m_time_path;        ///< Nexus path to Pulse Time array
        std::string             m_histoname;        ///< Name of bank histo entry in Nexus file
        std::string             m_histo_path;       ///< Nexus path to histo "NXdata" group
        std::string             m_data_path;        ///< Nexus path to Histo data dataset
        std::string             m_histo_pid_path;   ///< Nexus path to Histo PID dataset
        std::string             m_tofbin_path;      ///< Nexus path to Histo TOF Bin dataset
        bool                    m_nexus_init;       ///< Are bank NeXus groups initialized?
        uint64_t                m_event_cur_size;   ///< Running size of TOF and PID datasets (same size)
        uint64_t                m_index_cur_size;   ///< Running size of event index dataset
        NxGen&                  m_nxgen;            ///< NxGen parent class
    };

    /// MonitorInfo subclass that adds Nexus-required attributes
    class NxMonitorInfo : public STS::MonitorInfo
    {
    public:
        /// NxMonitorInfo constructor
        NxMonitorInfo
        (
            uint16_t a_id,                    ///< [in] ID of detector bank
            uint32_t a_buf_reserve,           ///< [in] Event buffer initial capacity
            uint32_t a_idx_buf_reserve,       ///< [in] Index buffer initial capacity
            STS::BeamMonitorConfig *a_config, ///< [in] Beam Mon Histo Config (opt)
            bool a_known_monitor,             ///< [in] Valid Beam Mon Config?
            NxGen &a_nxgen                    ///< [in] Parent NxGen instance
        )
        :
            MonitorInfo( a_id, a_buf_reserve, a_idx_buf_reserve, a_config ),
            m_index_cur_size(0),
            m_event_cur_size(0),
            m_nxgen(a_nxgen)
        {
            // "Known" Monitor - Valid Histo Config or No Configs at All
            if ( a_known_monitor )
            {
                m_name = std::string("monitor")
                    + boost::lexical_cast<std::string>(a_id);

                m_group_type = std::string("NXmonitor");
            }

            // "Unknown" Monitor - Invalid or Missing Histo Config
            // (Make it obvious on casual visual inspection that
            //    this monitor is whack... ;-)
            else
            {
                m_name = std::string("UnknownMonitor")
                    + boost::lexical_cast<std::string>(a_id);

                m_group_type = std::string("NXcollection");
            }

            m_path = m_nxgen.m_entry_path + "/" + m_name;

            // Monitor Event Paths

            m_index_path = m_path + "/" + m_nxgen.m_index_name;

            m_tof_path = m_path + "/" + m_nxgen.m_tof_name;

            // Monitor Histogram Paths

            m_data_path = m_path + "/" + m_nxgen.m_data_name;

            m_tofbin_path = m_path + "/" + m_nxgen.m_tofbin_name;
        }

        std::string             m_name;             ///< Name of monitor in Nexus file
        std::string             m_path;             ///< Nexus path to monitor group
        std::string             m_group_type;       ///< Type of encompassing group in Nexus file
        std::string             m_index_path;       ///< Nexus path to event index dataset
        std::string             m_tof_path;         ///< Nexus path to TOF dataset
        std::string             m_data_path;        ///< Nexus path to Histo data dataset
        std::string             m_tofbin_path;      ///< Nexus path to Histo TOF Bins dataset
        uint64_t                m_index_cur_size;   ///< Running size of event index dataset
        uint64_t                m_event_cur_size;   ///< Running size of TOF dataset
        NxGen&                  m_nxgen;            ///< NxGen parent class
    };

    /// PVInfo subclass that adds Nexus-required attributes and virtual method implementations.
    template<class T>
    class NxPVInfo : public STS::PVInfo<T>
    {
    public:
        /// NxPVInfo constructor
        NxPVInfo
        (
            const std::string     &a_device_name,  ///< [in] Name of owning device
            const std::string     &a_name,         ///< [in] Name of PV
            const std::string     &a_internal_name,///< [in] Internal (Nexus) name of PV
            const std::string     &a_connection,   ///< [in] PV Connection String
            const std::string     &a_internal_connection, ///< [in] Internal (Nexus) PV Connection String
            STS::Identifier        a_device_id,    ///< [in] ID of device that owns the PV
            STS::Identifier        a_pv_id,        ///< [in] ID of the PV
            STS::PVType            a_type,         ///< [in] Type of PV
            STS::PVEnumeratedType *a_enum,         ///< [in] Enumerated Type
            const std::string     &a_units,        ///< [in] Units of PV (empty if not needed)
            NxGen                 &a_nxgen         ///< [in] NxGen instance needed for Nexus output
        )
        :
            STS::PVInfo<T>( a_device_name, a_name, a_connection,
                a_device_id, a_pv_id, a_type, a_enum, a_units ),
            m_nxgen(a_nxgen),
            m_internal_name(a_internal_name),
            m_internal_connection(a_internal_connection),
            m_cur_size(0),
            m_string_data_cur_size(0)
        {
            // If the PV Name and Connection String are the Same,
            // then there's No Alias, and No Need for a Distinct Link.
            if ( m_internal_name == m_internal_connection )
            {
                m_log_path = m_nxgen.m_daslogs_path + "/" + m_internal_name;
                m_link_path = "";
            }

            // Otherwise, If the PV Name and Connection String are Distinct,
            // then Use the Connection String for the Actual Data and
            // Create a Link Path using the (Alias) Name...
            else
            {
                m_log_path = m_nxgen.m_daslogs_path
                    + "/" + m_internal_connection;
                m_link_path = m_nxgen.m_daslogs_path
                    + "/" + m_internal_name;
            }
        }

        /// NxPVInfo destructor
        ~NxPVInfo() {}

        /// Push Uint32 PV Value onto Statistics...
        void addToStats
        (
            uint32_t a_value ///< Uint32 Value to Push
        )
        {
            this->m_stats.push(a_value);
        }

        /// Push Double PV Value onto Statistics...
        void addToStats
        (
            double a_value ///< Double Value to Push
        )
        {
            this->m_stats.push(a_value);
        }

        /// Ignore String PV Value for Statistics...
        void addToStats
        (
            std::string UNUSED(a_value) ///< String Value to Ignore
        )
        {
        }

        /// Writes Buffered Uint32 PV Values to Nexus File 
        void flushValueBuffers
        (
            std::vector<uint32_t> & value_buffer ///< Uint32 Buffer to Write
        )
        {
            // TODO - This code may need to be optimized
            // when fast metadata is supported
            m_nxgen.writeSlab( m_log_path + "/value",
                value_buffer, m_cur_size );
        }

        /// Writes Buffered Double PV Values to Nexus File 
        void flushValueBuffers
        (
            std::vector<double> & value_buffer ///< Double Buffer to Write
        )
        {
            // TODO - This code may need to be optimized
            // when fast metadata is supported
            m_nxgen.writeSlab( m_log_path + "/value",
                value_buffer, m_cur_size );
        }

        /// Writes Buffered String PV Values to Nexus File 
        void flushValueBuffers
        (
            std::vector<std::string> & value_buffer ///< String Buffer to Write
        )
        {
            // Create String Meta-data in log,
            // if no data has been written yet...
            if ( !m_cur_size )
            {
                m_nxgen.makeDataset( m_log_path, "offset", NeXus::UINT32 );
                m_nxgen.makeDataset( m_log_path, "length", NeXus::UINT32 );
            }

            // Create String Offset, Length and Data Fields...
            std::vector<uint32_t> offset;
            std::vector<uint32_t> length;
            std::vector<char> data;
            unsigned long last_offset = m_string_data_cur_size;
            for ( uint32_t i=0 ; i < value_buffer.size() ; i++ )
            {
                offset.push_back( last_offset );
                last_offset += value_buffer[i].size();
                length.push_back( value_buffer[i].size() );
                data.reserve( data.size() + value_buffer[i].size() );
                data.insert( data.end(),
                    value_buffer[i].begin(), value_buffer[i].end()) ;
            }

            // Write String Fields to NeXus File...
            m_nxgen.writeSlab( m_log_path + "/offset",
                offset, m_cur_size );
            m_nxgen.writeSlab( m_log_path + "/length",
                length, m_cur_size );
            if ( data.size() )
            {
                m_nxgen.writeSlab( m_log_path + "/value",
                    data, m_string_data_cur_size );
                m_string_data_cur_size += data.size();
            }
        }

        /// Writes buffered PV values and time axis to Nexus file and performs finalization
        void flushBuffers
        (
            struct STS::RunMetrics *a_run_metrics      ///< If non-zero, indicates finalization code should be executed for this PV
        )
        {
            try
            {
                if ( m_nxgen.m_gen_nexus )
                {
                    // Create log if no data has been written yet
                    if ( !m_cur_size )
                    {
                        // "Nothing to See Here"...! ;-D
                        // (Never Got Any Data, and Don't Have Any Now,
                        //    So Just Ignore This PV and Move On...! ;-)
                        // [Need this check because _Always_ called
                        //    in finalizeStreamProcessing() now... ;-]
                        if ( !(this->m_value_buffer.size()) )
                            return;

                        m_nxgen.makeGroup( m_log_path, "NXlog" );
                        m_nxgen.makeDataset( m_log_path, "value",
                            m_nxgen.toNxType( this->m_type ),
                            this->m_units );
                        m_nxgen.makeDataset( m_log_path, "time",
                            NeXus::FLOAT64, TIME_SEC_UNITS );
                    }

                    // Flush Any Pending Value/Time Data...
                    if ( this->m_value_buffer.size() )
                    {
                        // Flush Value Buffers to NeXus File
                        // (by Templated Data type... ;-D)
                        flushValueBuffers( this->m_value_buffer );

                        m_nxgen.writeSlab( m_log_path + "/time",
                            this->m_time_buffer, m_cur_size );

                        m_cur_size += this->m_value_buffer.size();
                    }

                    if ( a_run_metrics )
                    {
                        // Add start time (offset) properties
                        // to all time axis in DAS logs
                        std::string time = timeToISO8601(
                            a_run_metrics->start_time );
                        std::string time_path = m_log_path + "/time";
                        m_nxgen.writeStringAttribute( time_path,
                            "start", time );
                        m_nxgen.writeScalarAttribute( time_path,
                            "offset_seconds",
                            (uint32_t)a_run_metrics->start_time.tv_sec
                                - ADARA::EPICS_EPOCH_OFFSET );
                        m_nxgen.writeScalarAttribute( time_path,
                            "offset_nanoseconds",
                            (uint32_t)a_run_metrics->start_time.tv_nsec );

                        if ( m_cur_size
                                // No Statistics for Strings!
                                && this->m_type != STS::PVT_STRING )
                        {
                            // Data has been written,
                            // so also write statistics
                            m_nxgen.writeScalar( m_log_path,
                                "minimum_value", this->m_stats.min(),
                                this->m_units );
                            m_nxgen.writeScalar( m_log_path,
                                "maximum_value", this->m_stats.max(),
                                this->m_units );
                            m_nxgen.writeScalar( m_log_path,
                                "average_value", this->m_stats.mean(),
                                this->m_units );
                            m_nxgen.writeScalar( m_log_path,
                                "average_value_error",
                                this->m_stats.stdDev(), this->m_units );
                        }

                        // Last Pass, Create Any Enumerated Type Link Now!
                        if ( this->m_enum != NULL )
                        {
                            std::stringstream ss_src;
                            ss_src << m_nxgen.m_daslogs_path << "/"
                                << "Device" << this->m_device_id
                                << ":" << "Enum"
                                << ":" << this->m_enum->name;

                            std::stringstream ss_dst;
                            ss_dst << m_log_path << "/" << "enum";

                            syslog( LOG_INFO,
                                "[%i] Linking Enum Group %s to %s",
                                g_pid, ss_src.str().c_str(),
                                ss_dst.str().c_str() );

                            m_nxgen.makeGroupLink(
                                ss_src.str(), ss_dst.str() );
                        }

                        // Last Pass, Create Any PV (Alias) Link Now!
                        if ( !m_link_path.empty() )
                        {
                            syslog( LOG_INFO,
                                "[%i] Linking PV Channel %s to Alias %s",
                                g_pid, m_log_path.c_str(),
                                m_link_path.c_str() );

                            // Manually Create "Target" String for Linking
                            // (as per makeGroupLink usage...)
                            m_nxgen.writeString( m_log_path,
                                "target", m_log_path );

                            m_nxgen.makeGroupLink(
                                m_log_path, m_link_path );
                        }
                    }
                }
            }
            catch( TraceException &e )
            {
                RETHROW_TRACE( e, "NxPVInfo::flushBuffers (pv: "
                    << this->m_device_id << "." << this->m_pv_id
                    << ") failed." )
            }

            this->m_value_buffer.clear();
            this->m_time_buffer.clear();
        }

        NxGen&          m_nxgen;        ///< NxGen instance used for Nexus ouput
        std::string     m_internal_name;///< Internal Nexus name of variable
        std::string     m_internal_connection;///< Internal Nexus connection string of variable
        std::string     m_log_path;     ///< Nexus path to log entry for PV
        std::string     m_link_path;    ///< (Optional) Nexus path for (alias) link to PV log entry
        uint64_t        m_cur_size;     ///< Running size of time and value datasets (same size for both)
        uint64_t        m_string_data_cur_size;   ///< Running size of character string data value dataset
    };

    // Nexus Marker types should correspond to ADARA marker types, but we want to
    // keep them as separate definitions to insulate Nexus from any changes made to
    // the ADARA protocol (and vice versa).
    enum MarkerType
    {
        MT_COMMENT    = 0,
        MT_SCAN_START = 1,
        MT_SCAN_STOP  = 2,
        MT_PAUSE      = 3,
        MT_RESUME     = 4
    };

public:

    NxGen(
        int a_fd_in,
        std::string & a_adara_out_file,
        std::string & a_nexus_out_file,
        bool a_strict,
        bool a_gather_stats,
        unsigned long a_chunk_size = 2048,
        unsigned short a_event_buf_chunk_count = 20,
        unsigned short a_ancillary_buf_chunk_count = 5,
        unsigned long a_cache_size = 10485760,
        unsigned short a_compression_level = 0 );
    ~NxGen();

    void dumpProcessingStatistics(void);

protected:

    void                initialize();
    void                finalize( const STS::RunMetrics &a_run_metrics );
    STS::PVInfoBase*    makePVInfo( const std::string & a_device_name,
                            const std::string & a_name,
                            const std::string & a_connection,
                            STS::Identifier a_device_id,
                            STS::Identifier a_pv_id,
                            STS::PVType a_type,
                            STS::PVEnumeratedType *a_enum,
                            const std::string & a_units );
    STS::BankInfo*      makeBankInfo( uint16_t a_id,
                            uint32_t a_buf_reserve,
                            uint32_t a_idx_buf_reserve );
    void                initializeNxBank( NxBankInfo *a_bi,
                            bool a_end_of_run );
    STS::MonitorInfo*   makeMonitorInfo( uint16_t a_id,
                            uint32_t a_buf_reserve,
                            uint32_t a_idx_buf_reserve,
                            STS::BeamMonitorConfig *a_config,
                            bool a_known_monitor );
    void                processRunInfo( const STS::RunInfo & a_run_info );
    void                processGeometry( const std::string & a_xml );
    void                pulseBuffersReady( STS::PulseInfo &a_pulse_info );
    void                bankBuffersReady( STS::BankInfo &a_bank );
    void                bankPulseGap( STS::BankInfo &a_bank,
                            uint64_t a_count );
    void                bankFinalize( STS::BankInfo &a_bank );
    void                monitorBuffersReady(
                            STS::MonitorInfo &a_monitor_info );
    void                monitorPulseGap( STS::MonitorInfo &a_monitor,
                            uint64_t a_count );
    void                monitorFinalize( STS::MonitorInfo &a_monitor );
    void                runComment( const std::string &a_comment );
    void                markerPause( double a_time,
                            const std::string &a_comment  );
    void                markerResume( double a_time,
                            const std::string &a_comment  );
    void                markerScanStart( double a_time,
                            unsigned long a_scan_index,
                            const std::string &a_comment );
    void                markerScanStop( double a_time,
                            unsigned long a_scan_index,
                            const std::string &a_comment  );
    void                markerComment( double a_time,
                            const std::string &a_comment );
    void                writeDeviceEnums( STS::Identifier a_devId,
                            std::vector<STS::PVEnumeratedType> a_enumVec );

private:
    void                flushPauseData();
    void                flushScanData();
    void                flushCommentData();
    NeXus::NXnumtype    toNxType( STS::PVType a_type ) const;
    void                makeGroup( const std::string &a_path,
                            const std::string &a_type );
    void                makeDataset( const std::string &dataset_path,
                            const std::string &dataset_name,
                            NeXus::NXnumtype nxdatatype,
                            const std::string units = "" );
    void                writeMultidimDataset(
                            const std::string &dataset_path,
                            const std::string &dataset_name,
                            std::vector<uint32_t> &a_data,
                            std::vector<hsize_t> &a_dims,
                            const std::string units = "" );
    void                makeLink( const std::string &source_path,
                            const std::string &dest_name );
    void                makeGroupLink( const std::string &source_path,
                            const std::string &dest_name );
    void                writeString( const std::string &a_path,
                            const std::string &a_dataset,
                            const std::string &a_value );
    void                writeStringAttribute( const std::string &a_path,
                            const std::string &a_attrib,
                            const std::string &a_value );

    /// Writes data values to a Nexus (HDF5) one-dimension dataset
    template<class T>
    void                writeSlab
                        (
                            const std::string & a_path, ///< [in] Nexus path to dataset
                            std::vector<T> & a_buffer,  ///< [in] Vector of data to write
                            uint64_t a_cur_size         ///< [in] Current dataset size (counts not bytes) [Actually "offset"...! Jeeem]
                        )
                        {
                            writeSlab( a_path,
                                a_buffer, a_buffer.size(), a_cur_size );
                        }
    template<class T>
    void                writeSlab
                        (
                            const std::string & a_path, ///< [in] Nexus path to dataset
                            std::vector<T> & a_buffer,  ///< [in] Vector of data to write
                            uint64_t a_buffer_size,     ///< [in] Size of Vector of data to write
                            uint64_t a_cur_size         ///< [in] Current dataset size (counts not bytes) [Actually "offset"...! Jeeem]
                        )
                        {
                            if ( a_buffer_size )
                            {
                                if ( m_h5nx.H5NXwrite_slab( a_path,
                                        a_buffer, a_buffer_size,
                                        a_cur_size ) != SUCCEED )
                                {
                                    THROW_TRACE( STS::ERR_OUTPUT_FAILURE,
                                        "H5NXwrite_slab FAILED for path: "
                                            << a_path
                                            << " a_buffer_size="
                                            << a_buffer_size
                                            << " a_cur_size(offset)="
                                            << a_cur_size);
                                }
                            }
                        }

    /// Fills (appends) a Nexus (HDF5) one-dimension dataset with a provided value
    template<class T>
    void                fillSlab
                        (
                            const std::string & a_path, ///< [in] Nexus path to dataset
                            T & a_value,                ///< [in] Value to fill (append) dataset with
                            uint64_t a_count,           ///< [in] Number of values to append
                            uint64_t a_cur_size         ///< Current dataset size (counts not bytes)
                        )
                        {
                            if ( a_count )
                            {
                                std::vector<T> buf;
                                uint64_t cur_size = a_cur_size;
                                uint64_t count = 0;

                                if ( a_count >= m_chunk_size )
                                {
                                    buf.resize( m_chunk_size, a_value );

                                    while ( count <= ( a_count - m_chunk_size ) )
                                    {
                                        writeSlab( a_path, buf, cur_size );
                                        count += buf.size();
                                        cur_size += buf.size();
                                    }
                                }

                                if ( count < a_count )
                                {
                                    buf.resize( a_count - count, a_value );
                                    writeSlab( a_path, buf, cur_size );
                                }
                            }
                        }

    template<typename T>
    void                writeScalar( const std::string & a_path,
                            const std::string & a_name, T a_value,
                            const std::string & a_units );
    template<typename T>
    void                writeScalarAttribute( const std::string & a_path,
                            const std::string & a_attribute, T a_value );

    bool                m_gen_nexus;            ///< Controls whether Nexus file is generated or not
    std::string         m_nexus_filename;       ///< Name of Nexus file
    std::string         m_entry_path;           ///< Path to Nexus NXentry
    std::string         m_instrument_path;      ///< Path to Nexus NXinstrument
    std::string         m_daslogs_path;         ///< Path to Nexus DAS Logs
    std::string         m_daslogs_freq_path;    ///< Path to Nexus Frequency DAS Log
    std::string         m_daslogs_pchg_path;    ///< Path to Nexus Proton Charge DAS Log
    std::string         m_pid_name;             ///< Name of PID data in Nexus file
    std::string         m_tof_name;             ///< Name of TOF data in Nexus file
    std::string         m_index_name;           ///< Name of Event Index data in Nexus file
    std::string         m_pulse_time_name;      ///< Name of Pulse Time data in Nexus file
    std::string         m_data_name;            ///< Name of Histo data in Nexus file
    std::string         m_histo_pid_name;       ///< Name of Histo PixelId data in Nexus file
    std::string         m_tofbin_name;          ///< Name of Histo TOF Bin data in Nexus file
    unsigned long       m_chunk_size;           ///< HDF5 chunk size for Nexus file
    H5nx                m_h5nx;                 ///< HDF5 library object
    uint64_t            m_pulse_info_cur_size;  ///< Current size of pulse info datasets (charge, time, frequency)
    std::vector<double> m_pulse_vetoes;         ///< Buffer of pulse veto times
    uint64_t            m_pulse_vetoes_cur_size;       ///< Current size of pulse veto dataset

    std::vector<double>         m_pulse_flags_time;     ///< Buffer of pulse flag times
    std::vector<uint32_t>       m_pulse_flags_value;    ///< Buffer of pulse flag values
    uint64_t                    m_pulse_flags_cur_size; ///< Current size of pulse flags dataset

    std::vector<double>         m_pause_time;           /// Pause annotation timestamp buffer
    std::vector<uint16_t>       m_pause_value;          /// Pause value (on/off) buffer
    std::vector<double>         m_scan_time;            /// Scan annotation value (on/off) buffer
    std::vector<uint32_t>       m_scan_value;           /// Scan value (index) buffer
    std::vector<double>         m_comment_time;         /// Comment annotation timestamp buffer
    std::vector<uint32_t>       m_comment_offset;       /// Comment data dataset offset buffer
    std::vector<uint32_t>       m_comment_length;       /// Comment data length buffer
    std::vector<char>           m_comment_data;         /// Comment data buffer
    unsigned long               m_comment_last_offset;  /// Last dataset offset written to Nexus
    std::set<std::string>       m_pv_name_history;      /// Name/version history of PVs written to Nexus file
    std::set<std::string>       m_pv_connection_history;/// Connection String/version history of PVs written to Nexus file
    bool                        m_haveRunComment;       /// Flag to prevent Duplicate Run Comments in Nexus file
    float                       m_duration;             /// Save Total Run Duration (seconds)
    uint64_t                    m_total_counts;         /// Total Run Event Counts
    uint64_t                    m_total_uncounts;       /// Total Run Event Uncounts
    uint64_t                    m_total_non_counts;     /// Total Run Event Non-Counts (Monitor)
    struct timespec             m_sts_start_time;       /// STS Start of Processing Time
};

#endif // NXGEN_H

// vim: expandtab

