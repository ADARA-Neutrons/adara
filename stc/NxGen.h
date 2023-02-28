#ifndef NXGEN_H
#define NXGEN_H

#include <string>
#include <vector>
#include <set>
#include <syslog.h>
#include <time.h>
#include <unistd.h>
#include "h5nx.hpp"
#include "stcdefs.h"
#include "StreamParser.h"
#include "ADARAUtils.h"
#include <boost/lexical_cast.hpp>
#include <boost/regex.hpp>


#define CHARGE_UNITS "picoCoulombs"
#define FREQ_UNITS "Hz"
#define TIME_SEC_UNITS "second"
#define TIME_USEC_UNITS "microsecond"


// Note: Rate-Limited Logging History Instance is Part of NxGen Class:
//    RateLimitedLogging::History m_RLLHistory_NxGen;

// Rate-Limited Logging IDs...
#define RLL_NON_NORM_BUF_FULL     0


/*! \brief ADARA Stream Adapter class that provides NeXus file generation
 *
 * The NxGen class is a stream adapter subclass that specializes the
 * ADARA StreamParser class for creating NeXus output files.
 */
class NxGen : public STC::StreamParser
{
private:

    /// BankInfo subclass that adds Nexus-required attributes
    class NxBankInfo : public STC::BankInfo
    {
    public:
        /// NxBankInfo constructor
        NxBankInfo
        (
            uint16_t a_id,              ///< [in] ID of detector bank
            uint16_t a_state,           ///< [in] State of detector bank
            uint32_t a_buf_reserve,     ///< [in] Event buffer initial capacity
            uint32_t a_idx_buf_reserve, ///< [in] Index buffer initial capacity
            NxGen &a_nxgen              ///< [in] Parent NxGen instance
        )
        :
            BankInfo(a_id, a_state, a_buf_reserve, a_idx_buf_reserve),
            m_nexus_bank_init(false),
            m_event_cur_size(0),
            m_index_cur_size(0),
            m_nxgen(a_nxgen)
        {
            if ( a_id == (uint16_t) UNMAPPED_BANK ) {
                m_name = std::string("bank_unmapped");
            }
            else if ( a_id == (uint16_t) ERROR_BANK ) {
                m_name = std::string("bank_error");
            }
            else if ( a_state != 0 ) {
                m_name = std::string("bank")
                    + boost::lexical_cast<std::string>(a_id)
                    + std::string("_state")
                    + boost::lexical_cast<std::string>(a_state);
            }
            else {
                m_name = std::string("bank")
                    + boost::lexical_cast<std::string>(a_id);
            }

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

            m_histo_pid_path_raw = m_instr_path + "/"
                + m_nxgen.m_histo_pid_name_raw;

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
        std::string             m_histo_pid_path_raw; ///< Nexus path to Histo Physical (Raw) PID dataset
        std::string             m_tofbin_path;      ///< Nexus path to Histo TOF Bin dataset
        bool                    m_nexus_bank_init;  ///< Are bank NeXus groups initialized?
        uint64_t                m_event_cur_size;   ///< Running size of TOF and PID datasets (same size)
        uint64_t                m_index_cur_size;   ///< Running size of event index dataset
        NxGen                  &m_nxgen;            ///< NxGen parent class
    };

    /// MonitorInfo subclass that adds Nexus-required attributes
    class NxMonitorInfo : public STC::MonitorInfo
    {
    public:
        /// NxMonitorInfo constructor
        NxMonitorInfo
        (
            uint16_t a_id,                    ///< [in] ID of detector bank
            uint32_t a_buf_reserve,           ///< [in] Event buffer initial capacity
            uint32_t a_idx_buf_reserve,       ///< [in] Index buffer initial capacity
            STC::BeamMonitorConfig &a_config, ///< [in] Beam Monitor Config
            NxGen &a_nxgen                    ///< [in] Parent NxGen instance
        )
        :
            MonitorInfo( a_id, a_buf_reserve, a_idx_buf_reserve,
                a_config ),
            m_nexus_monitor_init(false),
            m_index_cur_size(0),
            m_event_cur_size(0),
            m_nxgen(a_nxgen)
        {
            m_name = std::string("monitor")
                + boost::lexical_cast<std::string>(a_id);

            m_group_type = std::string("NXmonitor");

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
        bool                    m_nexus_monitor_init; ///< Are bank NeXus groups initialized?
        uint64_t                m_index_cur_size;   ///< Running size of event index dataset
        uint64_t                m_event_cur_size;   ///< Running size of TOF dataset
        NxGen                  &m_nxgen;            ///< NxGen parent class
    };

    // (STC Config) Element Structure to store information about
    // Linked Entities in a Group Container...
    struct ElementInfo
    {
        std::vector<std::string>    patterns;
        std::vector<std::string>    indices;
        std::string                 name;
        std::vector<std::string>    unitsPatterns;
        std::string                 unitsValue;
        std::string                 units;
        std::map<std::string, std::string>
                                    unitsPaths;
        bool                        linkValue;
        bool                        linkLastValue;
        std::map<std::string, std::string>
                                    createdLinks;
        uint32_t                    lastIndex;
    };

    // Look for an ElementInfo Struct by Name in an Existing Vector...
    struct ElementInfo *findGroupElementByName( std::string name,
            std::vector<struct ElementInfo> elements )
    {
        for ( uint32_t i=0 ; i < elements.size() ; i++ )
        {
            if ( !(name.compare( elements[i].name )) )
                return( &(elements[i]) );
        }
        return( (struct ElementInfo *) NULL );
    }

    // (STC Config) Conditional Structure to store information about
    // Conditionally-Included Elements in a Group Container...
    // (Contains a Vector of ElementInfo structures to be included...)
    struct ConditionInfo
    {
        std::string                     name;
        std::vector<std::string>        patterns;
        std::vector<std::string>        value_strings;
        std::vector<std::string>        values;
        std::vector<std::string>        not_value_strings;
        std::vector<std::string>        not_values;
        std::vector<struct ElementInfo> elements;
        bool                            is_set;
    };

    // (STC Config) Group Name Embedded Index Specifier...
    static std::string GroupNameIndex;

    // (STC Config) Group Container Structure to store information about
    // collections of NeXus/DASlogs data elements to be collected together
    // and Linked into a Group, at a specific location in the NeXus File.
    struct GroupInfo
    {
        std::string                         name;
        std::string                         path;
        std::string                         type;
        std::vector<struct ElementInfo>     elements;
        std::vector<struct ConditionInfo>   conditions;
        std::set<std::string>               createdIndices;
        bool                                created;
        bool                                hasIndex;
    };

    // (STC Config) Command Structure to specify a handy
    // (possibly "Conditional"!) Post-Translation (NeXus File Done)
    // and Pre-Post-Autoreduction Triggered "Command Script" for
    // execution, e.g. for Imaging beamlines to handle the final
    // "Post-Translation" Copying of Image Files from the beamline.
    struct CommandInfo
    {
        std::string                         name;
        std::string                         path;
        std::string                         args;
        std::vector<struct ElementInfo>     elements;
        std::vector<struct ConditionInfo>   conditions;
        std::set<std::string>               createdIndices;
        bool                                hasIndex;
    };

