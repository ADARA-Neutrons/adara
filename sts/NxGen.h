#ifndef NXGEN_H
#define NXGEN_H

#include <string>
#include <vector>
#include "h5nx.hpp"
#include "stsdefs.h"
#include "StreamParser.h"
#include "Utils.h"
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
            uint16_t a_pixel_count,     ///< [in] Pixel count of bank
            uint32_t a_buf_reserve,     ///< [in] Event buffer initial capacity
            uint32_t a_idx_buf_reserve  ///< [in] Index buffer initial capacity
        )
        :
            BankInfo(a_id, a_pixel_count, a_buf_reserve, a_idx_buf_reserve),
            m_event_slab_size(0),
            m_index_slab_size(0)
        {
            m_name = std::string("bank") + boost::lexical_cast<std::string>(a_id);
            m_eventname = m_name + "_events";
            m_tof_slab_path = "/entry/instrument/" + m_name + "/event_time_offset";
            m_pid_slab_path = "/entry/instrument/" + m_name + "/event_id";
            m_index_slab_path = "/entry/instrument/" + m_name + "/event_index";
        }

        std::string             m_name;             ///< Name of bank in Nexus file
        std::string             m_eventname;        ///< Name of bank events entry in Nexus file
        std::string             m_tof_slab_path;    ///< Nexus path to TOF slab
        std::string             m_pid_slab_path;    ///< Nexus path to PID slab
        std::string             m_index_slab_path;  ///< Nexus path to event index slab
        uint64_t                m_event_slab_size;  ///< Running size of TOF and PID slabs (same size)
        uint64_t                m_index_slab_size;  ///< Running size of event index slab
    };

    /// MonitorInfo subclass that adds Nexus-required attributes
    class NxMonitorInfo : public STS::MonitorInfo
    {
    public:
        /// NxMonitorInfo constructor
        NxMonitorInfo
        (
            uint16_t a_id,              ///< [in] ID of detector bank
            uint32_t a_buf_reserve,     ///< [in] Event buffer initial capacity
            uint32_t a_idx_buf_reserve  ///< [in] Index buffer initial capacity
        )
        :
            MonitorInfo( a_id, a_buf_reserve, a_idx_buf_reserve ),
            m_index_slab_size(0),
            m_event_slab_size(0)
        {
            m_name = std::string("monitor") + boost::lexical_cast<std::string>(a_id);
            m_index_slab_path = "/entry/" + m_name + "/event_index";
            m_tof_slab_path = "/entry/" + m_name + "/event_time_offset";
        }

        std::string             m_name;             ///< Name of monitor in Nexus file
        std::string             m_index_slab_path;  ///< Nexus path to event index slab
        std::string             m_tof_slab_path;    ///< Nexus path to TOF slab
        uint64_t                m_index_slab_size;  ///< Running size of event index slab
        uint64_t                m_event_slab_size;  ///< Running size of TOF slab
    };

    /// PVInfo subclass that adds Nexus-required attributes and virtual method implementations.
    template<class T>
    class NxPVInfo : public STS::PVInfo<T>
    {
    public:
        /// NxPVInfo constructor
        NxPVInfo
        (
            const std::string  &a_name,         ///< [in] Name of PV
            STS::Identifier     a_device_id,    ///< [in] ID of device that owns the PV
            STS::Identifier     a_pv_id,        ///< [in] ID of the PV
            STS::PVType         a_type,         ///< [in] Type of PV
            const std::string  &a_units,        ///< [in] Units of PV (empty if not needed)
            NxGen              &a_nxgen         ///< [in] NxGen instance needed for Nexus ouput
        )
        :
            STS::PVInfo<T>( a_name, a_device_id, a_pv_id, a_type, a_units ),
            m_nxgen(a_nxgen),
            m_slab_size(0)
        {
            m_log_path = std::string("/entry/DASlogs/") + a_name;
        }

        /// NxPVInfo destructor
        ~NxPVInfo() {}

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
                    if ( !m_slab_size )
                    {
                        m_nxgen.makeGroup( m_log_path, "NXlog" );
                        m_nxgen.makeDataset( m_log_path, "value", m_nxgen.toNxType( this->m_type ), this->m_units );
                        m_nxgen.makeDataset( m_log_path, "time", NeXus::FLOAT64, TIME_SEC_UNITS );
                    }

                    // TODO - This code may need to be optimized when fast metadata is supported
                    m_nxgen.writeSlab( m_log_path + "/value", this->m_value_buffer, m_slab_size );
                    m_nxgen.writeSlab( m_log_path + "/time", this->m_time_buffer, m_slab_size );

                    m_slab_size += this->m_value_buffer.size();

                    if ( a_run_metrics )
                    {
                        // Add start time (offset) properties to all time axis in DAS logs
                        std::string time = timeToISO8601( a_run_metrics->start_time );
                        std::string time_path = m_log_path + "/time";
                        m_nxgen.writeStringAttribute( time_path, "start", time );
                        m_nxgen.writeScalarAttribute( time_path, "offset_seconds", (uint32_t)a_run_metrics->start_time.tv_sec - ADARA::EPICS_EPOCH_OFFSET );
                        m_nxgen.writeScalarAttribute( time_path, "offset_nanoseconds", (uint32_t)a_run_metrics->start_time.tv_nsec );

                        if ( m_slab_size )
                        {
                            // Data has been writen, so also write statistics
                            m_nxgen.writeScalar( m_log_path, "minimum_value", this->m_stats.min(), this->m_units );
                            m_nxgen.writeScalar( m_log_path, "maximum_value", this->m_stats.max(), this->m_units );
                            m_nxgen.writeScalar( m_log_path, "average_value", this->m_stats.mean(), this->m_units );
                            m_nxgen.writeScalar( m_log_path, "average_value_error", this->m_stats.stdDev(), this->m_units );
                        }
                    }
                }
            }
            catch( TraceException &e )
            {
                RETHROW_TRACE( e, "NxPVInfo::flushBuffers (pv: " << this->m_device_id << "." << this->m_pv_id << ") failed." )
            }

            this->m_value_buffer.clear();
            this->m_time_buffer.clear();
        }

        NxGen&          m_nxgen;        ///< NxGen instance used for Nexus ouput
        std::string     m_log_path;     ///< Nexus path to log entry for PV
        uint64_t        m_slab_size;    ///< Running size of time and value slabs (same size for both)
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

    NxGen( int a_fd_in, std::string & a_adara_out_file, std::string &a_nexus_out_file, bool a_strict, bool a_gather_stats, unsigned long a_chunk_size = 2048, unsigned short a_event_buf_chunk_count = 20, unsigned short a_ancillary_buf_chunk_count = 5, unsigned long a_cache_size = 10485760, unsigned short a_compression_level = 0 );
    ~NxGen();

protected:

    void                initialize();
    void                finalize( const STS::RunMetrics &a_run_metrics );
    STS::PVInfoBase*    makePVInfo( const std::string & a_name, STS::Identifier a_device_id, STS::Identifier a_pv_id, STS::PVType a_type, const std::string & a_units );
    STS::BankInfo*      makeBankInfo( uint16_t a_id, uint16_t a_pixel_count, uint32_t a_buf_reserve, uint32_t a_idx_buf_reserve );
    STS::MonitorInfo*   makeMonitorInfo( uint16_t a_id, uint32_t a_buf_reserve, uint32_t a_idx_buf_reserve );
    void                processRunInfo( const STS::RunInfo & a_run_info );
    void                processGeometry( const std::string & a_xml );
    void                pulseBuffersReady( STS::PulseInfo &a_pulse_info );
    void                bankBuffersReady( STS::BankInfo &a_bank );
    void                bankPulseGap( STS::BankInfo &a_bank, uint64_t a_count );
    void                bankFinalize( STS::BankInfo &a_bank );
    void                monitorBuffersReady( STS::MonitorInfo &a_monitor_info );
    void                monitorPulseGap( STS::MonitorInfo &a_monitor, uint64_t a_count );
    void                monitorFinalize( STS::MonitorInfo &a_monitor );
    void                runComment( const std::string &a_comment );
    void                markerPause( double a_time, const std::string &a_comment  );
    void                markerResume( double a_time, const std::string &a_comment  );
    void                markerScanStart( double a_time, unsigned long a_scan_index, const std::string &a_comment );
    void                markerScanStop( double a_time, unsigned long a_scan_index, const std::string &a_comment  );
    void                markerComment( double a_time, const std::string &a_comment );