    /// PVInfo subclass that adds Nexus-required attributes and virtual method implementations.
    template<class T>
    class NxPVInfo : public STC::PVInfo<T>
    {
    public:
        /// NxPVInfo constructor
        NxPVInfo
        (
            const std::string   &a_device_name, ///< [in] Name of owning device
            const std::string   &a_name,        ///< [in] Name of PV
            const std::string   &a_internal_name, ///< [in] Internal (Nexus) name of PV
            const std::string   &a_connection,  ///< [in] PV Connection String
            const std::string   &a_internal_connection, ///< [in] Internal (Nexus) PV Connection String
            STC::Identifier      a_device_id,   ///< [in] ID of device that owns the PV
            STC::Identifier      a_pv_id,       ///< [in] ID of the PV
            STC::PVType          a_type,        ///< [in] Type of PV
            std::vector<STC::PVEnumeratedType>
                                *a_enum_vector, ///< [in] Enumerated Type Vector
            uint32_t             a_enum_index,  ///< [in] Enumerated Type Index
            const std::string   &a_units,       ///< [in] Units of PV (empty if not needed)
            bool                 a_ignore,      ///< [in] PV Ignore Flag
            bool                 a_duplicate,   ///< [in] PV is a Duplicate
            NxGen               &a_nxgen        ///< [in] NxGen instance needed for Nexus output
        )
        :
            STC::PVInfo<T>( a_device_name, a_name, a_connection,
                a_device_id, a_pv_id, a_type, a_enum_vector, a_enum_index,
                a_units, a_ignore, a_duplicate ),
            m_nxgen(a_nxgen),
            m_internal_name(a_internal_name),
            m_internal_connection(a_internal_connection),
            m_has_link(false),
            m_cur_size(0),
            m_full_buffer_count(0),
            m_value_enum_strings_max_len(-1),
            m_value_enum_strings_not_found(0),
            m_finalized(false)
        {
            // If the PV Name and Connection String are the Same,
            // then there's No Alias, and No Need for a Distinct Link.
            if ( !(m_internal_name.compare( m_internal_connection )) )
            {
                m_log_path =
                    m_nxgen.m_daslogs_path + "/" + m_internal_name;
                m_link_path = "";
            }

            // Otherwise, If the PV Name and Connection String are Distinct
            // then Use the Connection String for the Actual Data and
            // Create a Link Path using the (Alias) Name...
            else
            {
                m_log_path = m_nxgen.m_daslogs_path
                    + "/" + m_internal_connection;
                m_link_path = m_nxgen.m_daslogs_path
                    + "/" + m_internal_name;
            }

            // Construct Handy Device/PV Strings for Logging... ;-D
            std::stringstream device_ss;
            device_ss << "Device " << this->m_device_name
                << " (id=" << this->m_device_id << ")";
            this->m_device_str = device_ss.str();

            std::stringstream pv_ss;
            pv_ss << "PV " << m_internal_name;
            if ( m_internal_name.compare( m_internal_connection ) )
                pv_ss << " (" << m_internal_connection << ")";
            pv_ss << " (id=" << this->m_pv_id << ")";
            this->m_pv_str = pv_ss.str();

            this->m_device_pv_str =
                "[" + this->m_device_str + " " + this->m_pv_str + "]";
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

        /// Push Uint32 Array PV Values onto Statistics...
        void addToStats
        (
            std::vector<uint32_t> a_values  ///< Uint32 Array Values to Push
        )
        {
            for ( uint32_t i=0 ; i < a_values.size() ; i++ )
                this->m_stats.push(a_values[i]);
        }

        /// Push Double Array PV Values onto Statistics...
        void addToStats
        (
            std::vector<double> a_values    ///< Double Array Values to Push
        )
        {
            for ( uint32_t i=0 ; i < a_values.size() ; i++ )
                this->m_stats.push(a_values[i]);
        }

        /// Writes Buffered Uint32 PV Values to Nexus File 
        void flushValueBuffers
        (
            std::vector<uint32_t> &value_buffer ///< Uint32 Buffer to Write
        )
        {
            // TODO - This code may need to be optimized
            // when fast metadata is supported
            m_nxgen.writeSlab( m_log_path + "/value",
                value_buffer, m_cur_size );

            // Did this PV's Value *Change* for the First Time here...?
            if ( !(this->m_value_changed) )
            {
                uint32_t value = ( this->m_last_value_set )
                    ? ( this->m_last_value ) : ( value_buffer.front() );
                for ( uint32_t i=0 ; i < value_buffer.size() ; i++ )
                {
                    // PV Value Changed...
                    if ( value_buffer[i] != value )
                    {
                        this->m_value_changed = true;
                        break;
                    }
                }
            }

            // Save Last Value for Conditional STC Config Groups
            this->m_last_value = value_buffer.back();
            this->m_last_value_set = true;

            // Are There More Than One Value to This PV Time-Series?
            if ( m_cur_size || value_buffer.size() > 1 )
                this->m_last_value_more = true;

            // Capture Value Strings for Enumerated Type PVs...
            // (IFF Everything Works...! ;-D)
            if ( this->m_type == STC::PVT_ENUM
                    && this->m_enum_vector != NULL
                    && this->m_enum_index != (uint32_t) -1 )
            {
                STC::PVEnumeratedType *pv_enum =
                    &((*(this->m_enum_vector))[ this->m_enum_index ]);

                // Do We Have a Matching (Usable) Value and Name Vectors?
                if ( pv_enum->element_values.size()
                        == pv_enum->element_names.size() )
                {
                    // Find Matching Value String for Each Buffer Value...
                    // (I hope this isn't ever "Too Huge"... ;-b)
                    for ( uint32_t i=0 ; i < value_buffer.size() ; i++ )
                    {
                        bool found = false;

                        for ( uint32_t j=0 ; !found
                                && j < pv_enum->element_values.size() ;
                                j++ )
                        {
                            if ( value_buffer[i]
                                    == pv_enum->element_values[j] )
                            {
                                m_value_enum_strings.push_back(
                                    pv_enum->element_names[j] );

                                // Determine Max Value String Length...
                                if ( m_value_enum_strings_max_len
                                        == (uint32_t) -1
                                    || pv_enum->element_names[j].size()
                                        > m_value_enum_strings_max_len )
                                {
                                    m_value_enum_strings_max_len =
                                        pv_enum->element_names[j].size();
                                }

                                found = true;
                            }
                        }

                        // Bummer, Enum Value Not Found... ;-Q
                        // Put *Something* in there to Keep Order...
                        if ( !found )
                        {
                            std::string something = "<Not Found!>";

                            m_value_enum_strings.push_back( something );

                            // Determine Max Value String Length...
                            if ( m_value_enum_strings_max_len
                                    == (uint32_t) -1
                                || something.size()
                                    > m_value_enum_strings_max_len )
                            {
                                m_value_enum_strings_max_len =
                                    something.size();
                            }

                            // Just Log This Kind of Lookup Failure
                            // *ONCE* at the End...! ;-D
                            m_value_enum_strings_not_found++;
                        }
                    }

                    // Save Last Value Enum String
                    // for Conditional STC Config Groups...
                    this->m_last_enum_string = m_value_enum_strings.back();
                }
            }
        }

        /// Writes Buffered Double PV Values to Nexus File 
        void flushValueBuffers
        (
            std::vector<double> &value_buffer ///< Double Buffer to Write
        )
        {
            // TODO - This code may need to be optimized
            // when fast metadata is supported
            m_nxgen.writeSlab( m_log_path + "/value",
                value_buffer, m_cur_size );

            // Did this PV's Value *Change* for the First Time here...?
            if ( !(this->m_value_changed) )
            {
                double value = ( this->m_last_value_set )
                    ? ( this->m_last_value ) : ( value_buffer.front() );
                for ( uint32_t i=0 ; i < value_buffer.size() ; i++ )
                {
                    // PV Value Changed...
                    if ( value_buffer[i] != value )
                    {
                        this->m_value_changed = true;
                        break;
                    }
                }
            }

            // Save Last Value for Conditional STC Config Groups
            this->m_last_value = value_buffer.back();
            this->m_last_value_set = true;

            // Are There More Than One Value to This PV Time-Series?
            if ( m_cur_size || value_buffer.size() > 1 )
                this->m_last_value_more = true;
        }

        /// Writes Buffered String PV Values to Nexus File 
        void flushValueBuffers
        (
            std::vector<std::string> &value_buffer ///< String Buffer to Write
        )
        {
            // Determine Max String Length...
            uint32_t max_len = (uint32_t) -1;
            for ( uint32_t i=0 ; i < value_buffer.size() ; i++ )
            {
                if ( max_len == (uint32_t) -1
                        || value_buffer[i].size() > max_len )
                {
                    max_len = value_buffer[i].size();
                }
            }
            // Make Sure We Don't Freak Out HDF5 No Matter What...
            if ( max_len == (uint32_t) -1 || max_len == 0 )
                max_len = 1;

            if ( m_nxgen.verbose() > 2 )
            {
                syslog( LOG_INFO,
                    "[%i] DASlogs String %s size=%lu max_len=%u",
                    g_pid, this->m_device_pv_str.c_str(),
                    value_buffer.size(), max_len );
                give_syslog_a_chance;
            }

            // Write 2D String Array to NeXus File...
            if ( value_buffer.size() )
            {
                std::vector<hsize_t> dims;
                dims.push_back( value_buffer.size() );
                dims.push_back( max_len );

                // Pad the Strings with Spaces to Be of Uniform Length...
                std::vector<std::string> value_vec;
                // (And See if PV's Value *Changed* for First Time here...)
                std::string value = ( this->m_last_value_set )
                    ? ( this->m_last_value ) : ( value_buffer.front() );
                for ( uint32_t i=0 ; i < value_buffer.size() ; i++ )
                {
                    std::string str = value_buffer[i];
                    // PV Value Changed...
                    if ( !(this->m_value_changed)
                            && value.compare( str ) )
                    {
                        this->m_value_changed = true;
                    }
                    if ( str.size() < max_len )
                        str.insert( str.end(), max_len - str.size(), ' ' );
                    value_vec.push_back( str );
                }
                m_nxgen.writeMultidimDataset( m_log_path,
                    "value", value_vec, dims, this->m_units );

                // Save Last Value for Conditional STC Config Groups
                this->m_last_value = value_buffer.back();
                this->m_last_value_set = true;

                // Are There More Than One Value to This PV Time-Series?
                if ( value_buffer.size() > 1 )
                    this->m_last_value_more = true;
            }
            else
            {
                syslog( LOG_INFO, "[%i] %s %s, %s", g_pid,
                    "No String Array Values for",
                    this->m_device_pv_str.c_str(),
                    "Creating Empty String Value" );
                give_syslog_a_chance;
                m_nxgen.makeDataset( m_log_path,
                    "value", NeXus::CHAR, this->m_units, 1 );
            }
        }

        /// Writes Buffered Uint32 Array PV Values to Nexus File 
        void flushValueBuffers
        (
            std::vector< std::vector<uint32_t> > &value_buffer ///< Uint32 Array Buffer to Write
        )
        {
            // Determine Max Array Length...
            uint32_t max_len = (uint32_t) -1;
            for ( uint32_t i=0 ; i < value_buffer.size() ; i++ )
            {
                if ( max_len == (uint32_t) -1
                        || value_buffer[i].size() > max_len )
                {
                    max_len = value_buffer[i].size();
                }
            }
            // Make Sure We Don't Freak Out HDF5 No Matter What...
            if ( max_len == (uint32_t) -1 || max_len == 0 )
                max_len = 1;

            syslog( LOG_INFO,
                "[%i] DASlogs Uint32 Array %s size=%lu max_len=%u",
                g_pid, this->m_device_pv_str.c_str(),
                value_buffer.size(), max_len );
            give_syslog_a_chance;

            // Write 2D Uint32 Array to NeXus File...
            if ( value_buffer.size() )
            {
                std::vector<hsize_t> dims;
                dims.push_back( value_buffer.size() );
                dims.push_back( max_len );

                // Pad the Arrays with Zeros to Be of Uniform Length...
                std::vector<uint32_t> value_vec;
                // (And See if PV's Value *Changed* for First Time here...)
                std::vector<uint32_t> &value = ( this->m_last_value_set )
                    ? ( this->m_last_value ) : ( value_buffer.front() );
                for ( uint32_t i=0 ; i < value_buffer.size() ; i++ )
                {
                    // Did PV Value Change...?
                    if ( !(this->m_value_changed) )
                    {
                        if ( value.size() != value_buffer[i].size() )
                            this->m_value_changed = true;
                        else
                        {
                            for ( uint32_t j=0 ; j < value.size() ; j++ )
                            {
                                if ( value[j] != (value_buffer[i])[j] )
                                {
                                    this->m_value_changed = true;
                                    break;
                                }
                            }
                        }
                    }
                    value_vec.reserve( value_vec.size() + max_len );
                    value_vec.insert( value_vec.end(),
                        value_buffer[i].begin(), value_buffer[i].end() );
                    if ( value_buffer[i].size() < max_len )
                    {
                        value_vec.insert( value_vec.end(),
                            max_len - value_buffer[i].size(), 0 );
                    }
                }
                m_nxgen.writeMultidimDataset( m_log_path,
                    "value", value_vec, dims, this->m_units );

                // Save Last Value for Conditional STC Config Groups
                this->m_last_value = value_buffer.back();
                this->m_last_value_set = true;

                // Are There More Than One Value to This PV Time-Series?
                if ( value_buffer.size() > 1 )
                    this->m_last_value_more = true;
            }
            else
            {
                syslog( LOG_INFO, "[%i] %s %s, %s", g_pid,
                    "No Uint32 Array Values for",
                    this->m_device_pv_str.c_str(),
                    "Creating Empty Value Array" );
                give_syslog_a_chance;
                m_nxgen.makeDataset( m_log_path,
                    "value", NeXus::UINT32, this->m_units, 1 );
            }
        }

        /// Writes Buffered Double Array PV Values to Nexus File 
        void flushValueBuffers
        (
            std::vector< std::vector<double> > &value_buffer ///< Double Array Buffer to Write
        )
        {
            // Determine Max Array Length...
            uint32_t max_len = (uint32_t) -1;
            for ( uint32_t i=0 ; i < value_buffer.size() ; i++ )
            {
                if ( max_len == (uint32_t) -1
                        || value_buffer[i].size() > max_len )
                {
                    max_len = value_buffer[i].size();
                }
            }
            // Make Sure We Don't Freak Out HDF5 No Matter What...
            if ( max_len == (uint32_t) -1 || max_len == 0 )
                max_len = 1;

            syslog( LOG_INFO,
                "[%i] DASlogs Double Array %s size=%lu max_len=%u",
                g_pid, this->m_device_pv_str.c_str(),
                value_buffer.size(), max_len );
            give_syslog_a_chance;

            // Write 2D Double Array to NeXus File...
            if ( value_buffer.size() )
            {
                std::vector<hsize_t> dims;
                dims.push_back( value_buffer.size() );
                dims.push_back( max_len );

                // Pad the Arrays with Zeros to Be of Uniform Length...
                std::vector<double> value_vec;
                // (And See if PV's Value *Changed* for First Time here...)
                std::vector<double> &value = ( this->m_last_value_set )
                    ? ( this->m_last_value ) : ( value_buffer.front() );
                for ( uint32_t i=0 ; i < value_buffer.size() ; i++ )
                {
                    // Did PV Value Change...?
                    if ( !(this->m_value_changed) )
                    {
                        if ( value.size() != value_buffer[i].size() )
                            this->m_value_changed = true;
                        else
                        {
                            for ( uint32_t j=0 ; j < value.size() ; j++ )
                            {
                                if ( !(approximatelyEqual(
                                        value[j], (value_buffer[i])[j],
                                        STC_DOUBLE_EPSILON )) )
                                {
                                    this->m_value_changed = true;
                                    break;
                                }
                            }
                        }
                    }
                    value_vec.reserve( value_vec.size() + max_len );
                    value_vec.insert( value_vec.end(),
                        value_buffer[i].begin(), value_buffer[i].end() );
                    if ( value_buffer[i].size() < max_len )
                    {
                        value_vec.insert( value_vec.end(),
                            max_len - value_buffer[i].size(), 0.0 );
                    }
                }
                m_nxgen.writeMultidimDataset( m_log_path,
                    "value", value_vec, dims, this->m_units );

                // Save Last Value for Conditional STC Config Groups
                this->m_last_value = value_buffer.back();
                this->m_last_value_set = true;

                // Are There More Than One Value to This PV Time-Series?
                if ( value_buffer.size() > 1 )
                    this->m_last_value_more = true;
            }
            else
            {
                syslog( LOG_INFO, "[%i] %s %s, %s", g_pid,
                    "No Double Array Values for",
                    this->m_device_pv_str.c_str(),
                    "Creating Empty Value Array" );
                give_syslog_a_chance;
                m_nxgen.makeDataset( m_log_path,
                    "value", NeXus::FLOAT64, this->m_units, 1 );
            }
        }

        /// Writes buffered PV values and time axis to Nexus file and performs finalization
        int32_t flushBuffers
        (
            uint64_t start_time,    ///< 1st Pulse Time (nanosecs), if set
            struct STC::RunMetrics *a_run_metrics   ///< If non-zero, indicates finalization code should be executed for this PV
        )
        {
            int32_t num_values = -1;

            try
            {
                // Handle Not-Yet-Normalized Variable Value Update Times...
                if ( this->m_has_non_normalized )
                {
                    // Have We Received Our 1st Pulse Yet...?
                    // If So, Then Fix Non-Normalized Value Updates Now.
                    if ( start_time )
                    {
                        if ( m_nxgen.verbose() > 1 ) {
                            syslog( LOG_INFO,
                                "[%i] %s %s %s %s, %s: %s",
                                g_pid, "NxPVInfo::flushBuffers()",
                                this->m_device_str.c_str(),
                                "Normalizing PV Value Times",
                                "with First Pulse Time",
                                "Now Available",
                                this->m_pv_str.c_str() );
                            give_syslog_a_chance;
                        }

                        this->normalizeTimestamps( start_time,
                            m_nxgen.verbose() );
                    }

                    // Nope, No 1st Pulse Time to Normalize From Yet...
                    // Not Soup Yet, Wait for Later...
                    // (Tho Log Error About It, Lest We Swell Up & Pop!)
                    else
                    {
                        // Rate-Limiting logging of
                        // Non-Normalized PV Buffer Full...
                        std::string log_info;
                        if ( RateLimitedLogging::checkLog(
                                m_nxgen.m_RLLHistory_NxGen,
                                RLL_NON_NORM_BUF_FULL, this->m_pv_str,
                                60, 10, 100, log_info ) ) {
                            syslog( LOG_ERR,
                                "[%i] %s: %s%s %s %s %s, %s: %s",
                                g_pid, "STC Error", log_info.c_str(),
                                "NxPVInfo::flushBuffers()",
                                this->m_device_str.c_str(),
                                "Non-Normalized PV Buffer Full",
                                "With No First Pulse Time Yet",
                                "Deferring For Now",
                                this->m_pv_str.c_str() );
                            give_syslog_a_chance;
                        }

                        return( -1 );
                    }
                }

                // Do We Have a Valid Initialized NeXus Data File...?
                // (We shouldn't get called if not, so if we do, better
                // force it, lest we actually lose PV value data...!!)
                if ( m_nxgen.m_gen_nexus
                        && !(m_nxgen.initialize( true,
                            "NxPVInfo::flushBuffers()" )) )
                {
                    syslog( LOG_ERR, "[%i] %s %s: %s - %s (%s %s)",
                        g_pid, "STC Error:", "NxPVInfo::flushBuffers()",
                        "Failed to Force Initialize NeXus File",
                        "Losing PV Value Data!!",
                        this->m_device_str.c_str(),
                        this->m_pv_str.c_str() );
                    give_syslog_a_chance;
                    return( 0 );
                }

                // Write PV Values to NeXus File _If_ We're Writing to
                // NeXus and _If_ We Care About This PV (_Not_ Ignored!)
                // :-D
                if ( m_nxgen.m_gen_nexus && !(this->m_ignore) )
                {
                    // If This PV is Marked as a "Duplicate" of
                    // Another PV Log, Hold Off Writing Its Values
                    // to the NeXus file.
                    // We Hope to Merge/Collapse All Duplicates
                    // Just Prior to the Finalization Pass... ;-D
                    if ( this->m_duplicate )
                    {
                        // TODO Add Rate-Limiting...?
                        syslog( LOG_WARNING, "[%i] %s: %s: %s for %s",
                            g_pid, "Warning", "NxPVInfo::flushBuffers()",
                            "Deferring Write of Duplicate PV Log",
                            this->m_device_pv_str.c_str() );
                        give_syslog_a_chance;

                        return( -1 );
                    }

                    // Wait for End of Run to Dump String/Array PV Types...
                    // (Need to Determine Max String Length for 2D Array)
                    if ( !a_run_metrics &&
                            ( this->m_type == STC::PVT_STRING
                                || this->m_type == STC::PVT_UINT_ARRAY
                                || this->m_type == STC::PVT_DOUBLE_ARRAY )
                    )
                    {
                        if ( !(this->m_full_buffer_count++ % 1000) )
                        {
                            syslog( LOG_ERR,
                                "[%i] %s: %s %s: %s, %s: %s",
                                g_pid, "STC Error",
                                "NxPVInfo::flushBuffers()",
                                this->m_device_str.c_str(),
                                "String/Array PV Buffer Full",
                                "Deferring to Run End",
                                this->m_pv_str.c_str() );
                            give_syslog_a_chance;
                        }
                        return( -1 );
                    }

                    // Create log if no data has been written yet
                    if ( !m_cur_size )
                    {
                        // "Nothing to See Here"...! ;-D
                        // (Never Got Any Data, and Don't Have Any Now,
                        //    So Just Ignore This PV and Move On...! ;-)
                        // [Need this check because _Always_ called
                        //    in finalizeStreamProcessing() now... ;-]
                        if ( !(this->m_value_buffer.size()) )
                            return( 0 );

                        m_nxgen.makeGroup( m_log_path, "NXlog" );

                        // Let String/Array PVs Create Their Own Values
                        // (e.g. 2D String/Numerical Arrays)
                        if ( this->m_type != STC::PVT_STRING
                                && this->m_type != STC::PVT_UINT_ARRAY
                                && this->m_type != STC::PVT_DOUBLE_ARRAY )
                        {
                            m_nxgen.makeDataset( m_log_path, "value",
                                m_nxgen.toNxType( this->m_type ),
                                this->m_units,
                                this->m_value_buffer.size() );
                        }

                        m_nxgen.makeDataset( m_log_path, "time",
                            NeXus::FLOAT64, TIME_SEC_UNITS,
                            this->m_time_buffer.size() );

                        m_nxgen.writeString( m_log_path, "device_name",
                            this->m_device_name );
                        m_nxgen.writeScalar( m_log_path, "device_id",
                            this->m_device_id, "" );
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

                    num_values = this->m_value_buffer.size();

                    // Do Final Statistics & Links Pass
                    // (Unless We've Already Done It, Duplicate Keys...)
                    if ( a_run_metrics && !(this->m_finalized) )
                    {
                        // Add start time (offset) properties
                        // to all time axis in DAS logs
                        std::string time = timeToISO8601(
                            a_run_metrics->run_start_time );
                        std::string time_path = m_log_path + "/time";
                        m_nxgen.writeStringAttribute( time_path,
                            "start", time );
                        m_nxgen.writeScalarAttribute( time_path,
                            "offset_seconds",
                            (uint32_t)a_run_metrics->run_start_time.tv_sec
                                - ADARA::EPICS_EPOCH_OFFSET );
                        m_nxgen.writeScalarAttribute( time_path,
                            "offset_nanoseconds",
                            (uint32_t)a_run_metrics->run_start_time.tv_nsec
                        );

                        if ( m_cur_size
                                // No Statistics for Strings!
                                && this->m_type != STC::PVT_STRING )
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
                        if ( this->m_enum_vector != NULL
                                && this->m_enum_index != (uint32_t) -1 )
                        {
                            STC::PVEnumeratedType *pv_enum =
                                &((*(this->m_enum_vector))[
                                    this->m_enum_index ]);

                            std::stringstream ss_src;
                            ss_src << m_nxgen.m_daslogs_path << "/"
                                << "Device" << this->m_device_id
                                << ":" << "Enum"
                                << ":" << pv_enum->name;

                            std::stringstream ss_dst;
                            ss_dst << m_log_path << "/" << "enum";

                            if ( m_nxgen.verbose() > 1 ) {
                                syslog( LOG_INFO,
                                    "[%i] %s %s: %s %s to %s",
                                    g_pid, "NxPVInfo::flushBuffers()",
                                    this->m_device_pv_str.c_str(),
                                    "Linking Enum Group",
                                    ss_src.str().c_str(),
                                    ss_dst.str().c_str() );
                                give_syslog_a_chance;
                            }

                            m_nxgen.makeGroupLink(
                                ss_src.str(), ss_dst.str() );

                            // Write Any String Value Array for Enum Types
                            if ( !m_value_enum_strings.empty() )
                            {
                                // NOW Log if we Couldn't Find Some
                                // Particular Enumerated Type Values...
                                if ( m_value_enum_strings_not_found > 0 )
                                {
                                    syslog( LOG_ERR,
                                        "[%i] %s: %s %s: %d %s %s %s!",
                                        g_pid, "STC Error",
                                        "NxPVInfo::flushBuffers()",
                                        this->m_device_str.c_str(),
                                        m_value_enum_strings_not_found,
                                        "Enumerated Type Value Strings",
                                        "Not Found For",
                                        this->m_pv_str.c_str() );
                                    give_syslog_a_chance;
                                }

                                // Make Sure We Don't Freak Out HDF5
                                // No Matter What...
                                if ( m_value_enum_strings_max_len
                                        == (uint32_t) -1
                                    || m_value_enum_strings_max_len == 0 )
                                {
                                    m_value_enum_strings_max_len = 1;
                                }

                                if ( m_nxgen.verbose() > 2 )
                                {
                                    syslog( LOG_ERR,
                                      "[%i] %s %s: %s for %s %s=%lu %s=%u",
                                        g_pid, "NxPVInfo::flushBuffers()",
                                        this->m_device_str.c_str(),
                                        "Enumerated Type Value Strings",
                                        this->m_pv_str.c_str(),
                                        "size",
                                        m_value_enum_strings.size(),
                                        "max_len",
                                        m_value_enum_strings_max_len );
                                    give_syslog_a_chance;
                                }

                                // Value Strings as 2D String Dataset
                                std::vector<hsize_t> dims;
                                dims.push_back(
                                    m_value_enum_strings.size() );
                                dims.push_back(
                                    m_value_enum_strings_max_len );

                                // Pad the Strings with Spaces
                                // to Be of Uniform Length...
                                std::vector<std::string> values_vec;
                                for ( uint32_t i=0 ;
                                        i < m_value_enum_strings.size() ;
                                        i++ )
                                {
                                    std::string str =
                                        m_value_enum_strings[i];
                                    if ( str.size()
                                          < m_value_enum_strings_max_len )
                                    {
                                        str.insert( str.end(),
                                            m_value_enum_strings_max_len
                                                - str.size(), ' ' );
                                    }
                                    values_vec.push_back( str );
                                }

                                m_nxgen.writeMultidimDataset( m_log_path,
                                    "value_strings", values_vec, dims );
                            }
                            else
                            {
                                syslog( LOG_ERR,
                                    "[%i] %s: %s %s: %s for %s - %s",
                                    g_pid, "STC Error",
                                    "NxPVInfo::flushBuffers()",
                                    this->m_device_str.c_str(),
                                    "Empty Enumerated Type Value Strings",
                                    this->m_pv_str.c_str(),
                                    "Creating Dummy Value Strings" );
                                give_syslog_a_chance;

                                m_nxgen.makeDataset( m_log_path,
                                    "value_strings", NeXus::CHAR, "", 1 );
                            }
                        }

                        // Last Pass, Create Any PV (Alias) Link Now!
                        if ( !m_link_path.empty() )
                        {
                            syslog( LOG_INFO,
                                "[%i] %s %s: %s %s to Alias %s",
                                g_pid, "NxPVInfo::flushBuffers()",
                                this->m_device_pv_str.c_str(),
                                "Linking PV Channel",
                                m_log_path.c_str(),
                                m_link_path.c_str() );
                            give_syslog_a_chance;

                            // Only Create "Target" String for Group Links
                            // if we haven't already done so... ;-D
                            if ( !m_has_link )
                            {
                                // Manually Create "Target" String for
                                // Group Link (as per makeGroupLink usage)
                                m_nxgen.writeString( m_log_path,
                                    "target", m_log_path );

                                // Mark This PV as Having Created the
                                // "Target" String for Group Links!
                                // (so we only do it _Once_!)
                                m_has_link = true;
                            }
                            else
                            {
                                syslog( LOG_INFO,
                                    "[%i] %s %s: PV Channel %s %s - %s",
                                    g_pid, "NxPVInfo::flushBuffers()",
                                    this->m_device_pv_str.c_str(),
                                    m_log_path.c_str(),
                                    "Already Has Target Group Link String",
                                    "Skipping..." );
                                give_syslog_a_chance;
                            }

                            m_nxgen.makeGroupLink(
                                m_log_path, m_link_path );
                        }

                        // Search STC Config for Associated Groups
                        if ( m_nxgen.m_config_groups.size() )
                            createSTCConfigGroups();

                        // Done, We've Finalized This PV Log.
                        this->m_finalized = true;
                    }

                    // Log That We had a Duplicate Key & Finalized "Twice"
                    else if ( a_run_metrics && this->m_finalized )
                    {
                        syslog( LOG_WARNING,
                            "[%i] Warning in %s: %s: PV %s",
                            g_pid, "NxPVInfo::flushBuffers()",
                            this->m_device_pv_str.c_str(),
                            "Was Already Finalized, Duplicate Key...?" );
                        give_syslog_a_chance;
                    }
                }

                // Else Ignore This PV...
                // (or Maybe It's a Subsumed Duplicate?)
                else if ( m_nxgen.m_gen_nexus && this->m_ignore )
                {
                    syslog( LOG_INFO, "[%i] %s %s - Ignoring %s%s",
                        g_pid, "NxPVInfo::flushBuffers()",
                        this->m_device_str.c_str(),
                        ( ( this->m_duplicate ) ? "Duplicate " : "" ),
                        this->m_pv_str.c_str() );
                    give_syslog_a_chance;
                    num_values = 0;

                    // For Subsumed Duplicate PV Logs, We May
                    // Still Need to Create Any Alias Links on Final Pass
                    // (Unless We've Already Done It, Duplicate Keys...)
                    if ( this->m_duplicate && a_run_metrics
                            && !(this->m_finalized)
                            && !m_link_path.empty() )
                    {
                        syslog( LOG_INFO,
                            "[%i] %s %s: %s %s to Alias %s",
                            g_pid, "NxPVInfo::flushBuffers()",
                            this->m_device_pv_str.c_str(),
                            "Linking Duplicate PV Channel",
                            m_log_path.c_str(),
                            m_link_path.c_str() );
                        give_syslog_a_chance;

                        // Only Create "Target" String for Group Links
                        // if we haven't already done so... ;-D
                        // (For a Duplicate PV, We Have to Manually Check!)
                        bool exists = false;
                        m_nxgen.checkDataset(
                            m_log_path, "target", exists );
                        if ( !m_has_link && !exists )
                        {
                            // Manually Create "Target" String for
                            // Group Link (as per makeGroupLink usage)
                            m_nxgen.writeString( m_log_path,
                                "target", m_log_path );

                            // Mark This PV as Having Created the
                            // "Target" String for Group Links!
                            // (so we only do it _Once_!)
                            m_has_link = true;
                        }
                        else
                        {
                            syslog( LOG_INFO,
                                "[%i] %s %s: %s PV Channel %s %s - %s",
                                g_pid, "NxPVInfo::flushBuffers()",
                                this->m_device_pv_str.c_str(),
                                "Duplicate", m_log_path.c_str(),
                                "Already Has Target Group Link String",
                                "Skipping..." );
                            give_syslog_a_chance;
                        }

                        m_nxgen.makeGroupLink(
                            m_log_path, m_link_path );

                        // Search STC Config for Associated Groups
                        // NOTE: All of This will "Still Work" for
                        // Any Subsumed Duplicate PVs "By Alias",
                        // _Except_ for Any Links to the "Last PV Value",
                        // which may Erroneously Grab _This Duplicate PV's_
                        // Last Value, Rather than the Overall Last
                        // Value for the Combined PV Log... ;-b
                        // (This could be fixed, but probably unnecessary.)
                        if ( m_nxgen.m_config_groups.size() )
                            createSTCConfigGroups();

                        // Done, We've Finalized This PV Log.
                        this->m_finalized = true;
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

            return( num_values );
        }

        /// Compare Uint32 PV Value to Conditional STC Config Group Strings
        bool matchValues
        (
            uint32_t value,                 ///< Uint32 Value to Match
            std::vector<std::string> values ///< Conditional Values
        )
        {
            // Did We Have a "Last Value" for This PV...?
            if ( !(this->m_last_value_set) )
                return( false );

            // Compare PV Value to Conditional STC Config Group Values
            for ( uint32_t i=0 ; i < values.size() ; i++ )
            {
                uint32_t val = boost::lexical_cast<uint32_t>( values[i] );
                if ( val == value )
                {
                    syslog( LOG_INFO, "[%i] Value Match (%u)",
                        g_pid, value );
                    give_syslog_a_chance;
                    return( true );
                }
            }
            return( false );
        }

        /// Compare Double PV Value to Conditional STC Config Group Strings
        bool matchValues
        (
            double value,                   ///< Double Value to Match
            std::vector<std::string> values ///< Conditional Values
        )
        {
            // Did We Have a "Last Value" for This PV...?
            if ( !(this->m_last_value_set) )
                return( false );

            // Compare PV Value to Conditional STC Config Group Values
            for ( uint32_t i=0 ; i < values.size() ; i++ )
            {
                double val = boost::lexical_cast<double>( values[i] );
                if ( approximatelyEqual( val, value, STC_DOUBLE_EPSILON ) )
                {
                    syslog( LOG_INFO, "[%i] Value Match (%lf)",
                        g_pid, value );
                    give_syslog_a_chance;
                    return( true );
                }
            }
            return( false );
        }

        /// Compare String PV Value to Conditional STC Config Group Strings
        bool matchValues
        (
            std::string value,              ///< String Value to Match
            std::vector<std::string> values ///< Conditional Values
        )
        {
            // Did We Have a "Last Value" for This PV...?
            if ( !(this->m_last_value_set) )
                return( false );

            // Compare PV Value to Conditional STC Config Group Values
            for ( uint32_t i=0 ; i < values.size() ; i++ )
            {
                if ( !(value.compare( values[i] )) )
                {
                    syslog( LOG_INFO, "[%i] Value Match (%s)",
                        g_pid, value.c_str() );
                    give_syslog_a_chance;
                    return( true );
                }
            }
            return( false );
        }

        /// Compare Uint32 PV Array to Conditional STC Config Group Strings
        bool matchValues
        (
            std::vector<uint32_t> value,    ///< Uint32 Array to Match
            std::vector<std::string> values ///< Conditional Values
        )
        {
            // Did We Have a "Last Value" for This PV...?
            if ( !(this->m_last_value_set) )
                return( false );

            // Compare PV Value to Conditional STC Config Group Values
            for ( uint32_t i=0 ; i < values.size() ; i++ )
            {
                uint32_t val = boost::lexical_cast<uint32_t>( values[i] );

                // Compare _Each_ PV Array Value to the
                // Conditional STC Config Group Values...
                // (Meh, it's something... ;-b)
                for ( uint32_t j=0 ; j < value.size() ; j++ )
                {
                    if ( val == value[j] )
                    {
                        syslog( LOG_INFO, "[%i] Value[%d] Match (%u)",
                            g_pid, j, value[j] );
                        give_syslog_a_chance;
                        return( true );
                    }
                }
            }
            return( false );
        }

        /// Compare Double PV Array to Conditional STC Config Group Strings
        bool matchValues
        (
            std::vector<double> value,      ///< Double Array to Match
            std::vector<std::string> values ///< Conditional Values
        )
        {
            // Did We Have a "Last Value" for This PV...?
            if ( !(this->m_last_value_set) )
                return( false );

            // Compare PV Value to Conditional STC Config Group Values
            for ( uint32_t i=0 ; i < values.size() ; i++ )
            {
                double val = boost::lexical_cast<double>( values[i] );

                // Compare _Each_ PV Array Value to the
                // Conditional STC Config Group Values...
                // (Meh, it's something... ;-b)
                for ( uint32_t j=0 ; j < value.size() ; j++ )
                {
                    if ( approximatelyEqual( val, value[j],
                            STC_DOUBLE_EPSILON ) )
                    {
                        syslog( LOG_INFO, "[%i] Value[%d] Match (%lf)",
                            g_pid, j, value[j] );
                        give_syslog_a_chance;
                        return( true );
                    }
                }
            }
            return( false );
        }

        /// Not-Compare Uint32 PV Value to Conditional Config Group Strings
        bool notMatchValues
        (
            uint32_t value,                 ///< Uint32 Value to Not-Match
            std::vector<std::string> values ///< Conditional Values
        )
        {
            // Did We Have a "Last Value" for This PV...?
            if ( !(this->m_last_value_set) )
                return( false );

            // Not-Compare PV Value to Conditional STC Config Group Values
            for ( uint32_t i=0 ; i < values.size() ; i++ )
            {
                uint32_t val = boost::lexical_cast<uint32_t>( values[i] );
                if ( val != value )
                {
                    syslog( LOG_INFO, "[%i] Value Not-Match (%u)",
                        g_pid, value );
                    give_syslog_a_chance;
                    return( true );
                }
            }
            return( false );
        }

        /// Not-Compare Double PV Value to Conditional Config Group Strings
        bool notMatchValues
        (
            double value,                   ///< Double Value to Not-Match
            std::vector<std::string> values ///< Conditional Values
        )
        {
            // Did We Have a "Last Value" for This PV...?
            if ( !(this->m_last_value_set) )
                return( false );

            // Not-Compare PV Value to Conditional STC Config Group Values
            for ( uint32_t i=0 ; i < values.size() ; i++ )
            {
                double val = boost::lexical_cast<double>( values[i] );
                if ( !approximatelyEqual( val, value,
                        STC_DOUBLE_EPSILON ) )
                {
                    syslog( LOG_INFO, "[%i] Value Not-Match (%lf)",
                        g_pid, value );
                    give_syslog_a_chance;
                    return( true );
                }
            }
            return( false );
        }

        /// Not-Compare String PV Value to Conditional Config Group Strings
        bool notMatchValues
        (
            std::string value,              ///< String Value to Not-Match
            std::vector<std::string> values ///< Conditional Values
        )
        {
            // Did We Have a "Last Value" for This PV...?
            if ( !(this->m_last_value_set) )
                return( false );

            // Not-Compare PV Value to Conditional STC Config Group Values
            for ( uint32_t i=0 ; i < values.size() ; i++ )
            {
                if ( value.compare( values[i] ) )
                {
                    syslog( LOG_INFO, "[%i] Value Not-Match (%s)",
                        g_pid, value.c_str() );
                    give_syslog_a_chance;
                    return( true );
                }
            }
            return( false );
        }

        /// Not-Compare Uint32 PV Array to Conditional Config Group Strings
        bool notMatchValues
        (
            std::vector<uint32_t> value,    ///< Uint32 Array to Not-Match
            std::vector<std::string> values ///< Conditional Values
        )
        {
            // Did We Have a "Last Value" for This PV...?
            if ( !(this->m_last_value_set) )
                return( false );

            // Not-Compare PV Value to Conditional STC Config Group Values
            for ( uint32_t i=0 ; i < values.size() ; i++ )
            {
                uint32_t val = boost::lexical_cast<uint32_t>( values[i] );

                // Not-Compare _Each_ PV Array Value to the
                // Conditional STC Config Group Values...
                // (Meh, this is really terrible... ;-b)
                for ( uint32_t j=0 ; j < value.size() ; j++ )
                {
                    if ( val != value[j] )
                    {
                        syslog( LOG_INFO, "[%i] Value[%d] Not-Match (%u)",
                            g_pid, j, value[j] );
                        give_syslog_a_chance;
                        return( true );
                    }
                }
            }
            return( false );
        }

        /// Not-Compare Double PV Array to Conditional Config Group Strings
        bool notMatchValues
        (
            std::vector<double> value,      ///< Double Array to Not-Match
            std::vector<std::string> values ///< Conditional Values
        )
        {
            // Did We Have a "Last Value" for This PV...?
            if ( !(this->m_last_value_set) )
                return( false );

            // Not-Compare PV Value to Conditional STC Config Group Values
            for ( uint32_t i=0 ; i < values.size() ; i++ )
            {
                double val = boost::lexical_cast<double>( values[i] );

                // Not-Compare _Each_ PV Array Value to the
                // Conditional STC Config Group Values...
                // (Meh, this is really terrible... ;-b)
                for ( uint32_t j=0 ; j < value.size() ; j++ )
                {
                    if ( !approximatelyEqual( val, value[j],
                            STC_DOUBLE_EPSILON ) )
                    {
                        syslog( LOG_INFO, "[%i] Value[%d] Not-Match (%lf)",
                            g_pid, j, value[j] );
                        give_syslog_a_chance;
                        return( true );
                    }
                }
            }
            return( false );
        }

        /// Compare PV Enum String to Conditional STC Config Group Strings
        bool matchValueStrings
        (
            std::string enum_string,        ///< Enum String to Match
            std::vector<std::string> values ///< Conditional Values
        )
        {
            // Did We Have a "Last Value" for This PV...?
            if ( !(this->m_last_value_set) )
                return( false );

            // Compare PV Enum String to
            // Conditional STC Config Group Values
            for ( uint32_t i=0 ; i < values.size() ; i++ )
            {
                if ( !(enum_string.compare( values[i] )) )
                {
                    syslog( LOG_INFO, "[%i] Value String Match (%s)",
                        g_pid, enum_string.c_str() );
                    give_syslog_a_chance;
                    return( true );
                }
            }
            return( false );
        }

        /// Not-Compare PV Enum String to Conditional Config Group Strings
        bool notMatchValueStrings
        (
            std::string enum_string,        ///< Enum String to Not-Match
            std::vector<std::string> values ///< Conditional Values
        )
        {
            // Did We Have a "Last Value" for This PV...?
            if ( !(this->m_last_value_set) )
                return( false );

            // Not-Compare PV Enum String to
            // Conditional STC Config Group Values
            for ( uint32_t i=0 ; i < values.size() ; i++ )
            {
                if ( enum_string.compare( values[i] ) )
                {
                    syslog( LOG_INFO, "[%i] Value String Not-Match (%s)",
                        g_pid, enum_string.c_str() );
                    give_syslog_a_chance;
                    return( true );
                }
            }
            return( false );
        }

        /// Write Scalar Uint32 PV Value to NeXus
        void writeScalarValue
        (
            std::string path,
            std::string name,
            uint32_t value,                ///< Uint32 Value
            std::string units
        )
        {
            m_nxgen.writeScalar( path, name, value, units );
        }

        /// Write Scalar Double PV Value to NeXus
        void writeScalarValue
        (
            std::string path,
            std::string name,
            double value,                  ///< Double Value
            std::string units
        )
        {
            m_nxgen.writeScalar( path, name, value, units );
        }

        /// Write Scalar String PV Value to NeXus
        void writeScalarValue
        (
            std::string path,
            std::string name,
            std::string value,             ///< String Value
            std::string units
        )
        {
            m_nxgen.writeString( path, name, value );
            if ( units.size() && units.compare("(unset)") ) {
                m_nxgen.writeStringAttribute( path + "/" + name,
                    "units", units );
            }
        }

        /// Write "Scalar" Uint32 PV Array to NeXus
        void writeScalarValue
        (
            std::string path,
            std::string name,
            std::vector<uint32_t> value,   ///< Uint32 Array
            std::string units
        )
        {
            std::vector<hsize_t> dims;
            dims.push_back( value.size() );

            m_nxgen.writeMultidimDataset( path, name, value, dims, units );
        }

        /// Write "Scalar" Double PV Array to NeXus
        void writeScalarValue
        (
            std::string path,
            std::string name,
            std::vector<double> value,     ///< Double Array
            std::string units
        )
        {
            std::vector<hsize_t> dims;
            dims.push_back( value.size() );

            m_nxgen.writeMultidimDataset( path, name, value, dims, units );
        }

        /// Search STC Config for Associated Groups & Create...
        void createSTCConfigGroupMatchingElements
        (
            struct GroupInfo *G,                        ///< Config Group
            std::vector<struct ElementInfo> &elements,  ///< Elements
            std::string label                           ///< Logging Label
        )
        {
            //syslog( LOG_INFO, "[%i] %s: Checking for Group \"%s\"...",
                //g_pid, "createSTCConfigGroupMatchingElements()",
                //G->name.c_str() );
            //give_syslog_a_chance;

            for ( uint32_t e=0 ; e < elements.size() ; e++ )
            {
                struct ElementInfo *E = &(elements[e]);

                bool matched = false;

                // Check for Matching Elements to Link...
                for ( uint32_t p=0 ;
                        p < E->patterns.size() && !matched ; p++ )
                {
                    std::string &P = E->patterns[p];

                    std::string patt_str = label.c_str();
                    patt_str += "Element Pattern \"" + P + "\"";

                    //syslog( LOG_INFO, "[%i] %s: %s %s Match", g_pid,
                        //"createSTCConfigGroupMatchingElements()",
                        //"Checking for", patt_str.c_str() );
                    //give_syslog_a_chance;

                    // Does PV Match This Group's Regex Pattern?
                    boost::regex expr( P );
                    boost::smatch subs;
                    if ( boost::regex_search(
                            this->m_internal_name, subs, expr )
                        || boost::regex_search(
                            this->m_internal_connection, subs, expr ) )
                    {
                        if ( m_nxgen.verbose() > 2 )
                        {
                            syslog( LOG_INFO,
                                "[%i] %s: %s %s in %s \"%s\" %s", g_pid,
                                "createSTCConfigGroupMatchingElements()",
                                "Pattern Match for",
                                this->m_device_pv_str.c_str(),
                                "Group", G->name.c_str(),
                                patt_str.c_str() );
                            give_syslog_a_chance;
                        }

                        std::string group_path;

                        // Indexed Groups...
                        if ( G->hasIndex )
                        {
                            std::stringstream ss_index;
                            ss_index <<
                                 "createSTCConfigGroupMatchingElements():";

                            std::string indexedName;

                            uint32_t index = -1;

                            bool gotIndex = false;

                            for ( uint32_t i=0 ;
                                    i < E->indices.size() && !gotIndex ;
                                    i++ )
                            {
                                std::string &I = E->indices[i];

                                if ( sscanf( this->m_internal_name.c_str(),
                                        I.c_str(), &index ) == 1 )
                                {
                                    ss_index << " Index \"" << I << "\""
                                        << " Matched Internal Name \""
                                        << this->m_internal_name << "\""
                                        << " as " << index;

                                    gotIndex = true;
                                }

                                else if ( sscanf(
                                        this->m_internal_connection
                                            .c_str(),
                                        I.c_str(), &index ) == 1 )
                                {
                                    ss_index << " Index \"" << I << "\""
                                        << " Matched Internal"
                                        << " Connection \""
                                        << this->m_internal_connection
                                        << "\"" << " as " << index;

                                    gotIndex = true;
                                }
                            }

                            if ( gotIndex )
                            {
                                size_t start =
                                    G->name.find( GroupNameIndex );

                                if ( start == std::string::npos )
                                {
                                    syslog( LOG_ERR,
                                      "[%i] %s %s - %s \"%s\" for %s (%s)",
                                        g_pid, "STC Error:",
                                        ss_index.str().c_str(),
                                        "Index Not Found in Group Name",
                                        G->name.c_str(),
                                        this->m_device_pv_str.c_str(),
                                        patt_str.c_str() );
                                    give_syslog_a_chance;

                                    continue;
                                }

                                else
                                {
                                    std::stringstream ss;
                                    ss << index;

                                    indexedName = G->name;
                                    indexedName.replace( start,
                                        GroupNameIndex.length(),
                                        ss.str() );

                                    if ( m_nxgen.verbose() > 0 ) {
                                        syslog( LOG_INFO,
                                         "[%i] %s - %s %s as %s \"%s\" %s",
                                            g_pid, ss_index.str().c_str(),
                                            "Indexed Group Name Found for",
                                            this->m_device_pv_str.c_str(),
                                            "Group", indexedName.c_str(),
                                            patt_str.c_str() );
                                        give_syslog_a_chance;
                                    }

                                    group_path =
                                        G->path + "/" + indexedName;
                                }
                            }

                            else
                            {
                                syslog( LOG_ERR,
                                    "[%i] %s %s: %s %s in %s \"%s\" %s",
                                    g_pid, "STC Error:",
                                  "createSTCConfigGroupMatchingElements()",
                                    "No Index Found for",
                                    this->m_device_pv_str.c_str(),
                                    "Group", G->name.c_str(),
                                    patt_str.c_str() );
                                give_syslog_a_chance;

                                continue;
                            }

                            // Create Group If Not Yet Created...
                            if ( G->createdIndices.find( indexedName )
                                    == G->createdIndices.end() )
                            {
                                // If Config Group Matches "/entry/sample"
                                // and We're Receiving Sample Meta-Data
                                // via the RunInfo Structure, Skip It! ;-D
                                if ( ! m_nxgen.getNoSampleInfo()
                                        && ! group_path.compare(
                                            "/entry/sample") )
                                {
                                    std::stringstream ss;
                                    ss << "*** Sample Meta-Data Already"
                                        << " Provided in RunInfo"
                                        << " - Skipping Indexed Group"
                                        << " \"" << indexedName << "\""
                                        << " for "
                                        << this->m_device_pv_str;
                                    syslog( LOG_ERR,
                                        "[%i] %s %s: %s, %s=[%s] %s=[%s]",
                                        g_pid, "STC Error:",
                                  "createSTCConfigGroupMatchingElements()",
                                        ss.str().c_str(),
                                        "path", group_path.c_str(),
                                        "type", G->type.c_str() );
                                    give_syslog_a_chance;

                                    continue;
                                }

                                // Allow Group Elements to Be Created
                                // Directly in Top-Level DASlogs Group...
                                // (Just Skip Creating the Group Though!)
                                if ( ! group_path.compare(
                                        m_nxgen.m_daslogs_path ) )
                                {
                                    std::stringstream ss;
                                    ss << "Skip Creation of Indexed Group"
                                        << " for Top-Level DASlogs Usage"
                                        << " - \"" << indexedName << "\""
                                        << " for "
                                        << this->m_device_pv_str;
                                    syslog( LOG_INFO,
                                        "[%i] %s: %s, %s=[%s] %s=[%s]",
                                        g_pid,
                                  "createSTCConfigGroupMatchingElements()",
                                        ss.str().c_str(),
                                        "path", group_path.c_str(),
                                        "type", G->type.c_str() );
                                    give_syslog_a_chance;
                                }

                                else
                                {
                                    if ( m_nxgen.verbose() > 0 ) {
                                        syslog( LOG_INFO,
                                     "[%i] %s: %s \"%s\", %s=[%s] %s=[%s]",
                                            g_pid,
                                  "createSTCConfigGroupMatchingElements()",
                                            "Creating Indexed Group",
                                            indexedName.c_str(),
                                            "path", group_path.c_str(),
                                            "type", G->type.c_str() );
                                        give_syslog_a_chance;
                                    }

                                    m_nxgen.makeGroup( group_path,
                                        G->type );
                                }

                                G->createdIndices.insert( indexedName );
                            }
                        }

                        // Non-Indexed Groups...
                        else
                        {
                            group_path = G->path + "/" + G->name;

                            // Create Group if Not Yet Created
                            if ( !(G->created) )
                            {
                                // If Config Group Matches "/entry/sample"
                                // and We're Receiving Sample Meta-Data
                                // via the RunInfo Structure, Skip It! ;-D
                                if ( ! m_nxgen.getNoSampleInfo()
                                        && ! group_path.compare(
                                            "/entry/sample") )
                                {
                                    std::stringstream ss;
                                    ss << "*** Sample Meta-Data Already"
                                        << " Provided in RunInfo"
                                        << " - Skipping Group"
                                        << " \"" << G->name << "\""
                                        << " for "
                                        << this->m_device_pv_str;
                                    syslog( LOG_ERR,
                                        "[%i] %s %s: %s, %s=[%s] %s=[%s]",
                                        g_pid, "STC Error:",
                                  "createSTCConfigGroupMatchingElements()",
                                        ss.str().c_str(),
                                        "path", group_path.c_str(),
                                        "type", G->type.c_str() );
                                    give_syslog_a_chance;

                                    continue;
                                }

                                // Allow Group Elements to Be Created
                                // Directly in Top-Level DASlogs Group...
                                // (Just Skip Creating the Group Though!)
                                if ( ! group_path.compare(
                                        m_nxgen.m_daslogs_path ) )
                                {
                                    std::stringstream ss;
                                    ss << "Skip Creation of Group"
                                        << " for Top-Level DASlogs Usage"
                                        << " - \"" << G->name << "\""
                                        << " for "
                                        << this->m_device_pv_str;
                                    syslog( LOG_INFO,
                                        "[%i] %s: %s, %s=[%s] %s=[%s]",
                                        g_pid,
                                  "createSTCConfigGroupMatchingElements()",
                                        ss.str().c_str(),
                                        "path", group_path.c_str(),
                                        "type", G->type.c_str() );
                                    give_syslog_a_chance;
                                }

                                else
                                {
                                    if ( m_nxgen.verbose() > 0 ) {
                                        syslog( LOG_INFO,
                                     "[%i] %s: %s \"%s\", %s=[%s] %s=[%s]",
                                            g_pid,
                                  "createSTCConfigGroupMatchingElements()",
                                            "Creating Group",
                                                G->name.c_str(),
                                            "path", group_path.c_str(),
                                            "type", G->type.c_str() );
                                        give_syslog_a_chance;
                                    }

                                    m_nxgen.makeGroup( group_path,
                                        G->type );
                                }

                                G->created = true;
                            }
                        }

                        std::string elem_link_path =
                            group_path + "/" + E->name;

                        // Create Element Link if Not Yet Created...!
                        std::map<std::string, std::string>::iterator it;
                        if ( (it = E->createdLinks.find( elem_link_path ))
                                == E->createdLinks.end() )
                        {
                            // Create Dataset with *Just Last PV Value*
                            // in Group...
                            if ( E->linkLastValue )
                            {
                                // Do We Have a "Last PV Value" to Use...?
                                if ( this->m_last_value_set )
                                {
                                    std::string pv_value_path =
                                        m_log_path + "/" + "value";

                                    if ( m_nxgen.verbose() > 0 ) {
                                        syslog( LOG_INFO,
                                        "[%i] %s: %s %s %s in Group as %s",
                                            g_pid,
                                  "createSTCConfigGroupMatchingElements()",
                                            "Create Data with",
                                            "Last PV Value from",
                                            pv_value_path.c_str(),
                                            elem_link_path.c_str() );
                                        give_syslog_a_chance;
                                    }

                                    writeScalarValue(
                                        group_path, E->name, 
                                        this->m_last_value,
                                        this->m_units );

                                    std::pair<std::string, std::string>
                                        path_link_pair(
                                           elem_link_path, pv_value_path );

                                    E->createdLinks.insert(
                                        path_link_pair );

                                    // IF We Have a Chance of Capturing a
                                    // Units Value from some PV(s), then
                                    // Save ElementInfo Link Path Now for
                                    // Setting the Units Attribute Later...
                                    // (After We've Gone Thru All the PVs)
                                    if ( E->unitsPatterns.size()
                                            || E->units.size() )
                                    {
                                        E->unitsPaths.insert(
                                            path_link_pair );
                                    }

                                    // Log Error if There Were More Than 1
                                    // Values in This PV Time-Series Log!
                                    if ( this->m_last_value_more
                                            && this->m_value_changed )
                                    {
                                        syslog( LOG_ERR,
                                            "[%i] %s %s: %s %s %s - %s %s",
                                            g_pid, "STC Error:",
                                  "createSTCConfigGroupMatchingElements()",
                                            "More Than 1 PV Value for",
                                            elem_link_path.c_str(),
                                            "to Link to Element Path",
                                            "Check", m_log_path.c_str() );
                                        give_syslog_a_chance;
                                    }
                                }

                                else
                                {
                                    syslog( LOG_ERR,
                                        "[%i] %s %s: %s %s - %s - %s %s",
                                        g_pid, "STC Error:",
                                  "createSTCConfigGroupMatchingElements()",
                                        "*** NO LAST PV VALUE for",
                                        elem_link_path.c_str(),
                                        "Nothing to Link to Element Path",
                                        "Skipping", m_log_path.c_str() );
                                    give_syslog_a_chance;
                                }
                            }

                            // Link PV *Value* Only into Group...
                            else if ( E->linkValue )
                            {
                                std::string pv_value_path =
                                    m_log_path + "/" + "value";

                                if ( m_nxgen.verbose() > 0 ) {
                                    syslog( LOG_INFO,
                                        "[%i] %s: %s %s to Group as %s",
                                        g_pid,
                                  "createSTCConfigGroupMatchingElements()",
                                        "Linking PV Value",
                                        pv_value_path.c_str(),
                                        elem_link_path.c_str() );
                                    give_syslog_a_chance;
                                }

                                // Make Sure Target Group/Dataset Exists
                                // Before Trying to Link to It...! ;-D
                                bool exists = false;
                                m_nxgen.checkDataset(
                                    m_log_path, "value", exists );

                                // Group/Dataset Exists, Proceed...
                                if ( exists )
                                {
                                    m_nxgen.makeLink(
                                        pv_value_path, elem_link_path );

                                    std::pair<std::string, std::string>
                                        path_link_pair(
                                            elem_link_path,
                                                pv_value_path );

                                    E->createdLinks.insert(
                                        path_link_pair );

                                    // IF We Have a Chance of Capturing a
                                    // Units Value from some PV(s), then
                                    // Save ElementInfo Link Path Now for
                                    // Setting the Units Attribute Later..!
                                    // (_After_ We've Gone Thru All the
                                    // PVs!)
                                    if ( E->unitsPatterns.size()
                                            || E->units.size() )
                                    {
                                        E->unitsPaths.insert(
                                            path_link_pair );
                                    }
                                }

                                // Group/Dataset *Doesn't* Exist...!
                                // Don't Try to Link...!
                                else
                                {
                                    syslog( LOG_ERR,
                                     "[%i] %s %s: %s %s to Group as %s %s",
                                        g_pid, "STC Error:",
                                  "createSTCConfigGroupMatchingElements()",
                                        "Can't Link PV Value",
                                        pv_value_path.c_str(),
                                        elem_link_path.c_str(),
                                        "- PV Log/Value Does Not Exist!" );
                                    give_syslog_a_chance;

                                    // Don't Call This A "Match" Yet,
                                    // Let's Give Someone Else A Try...
                                    continue;
                                }
                            }

                            // Link Whole PV Log into Group...
                            else
                            {
                                if ( m_nxgen.verbose() > 0 ) {
                                    syslog( LOG_INFO,
                                        "[%i] %s: %s %s to Group in %s",
                                        g_pid,
                                  "createSTCConfigGroupMatchingElements()",
                                        "Linking PV Channel",
                                        m_log_path.c_str(),
                                        elem_link_path.c_str() );
                                    give_syslog_a_chance;
                                }

                                // Make Sure Target Group/Dataset Exists
                                // Before Trying to Link to It...! ;-D
                                bool exists = false;
                                m_nxgen.checkDataset(
                                    m_log_path, "", exists );

                                // Group/Dataset Exists, Proceed...
                                if ( exists )
                                {
                                    // Only Create "Target" String for
                                    // Group Links if we haven't already
                                    // done so... ;-D
                                    if ( !m_has_link )
                                    {
                                        // Manually Create "Target" String
                                        // for Group Link (as per
                                        // makeGroupLink)
                                        m_nxgen.writeString( m_log_path,
                                            "target", m_log_path );

                                        // Mark This PV as Having Created
                                        // the "Target" String for Group
                                        // Links! (so we only do it
                                        // _Once_!)
                                        m_has_link = true;
                                    }
                                    else
                                    {
                                        syslog( LOG_INFO,
                                        "[%i] %s: %s %s %s %s - %s", g_pid,
                                  "createSTCConfigGroupMatchingElements()",
                                            "PV Channel",
                                            m_log_path.c_str(),
                                            "Already Has",
                                            "Target Group Link String",
                                            "Skipping..." );
                                        give_syslog_a_chance;
                                    }

                                    m_nxgen.makeGroupLink(
                                        m_log_path, elem_link_path );

                                    E->createdLinks.insert(
                                        std::pair<std::string,
                                                std::string>(
                                            elem_link_path, m_log_path ) );
                                }

                                // Group/Dataset *Doesn't* Exist...!
                                // Don't Try to Link...!
                                else
                                {
                                    syslog( LOG_ERR,
                                     "[%i] %s %s: %s %s to Group as %s %s",
                                        g_pid, "STC Error:",
                                  "createSTCConfigGroupMatchingElements()",
                                        "Can't Link PV Channel",
                                        m_log_path.c_str(),
                                        elem_link_path.c_str(),
                                        "- PV Log Does Not Exist!" );
                                    give_syslog_a_chance;

                                    // Don't Call This A "Match" Yet,
                                    // Let's Give Someone Else A Try...
                                    continue;
                                }
                            }
                        }

                        // Log Error for Duplicate Element Link Attempt!
                        else
                        {
                            syslog( LOG_ERR,
                                "[%i] %s %s: %s %s - %s %s %s - %s %s",
                                g_pid, "STC Error:",
                                "createSTCConfigGroupMatchingElements()",
                                "*** DUPLICATE Element Link Attempt for",
                                elem_link_path.c_str(),
                                "PV/Log Path", it->second.c_str(),
                                "Already Linked to Element Path",
                                "Skipping", m_log_path.c_str() );
                            give_syslog_a_chance;
                        }

                        matched = true;
                    }
                }

                // Don't Bother Checking for Matching Units Patterns
                // If the PV's Value Hasn't Been Set Anyway... ;-D
                if ( !(this->m_last_value_set) )
                    continue;

                // Also Check for Matching Units Patterns...
                for ( uint32_t u=0 ;
                        u < E->unitsPatterns.size() ; u++ )
                {
                    std::string &U = E->unitsPatterns[u];

                    std::string units_patt_str = label.c_str();
                    units_patt_str += "Element Units Pattern \""
                        + U + "\"";

                    //syslog( LOG_INFO,
                        //"[%i] %s: %s %s Units Match", g_pid,
                        //"createSTCConfigGroupMatchingElements()",
                        //"Checking for",
                        //units_patt_str.c_str() );
                    //give_syslog_a_chance;

                    // Does PV Match This Element Units Regex Pattern?
                    boost::regex expr( U );
                    boost::smatch subs;
                    if ( boost::regex_search(
                            this->m_internal_name, subs, expr )
                        || boost::regex_search(
                            this->m_internal_connection, subs, expr ) )
                    {
                        if ( !(E->unitsValue.size()) )
                        {
                            std::stringstream ss;
                            ss << this->valueToString(
                                this->m_last_value );

                            if ( m_nxgen.verbose() > 2 )
                            {
                                syslog( LOG_INFO,
                               "[%i] %s: %s %s in %s \"%s\" %s, %s \"%s\"",
                                    g_pid,
                                  "createSTCConfigGroupMatchingElements()",
                                    "Pattern Match for",
                                    this->m_device_pv_str.c_str(),
                                    "Group", G->name.c_str(),
                                    units_patt_str.c_str(),
                                    "Capturing Units Value as",
                                    ss.str().c_str() );
                                give_syslog_a_chance;
                            }

                            E->unitsValue = ss.str();

                            // Log Error if There Were More Than 1
                            // Values in This PV Time-Series Log!
                            if ( this->m_last_value_more
                                    && this->m_value_changed )
                            {
                                syslog( LOG_ERR,
                               "[%i] %s %s: %s %s in %s \"%s\" %s - %s %s",
                                    g_pid, "STC Error:",
                                  "createSTCConfigGroupMatchingElements()",
                                    "More Than 1 PV Value for",
                                    this->m_device_pv_str.c_str(),
                                    "Group", G->name.c_str(),
                                    units_patt_str.c_str(),
                                    "Check", m_log_path.c_str() );
                                give_syslog_a_chance;
                            }
                        }

                        else
                        {
                            syslog( LOG_ERR,
                                "[%i] %s %s: %s %s - %s %s...",
                                g_pid, "STC Error:",
                                "createSTCConfigGroupMatchingElements()",
                                "*** DUPLICATE Element Units Value for",
                                units_patt_str.c_str(),
                                "Ignoring",
                                this->m_device_pv_str.c_str() );
                            give_syslog_a_chance;
                        }
                    }
                }
            }
        }

        /// Collect/Append Elements to STC Config Command Line Parameters
        void appendSTCConfigCommandMatchingElements
        (
            struct CommandInfo *CMD,                    ///< Config Command
            std::vector<struct ElementInfo> &elements,  ///< Elements
            std::string label                           ///< Logging Label
        )
        {
            //syslog( LOG_INFO,
                //"[%i] %s: Checking Command \"%s\" for %s%s...",
                //g_pid, "appendSTCConfigCommandMatchingElements()",
                //CMD->name.c_str(), label.c_str(),
                //"Element Command Line Parameters" );
            //give_syslog_a_chance;

            for ( uint32_t e=0 ; e < elements.size() ; e++ )
            {
                struct ElementInfo *E = &(elements[e]);

                bool matched = false;

                // Check for Matching Elements to Link...
                for ( uint32_t p=0 ;
                        p < E->patterns.size() && !matched ; p++ )
                {
                    std::string &P = E->patterns[p];

                    std::string patt_str = label.c_str();
                    patt_str += "Element Pattern \"" + P + "\"";

                    //syslog( LOG_INFO, "[%i] %s: %s %s Match", g_pid,
                        //"appendSTCConfigCommandMatchingElements()",
                        //"Checking for", patt_str.c_str() );
                    //give_syslog_a_chance;

                    // Does PV Match This Command's Regex Pattern?
                    boost::regex expr( P );
                    boost::smatch subs;
                    if ( boost::regex_search(
                            this->m_internal_name, subs, expr )
                        || boost::regex_search(
                            this->m_internal_connection, subs, expr ) )
                    {
                        if ( m_nxgen.verbose() > 2 )
                        {
                            syslog( LOG_INFO,
                                "[%i] %s: %s %s in %s \"%s\" %s", g_pid,
                                "appendSTCConfigCommandMatchingElements()",
                                "Pattern Match for",
                                this->m_device_pv_str.c_str(),
                                "Command", CMD->name.c_str(),
                                patt_str.c_str() );
                            give_syslog_a_chance;
                        }

                        // Indexed Commands...
                        if ( CMD->hasIndex )
                        {
                            std::stringstream ss_index;
                            ss_index <<
                               "appendSTCConfigCommandMatchingElements():";

                            std::string indexedName;

                            uint32_t index = -1;

                            bool gotIndex = false;

                            for ( uint32_t i=0 ;
                                    i < E->indices.size() && !gotIndex ;
                                    i++ )
                            {
                                std::string &I = E->indices[i];

                                if ( sscanf( this->m_internal_name.c_str(),
                                        I.c_str(), &index ) == 1 )
                                {
                                    ss_index << " Index \"" << I << "\""
                                        << " Matched Internal Name \""
                                        << this->m_internal_name << "\""
                                        << " as " << index;

                                    gotIndex = true;
                                }

                                else if ( sscanf(
                                        this->m_internal_connection
                                            .c_str(),
                                        I.c_str(), &index ) == 1 )
                                {
                                    ss_index << " Index \"" << I << "\""
                                        << " Matched Internal"
                                        << " Connection \""
                                        << this->m_internal_connection
                                        << "\"" << " as " << index;

                                    gotIndex = true;
                                }
                            }

                            if ( gotIndex )
                            {
                                size_t start =
                                    CMD->name.find( GroupNameIndex );

                                if ( start == std::string::npos )
                                {
                                    syslog( LOG_ERR,
                                      "[%i] %s %s - %s \"%s\" for %s (%s)",
                                        g_pid, "STC Error:",
                                        ss_index.str().c_str(),
                                        "Index Not Found in Command Name",
                                        CMD->name.c_str(),
                                        this->m_device_pv_str.c_str(),
                                        patt_str.c_str() );
                                    give_syslog_a_chance;

                                    continue;
                                }

                                else
                                {
                                    std::stringstream ss;
                                    ss << index;

                                    indexedName = CMD->name;
                                    indexedName.replace( start,
                                        GroupNameIndex.length(),
                                        ss.str() );

                                    syslog( LOG_INFO,
                                        "[%i] %s - %s %s as %s \"%s\" %s",
                                        g_pid, ss_index.str().c_str(),
                                        "Indexed Command Name Found for",
                                        this->m_device_pv_str.c_str(),
                                        "Command", indexedName.c_str(),
                                        patt_str.c_str() );
                                    give_syslog_a_chance;

                                    // Capture Indexed Command Name
                                    // If Not Yet Included...
                                    if ( CMD->createdIndices.find(
                                                indexedName )
                                            == CMD->createdIndices.end() )
                                    {
                                        syslog( LOG_INFO,
                                            "[%i] %s: %s \"%s\"", g_pid,
                                "appendSTCConfigCommandMatchingElements()",
                                            "Capturing Indexed Group Name",
                                            indexedName.c_str() );
                                        give_syslog_a_chance;

                                        CMD->createdIndices.insert(
                                            indexedName );
                                    }
                                }
                            }

                            else
                            {
                                syslog( LOG_ERR,
                                    "[%i] %s %s: %s %s in %s \"%s\" %s",
                                    g_pid, "STC Error:",
                                "appendSTCConfigCommandMatchingElements()",
                                    "No Index Found for",
                                    this->m_device_pv_str.c_str(),
                                    "Command", CMD->name.c_str(),
                                    patt_str.c_str() );
                                give_syslog_a_chance;

                                continue;
                            }
                        }

                        // Capture *Just Last PV Value*
                        // as Command Line Parameter...
                        // - Whether E->linkLastValue or E->linkValue
                        // or neither (plain "element" reference)... ;-D

                        // Do We Have a "Last PV Value" to Use...?
                        if ( this->m_last_value_set )
                        {
                            std::string pv_value_path =
                                m_log_path + "/" + "value";

                            syslog( LOG_INFO,
                             "[%i] %s: %s %s %s for Command %s: %s=%s",
                                g_pid,
                                "appendSTCConfigCommandMatchingElements()",
                                "Append Command Line Parameter for",
                                "Last PV Value from",
                                pv_value_path.c_str(),
                                CMD->name.c_str(), E->name.c_str(),
                                this->valueToString(
                                    this->m_last_value ).c_str() );
                            give_syslog_a_chance;

                            std::string arg_value = this->valueToString(
                                    this->m_last_value );

                            if ( Utils::sanitizeString( arg_value,
                                false /* a_preserve_uri */,
                                true /* a_preserve_whitespace */ ) )
                            {
                                syslog( LOG_INFO,
                                "[%i] %s: %s %s %s %s %s: %s = %s -> %s",
                                    g_pid,
                                "appendSTCConfigCommandMatchingElements()",
                                    "Sanitizing PV",
                                    pv_value_path.c_str(),
                                    "Value for Command Line Parameter",
                                    "for Command", CMD->name.c_str(),
                                    E->name.c_str(),
                                    this->valueToString(
                                        this->m_last_value ).c_str(),
                                    arg_value.c_str() );
                                give_syslog_a_chance;
                            }

                            CMD->args += " " + E->name
                                + "=" + "\"" + arg_value + "\"";
                        }

                        else
                        {
                            syslog( LOG_ERR,
                                "[%i] %s %s: %s %s %s - %s - %s %s",
                                g_pid, "STC Error:",
                                "appendSTCConfigCommandMatchingElements()",
                                "*** NO LAST PV VALUE for",
                                CMD->name.c_str(), "Command",
                                "Nothing to Link to Element Path",
                                "Skipping", m_log_path.c_str() );
                            give_syslog_a_chance;

                            // Don't Call This A "Match" Yet,
                            // Let's Give Someone Else A Try...
                            continue;
                        }

                        matched = true;
                    }
                }
            }
        }

        /// Search STC Config for Associated Groups & Create...
        void createSTCConfigGroups(void)
        {
            //syslog( LOG_INFO, "[%i] %s: Checking %s for %s",
                //g_pid, "createSTCConfigGroups()",
                //this->m_device_pv_str.c_str(),
                //"Config Group Membership..." );
            //give_syslog_a_chance;

            // Do We Have a Valid Initialized NeXus Data File...?
            // (We shouldn't get called if not, so if we do, better
            // force it, lest we actually lose STC Config meta-data...!!)
            // Note: This is Just for Paranoia's Sake to Check Initialize
            // here, because we only get called by NxPVInfo::flushBuffers()
            // which _Also_ checks the NeXus Initialization... ;-D
            if ( !(m_nxgen.initialize( true,
                    "NxPVInfo::createSTCConfigGroups()" )) )
            {
                syslog( LOG_ERR, "[%i] %s %s: %s: %s - %s (%s %s)",
                    g_pid, "STC Error:",
                    "createSTCConfigGroups()",
                    "NxPVInfo::createSTCConfigGroups()",
                    "Failed to Force Initialize NeXus File",
                    "Losing STC Config Meta-Data!!",
                    this->m_device_str.c_str(),
                    this->m_pv_str.c_str() );
                give_syslog_a_chance;
                return;
            }

            // Check Each Config Group in Turn
            // for a Pattern Match on This PV...
            for ( uint32_t g=0 ; g < m_nxgen.m_config_groups.size() ; g++ )
            {
                struct GroupInfo *G = &(m_nxgen.m_config_groups[g]);

                // Check for Element Pattern Matches...
                createSTCConfigGroupMatchingElements(
                    G, G->elements, "" );

                // Skip Conditional Value Checks if No PV Values Set...
                if ( !(this->m_last_value_set) )
                    continue;

                // Check for Conditional Pattern Matches...
                for ( uint32_t c=0 ; c < G->conditions.size() ; c++ )
                {
                    struct ConditionInfo *C = &(G->conditions[c]);

                    // Skip If This Condition is Already Set/Satisfied...
                    if ( C->is_set )
                        continue;

                    // Check All Conditional Patterns (Until Set...)
                    for ( uint32_t p=0 ;
                            p < C->patterns.size() && !(C->is_set) ; p++ )
                    {
                        std::string &P = C->patterns[p];

                        //syslog( LOG_INFO,
                            //"[%i] %s: %s \"%s\" Pattern Match (%s)",
                            //g_pid, "createSTCConfigGroups()",
                            //"Checking for Group Conditional",
                            //C->name.c_str(), P.c_str() );
                        //give_syslog_a_chance;

                        // Does PV Match This Group's Conditional Pattern?
                        boost::regex expr( P );
                        boost::smatch subs;
                        if ( boost::regex_search(
                                this->m_internal_name, subs, expr )
                            || boost::regex_search(
                                this->m_internal_connection, subs, expr ) )
                        {
                            if ( m_nxgen.verbose() > 2 )
                            {
                                syslog( LOG_INFO,
                         "[%i] %s: %s %s in %s \"%s\" %s \"%s\" %s \"%s\"",
                                    g_pid, "createSTCConfigGroups()",
                                    "Pattern Match for",
                                    this->m_device_pv_str.c_str(),
                                    "Group", G->name.c_str(),
                                    "Conditional", C->name.c_str(),
                                    "Pattern", P.c_str());
                                give_syslog_a_chance;
                            }

                            // Check PV Value Against Condition Values...
                            if ( matchValues( this->m_last_value,
                                    C->values )
                                || notMatchValues( this->m_last_value,
                                    C->not_values )
                                || matchValueStrings(
                                    this->m_last_enum_string,
                                    C->value_strings )
                                || notMatchValueStrings(
                                    this->m_last_enum_string,
                                    C->not_value_strings ) )
                            {
                                // Condition Has Been Satisfied...! :-D
                                C->is_set = true;

                                std::string info = "Condition ";
                                info += "\"" + C->name + "\"";
                                info += " Set to True for";
                                syslog( LOG_INFO,
                                   "[%i] %s: %s %s in %s \"%s\" %s \"%s\"",
                                    g_pid, "createSTCConfigGroups()",
                                    info.c_str(),
                                    this->m_device_pv_str.c_str(),
                                    "Group", G->name.c_str(),
                                    "Conditional Pattern", P.c_str());
                                give_syslog_a_chance;
                            }
                        }
                    }
                }
            }

            // Check Each Config Command in Turn
            // for a Pattern Match on This PV...
            // (Don't Create Groups, Just Set Conditionals! ;-D)
            for ( uint32_t cmd=0 ;
                    cmd < m_nxgen.m_config_commands.size() ; cmd++ )
            {
                struct CommandInfo *CMD =
                    &(m_nxgen.m_config_commands[cmd]);

                appendSTCConfigCommandMatchingElements(
                    CMD, CMD->elements, "" );

                // Skip Command Conditional Values/Checks
                // If No PV Values Set...
                if ( !(this->m_last_value_set) )
                    continue;

                // Check for Conditional Pattern Matches...
                for ( uint32_t c=0 ; c < CMD->conditions.size() ; c++ )
                {
                    struct ConditionInfo *C = &(CMD->conditions[c]);

                    // For now, Just *Always* Append Conditional Elements
                    // as Command Line Parameters...
                    // - This isn't terrible, as if a Condition isn't set,
                    // then we may not execute the Command Script anyway...
                    // (TODO: "I'm pretty sure I can fix that..." ;-D)
                    appendSTCConfigCommandMatchingElements(
                        CMD, C->elements, "Condition" );

                    // Skip If This Condition is Already Set/Satisfied...
                    if ( C->is_set )
                        continue;

                    // Check All Conditional Patterns (Until Set...)
                    for ( uint32_t p=0 ;
                            p < C->patterns.size() && !(C->is_set) ; p++ )
                    {
                        std::string &P = C->patterns[p];

                        //syslog( LOG_INFO,
                            //"[%i] %s: %s \"%s\" Pattern Match (%s)",
                            //g_pid, "createSTCConfigGroups()",
                            //"Checking for Command Conditional",
                            //C->name.c_str(), P.c_str() );
                        //give_syslog_a_chance;

                        // Does PV Match This Group's Conditional Pattern?
                        boost::regex expr( P );
                        boost::smatch subs;
                        if ( boost::regex_search(
                                this->m_internal_name, subs, expr )
                            || boost::regex_search(
                                this->m_internal_connection, subs, expr ) )
                        {
                            if ( m_nxgen.verbose() > 2 )
                            {
                                syslog( LOG_INFO,
                         "[%i] %s: %s %s in %s \"%s\" %s \"%s\" %s \"%s\"",
                                    g_pid, "createSTCConfigGroups()",
                                    "Pattern Match for",
                                    this->m_device_pv_str.c_str(),
                                    "Command", CMD->name.c_str(),
                                    "Conditional", C->name.c_str(),
                                    "Pattern", P.c_str());
                                give_syslog_a_chance;
                            }

                            // Check PV Value Against Condition Values...
                            if ( matchValues( this->m_last_value,
                                    C->values )
                                || notMatchValues( this->m_last_value,
                                    C->not_values )
                                || matchValueStrings(
                                    this->m_last_enum_string,
                                    C->value_strings )
                                || notMatchValueStrings(
                                    this->m_last_enum_string,
                                    C->not_value_strings ) )
                            {
                                // Condition Has Been Satisfied...! :-D
                                C->is_set = true;

                                std::string info = "Condition ";
                                info += "\"" + C->name + "\"";
                                info += " Set to True for";
                                syslog( LOG_INFO,
                                   "[%i] %s: %s %s in %s \"%s\" %s \"%s\"",
                                    g_pid, "createSTCConfigGroups()",
                                    info.c_str(),
                                    this->m_device_pv_str.c_str(),
                                    "Command", CMD->name.c_str(),
                                    "Conditional Pattern", P.c_str());
                                give_syslog_a_chance;
                            }
                        }
                    }
                }
            }
        }

        /// Search STC Config for Conditional Groups & Create...
        void createSTCConfigConditionalGroups(void)
        {
            // Skip This If We're Not Writing to NeXus
            // or If We Don't Care About This PV (Ignored),
            // or We Don't Have Any STC Config Groups...! ;-D
            if ( !(m_nxgen.m_gen_nexus) || this->m_ignore
                    || !(m_nxgen.m_config_groups.size()) )
            {
                return;
            }

            // Do We Have a Valid Initialized NeXus Data File...?
            // (We shouldn't get called if not, so if we do, better
            // force it, lest we actually lose the final meta-data...!!)
            // Although if we just Forced Working Directory construction
            // in StreamParser::finalizeStreamProcessing(), then
            // Now's a Chance to Finally Create a NeXus Data File...! ;-D
            if ( !(m_nxgen.initialize( true,
                    "createSTCConfigConditionalGroups()" )) )
            {
                syslog( LOG_ERR, "[%i] %s %s: %s - %s (%s %s)",
                    g_pid, "STC Error:",
                    "createSTCConfigConditionalGroups()",
                    "Failed to Force Initialize NeXus File",
                    "Losing STC Conditional Config Groups!!",
                    this->m_device_str.c_str(),
                    this->m_pv_str.c_str() );
                give_syslog_a_chance;
                return;
            }

            try
            {
                // Search for Activated STC Config Conditional Groups

                //syslog( LOG_INFO, "[%i] %s: Checking %s for %s",
                    //g_pid, "createSTCConfigConditionalGroups()",
                    //this->m_device_pv_str.c_str(),
                    //"STC Config Conditional Group Membership..." );
                //give_syslog_a_chance;

                // Check Each Config Group in Turn for a
                // Conditional Pattern Match on This PV...
                for ( uint32_t g=0 ;
                        g < m_nxgen.m_config_groups.size() ; g++ )
                {
                    struct GroupInfo *G = &(m_nxgen.m_config_groups[g]);

                    // Skip Any Groups Without Conditions...
                    if ( !(G->conditions.size()) )
                        continue;

                    //syslog( LOG_INFO,
                       //"[%i] %s: Checking for Conditional Group %s...",
                        //g_pid, "createSTCConfigConditionalGroups()",
                        //G->name.c_str() );
                    //give_syslog_a_chance;

                    for ( uint32_t c=0 ; c < G->conditions.size() ; c++ )
                    {
                        struct ConditionInfo *C = &(G->conditions[c]);

                        if ( C->is_set )
                        {
                            std::string label = "Condition ";
                            label += "\"" + C->name + "\" ";
                            createSTCConfigGroupMatchingElements(
                                G, C->elements, label );
                        }
                    }
                }
            }
            catch( TraceException &e )
            {
                RETHROW_TRACE( e,
                    "createSTCConfigConditionalGroups (pv: "
                    << this->m_device_id << "." << this->m_pv_id
                    << ") failed." )
            }
        }

        NxGen          &m_nxgen;        ///< NxGen instance used for Nexus output
        std::string     m_internal_name;///< Internal Nexus name of variable
        std::string     m_internal_connection;///< Internal Nexus connection string of variable
        std::string     m_log_path;     ///< Nexus path to log entry for PV
        std::string     m_link_path;    ///< (Optional) Nexus path for (alias) link to PV log entry
        bool            m_has_link;     ///< Flag to Note Creation of "Target" String for Group Links
        uint64_t        m_cur_size;     ///< Running size of time and value datasets (same size for both)
        uint64_t        m_full_buffer_count;    ///< Rate-Limited Logging
        std::vector<std::string>
                        m_value_enum_strings;   ///< Vector of Enumerated Type Value Strings
        uint32_t        m_value_enum_strings_max_len;   ///< Max Length of Enumerated Type Value Strings
        uint32_t        m_value_enum_strings_not_found;   ///< Number of Enumerated Type Value Strings Not Found
        bool            m_finalized;    ///< Flag to Indicate PV Log has been Finalized
    };

public:

    NxGen(
        int a_fd_in,
        std::string &a_work_root,
        std::string &a_work_base,
        std::string &a_adara_out_file,
        std::string &a_nexus_out_file,
        std::string &a_config_file,
        bool a_strict,
        bool a_gather_stats,
        unsigned long a_chunk_size = 2048,   // in Dataset Elements! :-O
        unsigned short a_event_buf_chunk_count = 20,
        unsigned short a_ancillary_buf_chunk_count = 5,
        unsigned long a_cache_size = 10485760,
        unsigned short a_compression_level = 0,
        uint32_t a_verbose_level = 0 );
    ~NxGen();

    RateLimitedLogging::History m_RLLHistory_NxGen;

    void dumpProcessingStatistics(void);

    uint32_t executePrePostCommands(void);

protected:

    bool                initialize( bool a_force_init = false,
                            std::string caller = "" );
    void                finalize( const STC::RunMetrics &a_run_metrics,
                            const STC::RunInfo &a_run_info );
    STC::PVInfoBase*    makePVInfo( const std::string &a_device_name,
                            const std::string &a_name,
                            const std::string &a_connection,
                            STC::Identifier a_device_id,
                            STC::Identifier a_pv_id,
                            STC::PVType a_type,
                            std::vector<STC::PVEnumeratedType>
                                *a_enum_vector,
                            uint32_t a_enum_index,
                            const std::string &a_units,
                            bool a_ignore );
    STC::BankInfo*      makeBankInfo( uint16_t a_id, uint32_t a_state,
                            uint32_t a_buf_reserve,
                            uint32_t a_idx_buf_reserve );
    void                initializeNxBank( NxBankInfo *a_bi,
                            bool a_end_of_run );
    STC::MonitorInfo*   makeMonitorInfo( uint16_t a_id,
                            uint32_t a_buf_reserve,
                            uint32_t a_idx_buf_reserve,
                            STC::BeamMonitorConfig &a_config );
    void                initializeNxMonitor( NxMonitorInfo *a_mi );
    void                processBeamlineInfo(
                            const STC::BeamlineInfo &a_beamline_info,
                            bool a_force_init = false );
    void                processRunInfo( const STC::RunInfo &a_run_info,
                            const bool a_strict );
    void                processGeometry( const std::string &a_xml,
                            bool a_force_init = false );
    void                pulseBuffersReady( STC::PulseInfo &a_pulse_info );
    void                bankPidTOFBuffersReady( STC::BankInfo &a_bank );
    void                bankIndexBuffersReady( STC::BankInfo &a_bank,
                            bool use_default_chunk_size );
    void                bankPulseGap( STC::BankInfo &a_bank,
                            uint64_t a_count );
    void                bankFinalize( STC::BankInfo &a_bank );
    void                monitorTOFBuffersReady(
                            STC::MonitorInfo &a_monitor_info );
    void                monitorIndexBuffersReady(
                            STC::MonitorInfo &a_monitor_info,
                            bool use_default_chunk_size );
    void                monitorPulseGap( STC::MonitorInfo &a_monitor,
                            uint64_t a_count );
    void                monitorFinalize( STC::MonitorInfo &a_monitor );
    void                runComment( double a_time, uint64_t a_ts_nano,
                            const std::string &a_comment,
                            bool a_force_init = false );
    void                writeDeviceEnums( STC::Identifier a_devId,
                            std::vector<STC::PVEnumeratedType>
                                &a_enumVec );
    void                checkSTCConfigElementUnitsPaths(void);
    void                writeSTCConfigUnitsAttributes(
                            struct GroupInfo *G,
                            std::vector<struct ElementInfo> &elements );

private:

    template <typename TypeT>
    void                normalizeAnnotationTimestamps(
                            uint64_t a_start_time,
                            std::string a_label,
                            std::multimap<uint64_t,
                                std::pair<double, TypeT> >
                                    &a_annot_multimap,
                            bool &a_has_non_normalized );

    void                flushPauseData( uint64_t a_start_time );
    void                flushScanData( uint64_t a_start_time,
                            const STC::RunMetrics &a_run_metrics );
    void                flushCommentData( uint64_t a_start_time );

    NeXus::NXnumtype    toNxType( STC::PVType a_type ) const;

    void                makeGroup( const std::string &a_path,
                            const std::string &a_type );
    void                makeDataset( const std::string &a_path,
                            const std::string &a_name,
                            NeXus::NXnumtype a_type,
                            const std::string a_units = "",
                            unsigned long a_chunk_size = 0 );

    template <typename TypeT>
    void                writeMultidimDataset(
                            const std::string &a_path,
                            const std::string &a_name,
                            std::vector<TypeT> &a_data,
                            std::vector<hsize_t> &a_dims,
                            const std::string a_units = "" );

    void                parseSTCConfigFile(
                            const std::string &a_config_file );

    void                makeLink( const std::string &source_path,
                            const std::string &dest_name );
    void                makeGroupLink( const std::string &source_path,
                            const std::string &dest_name );

    void                writeString( const std::string &a_path,
                            const std::string &a_dataset,
                            const std::string &a_value );
    void                checkDataset( const std::string &a_path,
                            const std::string &a_dataset,
                            bool &a_exists );
    void                writeStringAttribute( const std::string &a_path,
                            const std::string &a_attrib,
                            const std::string &a_value );
    bool                checkStringAttribute( const std::string &a_path,
                            const std::string &a_attrib,
                            const std::string &a_value,
                            std::string &a_attr_value );

    /// Writes data values to a Nexus (HDF5) one-dimension dataset
    template<class T>
    void                writeSlab
                        (
                            const std::string &a_path, ///< [in] Nexus path to dataset
                            std::vector<T> &a_buffer,  ///< [in] Vector of data to write
                            uint64_t a_cur_size        ///< [in] Current dataset size (counts not bytes) [Actually "offset"...! Jeeem]
                        )
                        {
                            writeSlab( a_path,
                                a_buffer, a_buffer.size(), a_cur_size );
                        }
    template<class T>
    void                writeSlab
                        (
                            const std::string &a_path, ///< [in] Nexus path to dataset
                            std::vector<T> &a_buffer,  ///< [in] Vector of data to write
                            uint64_t a_buffer_size,    ///< [in] Size of Vector of data to write
                            uint64_t a_cur_size        ///< [in] Current dataset size (counts not bytes) [Actually "offset"...! Jeeem]
                        )
                        {
                            if ( a_buffer_size )
                            {
                                if ( m_h5nx.H5NXwrite_slab( a_path,
                                        a_buffer, a_buffer_size,
                                        a_cur_size ) != SUCCEED )
                                {
                                    THROW_TRACE( STC::ERR_OUTPUT_FAILURE,
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
                            const std::string &a_path, ///< [in] Nexus path to dataset
                            T &a_value,                ///< [in] Value to fill (append) dataset with
                            uint64_t a_count,          ///< [in] Number of values to append
                            uint64_t a_cur_size        ///< Current dataset size (counts not bytes)
                        )
                        {
                            if ( a_count )
                            {
                                std::vector<T> buf;
                                uint64_t cur_size = a_cur_size;
                                uint64_t count = 0;

                                // NOTE: Chunk Size is measured in
                                // *Dataset Elements*...! :-O
                                if ( a_count >= m_chunk_size )
                                {
                                    buf.resize( m_chunk_size, a_value );

                                    while ( count
                                            <= ( a_count - m_chunk_size ) )
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
    void                writeScalar( const std::string &a_path,
                            const std::string &a_name, T a_value,
                            const std::string &a_units );
    template<typename T>
    void                writeScalarAttribute( const std::string &a_path,
                            const std::string &a_attribute, T a_value );

    bool                m_gen_nexus;            ///< Controls whether Nexus file is generated or not
    bool                m_nexus_init;           ///< Has the Nexus file been Initialized yet or not?
    bool                m_nexus_beamline_init;  ///< Has the Nexus BeamlineInfo been Initialized yet or not?
    std::string         m_nexus_filename;       ///< Name of Nexus file

    std::string         m_config_file;          ///< Name of STC Config file
    std::vector<struct GroupInfo>
                        m_config_groups;        ///< Vector of STC Config Group Containers
    std::vector<struct CommandInfo>
                        m_config_commands;      ///< Vector of STC Config Pre-Post-Autoreduction Commands


    std::string         m_entry_path;           ///< Path to Nexus NXentry
    std::string         m_instrument_path;      ///< Path to Nexus NXinstrument
    std::string         m_daslogs_path;         ///< Path to Nexus DAS Logs
    std::string         m_daslogs_freq_path;    ///< Path to Nexus Frequency DAS Log
    std::string         m_daslogs_pchg_path;    ///< Path to Nexus Proton Charge DAS Log
    std::string         m_software_path;        ///< Path to Software Provenance Collection
    std::string         m_pid_name;             ///< Name of PID data in Nexus file
    std::string         m_tof_name;             ///< Name of TOF data in Nexus file
    std::string         m_index_name;           ///< Name of Event Index data in Nexus file
    std::string         m_pulse_time_name;      ///< Name of Pulse Time data in Nexus file
    std::string         m_data_name;            ///< Name of Histo data in Nexus file
    std::string         m_histo_pid_name;       ///< Name of Histo PixelId data in Nexus file
    std::string         m_histo_pid_name_raw;   ///< Name of Histo Physical (Raw) PixelId data in Nexus file
    std::string         m_tofbin_name;          ///< Name of Histo TOF Bin data in Nexus file
    unsigned long       m_chunk_size;           ///< HDF5 chunk size for Nexus file (in Dataset Elements!)
    H5nx                m_h5nx;                 ///< HDF5 library object
    uint64_t            m_pulse_info_cur_size;  ///< Current size of pulse info datasets (charge, time, frequency)
    std::vector<double> m_pulse_vetoes;         ///< Buffer of pulse veto times
    uint64_t            m_pulse_vetoes_cur_size;       ///< Current size of pulse veto dataset

    std::vector<double>         m_pulse_flags_time;     ///< Buffer of pulse flag times
    std::vector<uint32_t>       m_pulse_flags_value;    ///< Buffer of pulse flag values
    uint64_t                    m_pulse_flags_cur_size; ///< Current size of pulse flags dataset

    std::set<std::string>       m_pv_name_history;      /// Name/version history of PVs written to Nexus file
    std::string                 m_runComment;           /// Capture the "Singular" Run Comment for the Nexus file
    double                      m_runComment_time;      /// Time for the "Singular" Run Comment
    uint64_t                    m_runComment_ts_nano;   /// Nanoseconds for the "Singular" Run Comment
    bool                        m_nexus_run_comment_init; /// Has the Nexus Run Comment been Initialized yet or not?
    std::string                 m_geometryXml;          /// Capture the Geometry/IDF XML for the Nexus file
    bool                        m_nexus_geometry_init;  /// Has the Nexus Geometry/IDF been Initialized yet or not?
    float                       m_duration;             /// Save Total Run Duration (seconds)
    uint64_t                    m_total_counts;         /// Total Run Event Counts
    uint64_t                    m_total_uncounts;       /// Total Run Event Uncounts
    uint64_t                    m_total_non_counts;     /// Total Run Event Non-Counts (Monitor)
    struct timespec             m_stc_run_start_time;   /// STC Start of Processing Time
};

#endif // NXGEN_H

// vim: expandtab