private:
    void                flushPauseData();
    void                flushScanData();
    void                flushCommentData();
    NeXus::NXnumtype    toNxType( STS::PVType a_type ) const;
    void                makeGroup( const std::string &a_path, const std::string &a_type );
    void                makeDataset( const std::string &dataset_path, const std::string &dataset_name, NeXus::NXnumtype nxdatatype, const std::string units = "" );
    void                makeLink( const std::string &source_path, const std::string &dest_name );
    void                writeString( const std::string &a_path, const std::string &a_dataset, const std::string &a_value );
    void                writeStringAttribute( const std::string &a_path, const std::string &a_attrib, const std::string &a_value );

    /// Writes data values to a Nexus (HDF5) one-dimension slab
    template<class T>
    void                writeSlab
                        (
                            const std::string & a_path, ///< [in] Nexus path to slab
                            std::vector<T> & a_buffer,  ///< [in] Vector of data to write
                            uint64_t a_slab_size        ///< [in] Current slab size (counts not bytes)
                        )
                        {
                            if ( a_buffer.size())
                            {
                                if ( m_h5nx.H5NXwrite_slab( a_path, a_buffer, a_slab_size ) != SUCCEED )
                                {
                                    THROW_TRACE( STS::ERR_OUTPUT_FAILURE, "H5NXwrite_slab FAILED for path: " << a_path );
                                }
                            }
                        }

    /// Fills (appends) a Nexus (HDF5) one-dimension slab with a provided value
    template<class T>
    void                fillSlab
                        (
                            const std::string & a_path, ///< [in] Nexus path to slab
                            T & a_value,                ///< [in] Value to fill (append) slab with
                            uint64_t a_count,           ///< [in] Number of values to append
                            uint64_t a_slab_size        ///< Current slab size (counts not bytes)
                        )
                        {
                            if ( a_count )
                            {
                                std::vector<T> buf;
                                uint64_t slab_size = a_slab_size;
                                uint64_t count = 0;

                                if ( a_count >= m_chunk_size )
                                {
                                    buf.resize( m_chunk_size, a_value );

                                    while( count <= ( a_count - m_chunk_size ))
                                    {
                                        writeSlab( a_path, buf, slab_size );
                                        count += buf.size();
                                        slab_size += buf.size();
                                    }
                                }

                                if ( count < a_count )
                                {
                                    buf.resize( a_count - count, a_value );
                                    writeSlab( a_path, buf, slab_size );
                                }
                            }
                        }

    template<typename T>
    void                writeScalar( const std::string & a_path, const std::string & a_name, T a_value, const std::string & a_units );
    template<typename T>
    void                writeScalarAttribute( const std::string & a_path, const std::string & a_attribute, T a_value );

    bool                m_gen_nexus;            ///< Controls whether Nexus file is generated or not
    std::string         m_nexus_filename;       ///< Name of Nexus file
    unsigned long       m_chunk_size;           ///< HDF5 chunk size for Nexus file
    H5nx                m_h5nx;                 ///< HDF5 library object
    uint64_t            m_pulse_info_slab_size; ///< Current size of pulse info slabs (charge, time, frequency)
    std::vector<double> m_pulse_vetoes;         ///< Buffer of pulse veto times
    uint64_t            m_pulse_vetoes_slab_size;   ///< Current size of pulse vetoe slab

    // Pause/resume data
    std::vector<double>         m_pause_time;
    std::vector<uint16_t>       m_pause_value;

    // Scan start/stop/index data
    std::vector<double>         m_scan_time;
    std::vector<uint32_t>       m_scan_value;

    // Stream annotation data
    std::vector<double>         m_comment_time;
    std::vector<uint32_t>       m_comment_offset;
    std::vector<uint32_t>       m_comment_length;
    std::vector<char>           m_comment_data;
    unsigned long               m_comment_last_offset;
};

#endif // NXGEN_H
