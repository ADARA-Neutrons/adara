//
// SNS ADARA SYSTEM - Retrieve Unmapped Events Utility
// 
// This repository contains the software for the next-generation Data
// Acquisition System (DAS) at the Spallation Neutron Source (SNS) at
// Oak Ridge National Laboratory (ORNL) -- "ADARA".
// 
// Copyright (c) 2019, UT-Battelle LLC
// All rights reserved.
// 
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions
// are met:
// 
// 1. Redistributions of source code must retain the above copyright
// notice, this list of conditions and the following disclaimer.
// 
// 2. Redistributions in binary form must reproduce the above copyright
// notice, this list of conditions and the following disclaimer in the
// documentation and/or other materials provided with the distribution.
// 
// 3. Neither the name of the copyright holder nor the names of its
// contributors may be used to endorse or promote products derived from
// this software without specific prior written permission.
// 
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS
// IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO,
// THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
// PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR
// CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
// EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
// PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
// PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
// LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
// NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
// SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
// 

/**
 * \page This utility program retrieves captured neutron events
 * from formerly "unmapped" detector pixel ids, and combines them
 * with the existing valid detector bank events.
 */

#include <iostream>
#include <boost/filesystem.hpp>
#include <boost/program_options.hpp>
#include <stdint.h>
#include "h5nx.hpp"

// Global pid
pid_t g_pid = 0;

bool verbose;
bool dump;

/**
 * @brief copyFile - Attempts to copy a file to the specified path
 * @param a_source - Full path to source file
 * @param a_dest_path - Destination path (no filename)
 * @param a_dest_filename - Destination filename
 *
 * This method attempts to copy the specified file to the specified path
 * and filename. If the operation fails, an exception is thrown.
 */
void
copyFile( const std::string &a_source,
        const std::string &a_dest_path, const std::string &a_dest_filename )
{
    try
    {
        boost::filesystem::create_directories(
            boost::filesystem::path( a_dest_path ) );
        boost::filesystem::copy_file(
            boost::filesystem::path( a_source ),
            boost::filesystem::path( a_dest_path + "/" + a_dest_filename )
        );
    }
    catch( boost::filesystem::filesystem_error &e )
    {
        std::cerr << "Copy of " << a_source << " to "
                << a_dest_path << "/" << a_dest_filename
                << " failed. {" << e.what() << "}" << std::endl;
    }
}

/**
 * @brief readVector - read a numerical vector from the specified HDF5 path
 * @param a_h5nx - handle to H5NX class instance
 * @param a_vector - vector to initialize and fill with data values
 * @param a_data_path - internal HDF5 file path to dataset
 * @param a_label - semantic label for this dataset
 *
 * This method attempts to copy the specified file to the specified path
 * and filename. If the operation fails, an exception is thrown.
 */
template <typename NumT>
int
readVector( H5nx &a_h5nx, std::vector<NumT> &a_vector,
        std::string a_data_path, std::string a_label )
{
    std::vector<hsize_t> dim_vec( H5S_MAX_RANK );
    hsize_t vec_size;
    int rank;

    if ( a_h5nx.H5NXget_dataset_dims( a_data_path,
            rank, dim_vec ) != SUCCEED )
    {
        std::cerr << std::endl
            << "Error Getting " << a_label << " Dataset Dimensions!"
            << std::endl << "Bailing..." << std::endl;
        return( -110 );
    }
    else if ( verbose )
    {
        std::cout << std::endl
            << "Got " << a_label << " Dataset Dimensions"
            << std::endl << "   rank=" << rank << ", dim[0]=" << dim_vec[0]
            << std::endl << std::flush;
    }

    vec_size = a_h5nx.H5NXget_vector_size( rank, dim_vec );

    if ( verbose )
    {
        std::cout << std::endl
            << a_label << " Vector has " << vec_size << " Elements."
            << std::endl << std::flush;
    }

    a_vector.assign( vec_size, 0 );

    if ( a_h5nx.H5NXread_slab( a_data_path,
            a_vector, vec_size, 0 ) != SUCCEED )
    {
        std::cerr << std::endl
            << "Error Reading " << a_label << " Dataset!"
            << std::endl << "Bailing..." << std::endl;
        return( -120 );
    }
    else if ( dump )
    {
        std::cout << std::endl
            << a_label << " Vector:" << std::endl;
        for ( hsize_t i=0 ; i < vec_size ; i++ )
        {
            std::cout << i << ": "
                << a_vector[i]
                << " (0x" << std::hex << a_vector[i] << std::dec << ")"
                << std::endl;
        }
        std::cout << std::flush;
    }

    return( 0 );
}

template
int readVector( H5nx &a_h5nx, std::vector<uint32_t> &a_vector,
        std::string a_data_path, std::string a_label );

template
int readVector( H5nx &a_h5nx, std::vector<uint64_t> &a_vector,
        std::string a_data_path, std::string a_label );

/**
 * @brief main - Entry point of RetrieveUnmapped process
 * @param argc - Number of CLI arguments
 * @param argv - Array of CLI command/parameter strings
 * @return 0 on success, 1 on error
 */
int main( int argc, char** argv )
{
    unsigned long   chunk_size; // in Dataset Elements! :-O
    unsigned long   cache_size;
    unsigned short  compression_level;
    std::string     nexus_source;
    std::string     nexus_dest;
    std::string     dest_bank;
    int             cc;

    // Setup global syslog info
    g_pid = getpid();

    try
    {
        namespace po = boost::program_options;
        po::options_description options(
            "retrieveUnmapped program options" );
        options.add_options()
                ("help,h", "show help")
                ("verbose,v", po::bool_switch( &verbose ), "verbose output")
                ("dump,d", po::bool_switch( &dump ), "dump array values")
                ("nexus_source",po::value<std::string>( &nexus_source ),"NeXus input file to read from")
                ("nexus_dest",po::value<std::string>( &nexus_dest ),"NeXus output file to write to")
                ("dest_bank",po::value<std::string>( &dest_bank ),"destination detector bank for unmapped events")
                ("compression-level,c", po::value<unsigned short>( &compression_level )->default_value( 0 ), "set nexus compression level (0=off,9=max)")
                ("chunk-size", po::value<unsigned long>( &chunk_size )->default_value( 49152 ),"set hdf5 chunk size (in Dataset Elements!)")
                ("cache-size", po::value<unsigned long>( &cache_size )->default_value( 1024 ),"set hdf5 cache size (in KB)")
                ;

        po::variables_map opt_map;
        po::store( po::parse_command_line(argc,argv,options), opt_map );
        po::notify( opt_map );

        if ( opt_map.count( "help" ) )
        {
            std::cout << std::endl << options << std::endl;
            return( -1 );
        }

        if ( !opt_map.count( "dest_bank" ) )
        {
            std::cout << std::endl
                << "Error: No Destination Detector Bank Specified...!"
                << std::endl;
            std::cout << std::endl << options << std::endl;
            return( -2 );
        }

        if ( verbose )
        {
            std::cout << std::endl
                << "RetrieveUnmapped Information:"
                << std::endl << std::endl;
            std::cout << "   Source NeXus File   : "
                 << nexus_source << std::endl;
            std::cout << "   Dest NeXus File     : "
                 << nexus_dest << std::endl;
            std::cout << "   Dest Detector Bank  : "
                 << dest_bank << std::endl;
            std::cout << "   Chunk Size          : "
                 << chunk_size << " (Dataset Elements!)" << std::endl;
            std::cout << "   Cache Size          : "
                 << cache_size << " (bytes)" << std::endl;
            std::cout << "   Compress Level      : "
                 << compression_level <<  std::endl;
        }

        // Try to "copy" NeXus file to be modified...
        if ( !nexus_source.empty() )
        {
            if ( nexus_dest.empty() )
            {
                nexus_dest = "Output.nxs.h5";

                if ( verbose )
                {
                    std::cout << std::endl
                        << "No Output NeXus File Destination Specified."
                        << std::endl << std::endl
                        << "Using Default NeXus File:"
                        << std::endl << std::endl
                        << "   [" << nexus_dest << "]" << std::endl;
                }
            }

            copyFile( nexus_source, ".", nexus_dest );

            if ( verbose )
            {
                std::cout << std::endl
                    << "Successfully Copied Source NeXus File:" << std::endl
                    << std::endl
                    << "   [" << nexus_source << "]" << std::endl
                    << std::endl
                    << "to Destination Copy:" << std::endl
                    << std::endl
                    << "   [" << nexus_dest << "]"
                    << std::endl;
            }
        }
    }
    catch( std::exception &e )
    {
        std::cerr << "Exception in RetrieveUnmapped: " << e.what()
            << std::endl;
        return( -10 );
    }
    catch( ... )
    {
        std::cerr << "Unknown Exception in RetrieveUnmapped!" << std::endl;
        return( -11 );
    }

    // Create H5NX Instance

    H5nx m_h5nx = H5nx(compression_level);

    m_h5nx.H5NXset_cache_size( cache_size );

    // Open NeXus Source Data File
    if ( m_h5nx.H5NXopen_file( nexus_source ) != SUCCEED )
    {
        std::cerr << std::endl
            << "Error Opening NeXus Source Data File! Bailing..."
            << std::endl;
        return( -100 );
    }
    else if ( verbose )
    {
        std::cout << std::endl
            << "Opened NeXus Source Data File for Processing:"
            << std::endl << std::endl
            << "   [" << nexus_source << "]"
            << std::endl << std::flush;
    }

    // Read Unmapped Datasets from Source NeXus File

    std::vector<uint32_t> unmapped_event_id;
    std::vector<uint32_t> unmapped_event_time_offset;

    std::vector<uint64_t> unmapped_event_index;

    uint64_t unmapped_total_counts;

    // Read Unmapped Event ID Dataset
    if ( (cc = readVector( m_h5nx, unmapped_event_id,
            "/entry/instrument/bank_unmapped/event_id",
            "Unmapped Event ID" )) != 0 )
    {
        std::cerr << std::endl
            << "Error Reading Unmapped Event ID Dataset!"
            << " cc=" << cc << std::endl
            << "Bailing..." << std::endl;
        return( cc );
    }

    // Strip Off High 0x8 Error/Unmapped Byte from Pixel IDs...
    if ( verbose )
    {
        std::cout << std::endl
            << "Stripping Off High 0x8 Error/Unmapped Byte from Pixel IDs."
            << std::endl;
    }
    if ( dump )
    {
        std::cout << std::endl
            << "Stripped Unmapped Event ID Vector:" << std::endl;
    }
    for ( hsize_t i=0 ; i < unmapped_event_id.size() ; i++ )
    {
        unmapped_event_id[i] &= 0x0FFFFFFF;
        if ( dump )
        {
            std::cout << i << ": "
                << unmapped_event_id[i]
                << " (0x" << std::hex << unmapped_event_id[i]
                    << std::dec << ")"
                << std::endl;
        }
    }

    // Read Unmapped Event Time Offset Dataset
    if ( (cc = readVector( m_h5nx, unmapped_event_time_offset,
            "/entry/instrument/bank_unmapped/event_time_offset",
            "Unmapped Event Time Offset" )) != 0 )
    {
        std::cerr << std::endl
            << "Error Reading Unmapped Event Time Offset Dataset!"
            << " cc=" << cc << std::endl
            << "Bailing..." << std::endl;
        return( cc );
    }

    // Read Unmapped Event Index Dataset
    if ( (cc = readVector( m_h5nx, unmapped_event_index,
            "/entry/instrument/bank_unmapped/event_index",
            "Unmapped Event Index" )) != 0 )
    {
        std::cerr << std::endl
            << "Error Reading Unmapped Event Index Dataset!"
            << " cc=" << cc << std::endl
            << "Bailing..." << std::endl;
        return( cc );
    }

    // Read Unmapped Bank Total Counts
    if ( m_h5nx.H5NXread_dataset_scalar(
            "/entry/instrument/bank_unmapped/total_counts",
            unmapped_total_counts ) != SUCCEED )
    {
        std::cerr << std::endl
            << "Error Reading Unmapped Bank Total Counts! Bailing..."
            << std::endl;
        return( -200 );
    }
    else if ( verbose )
    {
        std::cout << std::endl
            << "Total Unmapped Event Counts = " << unmapped_total_counts
            << std::endl << std::flush;
    }

    // Read Destination Bank Datasets from Source NeXus File

    std::string dest_path = "/entry/instrument/" + dest_bank;

    std::vector<uint32_t> dest_event_id;
    std::vector<uint32_t> dest_event_time_offset;

    std::vector<uint64_t> dest_event_index;

    uint64_t dest_total_counts;

    // Read Destination Bank Event ID Dataset
    if ( (cc = readVector( m_h5nx, dest_event_id,
            dest_path + "/event_id",
            "Destination Bank Event ID" )) != 0 )
    {
        std::cerr << std::endl
            << "Error Reading Destination Bank Event ID Dataset!"
            << " cc=" << cc << std::endl
            << "Bailing..." << std::endl;
        return( cc );
    }

    // Read Destination Bank Event Time Offset Dataset
    if ( (cc = readVector( m_h5nx, dest_event_time_offset,
            dest_path + "/event_time_offset",
            "Destination Bank Event Time Offset" )) != 0 )
    {
        std::cerr << std::endl
            << "Error Reading Destination Bank Event Time Offset Dataset!"
            << " cc=" << cc << std::endl
            << "Bailing..." << std::endl;
        return( cc );
    }

    // Read Destination Bank Event Index Dataset
    if ( (cc = readVector( m_h5nx, dest_event_index,
            dest_path + "/event_index",
            "Destination Bank Event Index" )) != 0 )
    {
        std::cerr << std::endl
            << "Error Reading Destination Bank Event Index Dataset!"
            << " cc=" << cc << std::endl
            << "Bailing..." << std::endl;
        return( cc );
    }

    // Read Destination Bank Total Counts
    if ( m_h5nx.H5NXread_dataset_scalar( dest_path + "/total_counts",
            dest_total_counts ) != SUCCEED )
    {
        std::cerr << std::endl
            << "Error Reading Destination Bank Total Counts! Bailing..."
            << std::endl;
        return( -300 );
    }
    else if ( verbose )
    {
        std::cout << std::endl
            << "Total Destination Bank Event Counts = " << dest_total_counts
            << std::endl << std::flush;
    }

    // Close NeXus Source Data File
    if ( m_h5nx.H5NXclose_file() != SUCCEED )
    {
        std::cerr << std::endl
            << "Error Closing NeXus Source Data File! Bailing..."
            << std::endl;
        return( -400 );
    }
    else if ( verbose )
    {
        std::cout << std::endl
            << "Closed NeXus Source Data File."
            << std::endl << std::flush;
    }

    // Sanity Check the Event Indices...
    if ( unmapped_event_index.size() != dest_event_index.size() )
    {
        std::cerr << std::endl
            << "Error: Event Index (Time) Vector Size Mismatch!"
            << std::endl;
        std::cerr << std::endl
            << "Unmapped Event Index has "
            << unmapped_event_index.size() << " Elements,"
            << std::endl << "   And Destination Bank Event Index has "
            << dest_event_index.size() << " Elements!"
            << " Bailing..."
            << std::endl;
        return( -500 );
    }
    else
    {
        std::cerr << std::endl
            << "Event Index (Time) Vector Sizes Match."
            << std::endl;
        std::cerr << std::endl
            << "Unmapped Event Index has "
            << unmapped_event_index.size() << " Elements,"
            << std::endl << "   And Destination Bank Event Index has "
            << dest_event_index.size() << " Elements."
            << std::endl;
    }

    // Now Merge Unmapped Events into Destination Detector Bank...

    std::vector<uint32_t> merged_event_id;
    std::vector<uint32_t> merged_event_time_offset;

    std::vector<uint64_t> merged_event_index;

    uint64_t merged_total_counts;

    uint64_t total_uncounted_counts;
    uint64_t events_have_no_bank;
    uint64_t total_counts;

    uint64_t unmapped_last_event_index = 0;
    uint64_t dest_last_event_index = 0;

    uint64_t next_merged_event_index = 0;

    uint64_t unmapped_index = 0;
    uint64_t dest_index = 0;
    uint64_t time_index = 0;

    if ( dump ) std::cout << std::endl;

    while ( time_index < dest_event_index.size() )
    {
        // Initialize Merged Event Index
        merged_event_index.push_back( next_merged_event_index );

        // Check for Unmapped Events to Add to Merged Set...
        if ( unmapped_event_index[ time_index ]
                != unmapped_last_event_index )
        {
            if ( dump )
            {
                std::cout << "Add "
                    << unmapped_event_index[ time_index ]
                        - unmapped_last_event_index
                    << " Unmapped Events..." << std::endl;
            }

            // Add Unmapped Event(s) to Merged Set...
            for ( uint64_t i = unmapped_last_event_index ;
                    i < unmapped_event_index[ time_index ] ; i++ )
            {
                if ( dump )
                {
                    std::cout << "   idx=" << unmapped_index
                        << " event_id="
                        << unmapped_event_id[ unmapped_index ]
                        << " time_offset="
                        << unmapped_event_time_offset[ unmapped_index ]
                        << std::endl;
                }

                merged_event_id.push_back(
                    unmapped_event_id[ unmapped_index ] );
                merged_event_time_offset.push_back(
                    unmapped_event_time_offset[ unmapped_index ] );
                unmapped_index++;
            }

            // Update Merged Event Index...
            merged_event_index[ time_index ] +=
                unmapped_event_index[ time_index ]
                    - unmapped_last_event_index;

            // Save Last Unmapped Event Index
            unmapped_last_event_index = unmapped_event_index[ time_index ];
        }

        // Check for Original Destination Bank Events to Add to Merged Set
        if ( dest_event_index[ time_index ]
                != dest_last_event_index )
        {
            if ( dump )
            {
                std::cout << "Add "
                    << dest_event_index[ time_index ]
                        - dest_last_event_index
                    << " Original Destination Events..." << std::endl;
            }

            // Add Original Destination Bank Event(s) to Merged Set...
            for ( uint64_t i = dest_last_event_index ;
                    i < dest_event_index[ time_index ] ; i++ )
            {
                if ( dump )
                {
                    std::cout << "   idx=" << dest_index
                        << " event_id=" << dest_event_id[ dest_index ]
                        << " time_offset="
                        << dest_event_time_offset[ dest_index ]
                        << std::endl;
                }

                merged_event_id.push_back(
                    dest_event_id[ dest_index ] );
                merged_event_time_offset.push_back(
                    dest_event_time_offset[ dest_index ] );
                dest_index++;
            }

            // Update Merged Event Index...
            merged_event_index[ time_index ] +=
                dest_event_index[ time_index ]
                    - dest_last_event_index;

            // Save Last Unmapped Event Index
            dest_last_event_index = dest_event_index[ time_index ];
        }

        // Advance to Next Time Index...

        if ( dump )
        {
            std::cout << "merged_event_index[ " << time_index << " ] = "
                << merged_event_index[ time_index ] << std::endl;
        }

        next_merged_event_index = merged_event_index[ time_index ];

        time_index++;
    }

    // Verify Merged Total Event Counts...

    merged_total_counts = unmapped_total_counts + dest_total_counts;

    if ( merged_total_counts != merged_event_id.size() )
    {
        std::cerr << std::endl
            << "Error: Merged Total Counts Mismatch!"
            << std::endl;
        std::cerr << std::endl
            << "Calculated Merged Total Counts of "
            << merged_total_counts
            << std::endl << "   Does Not Equal Merged Event ID Vector"
            << " Size of " << merged_event_id.size() << "!"
            << " Bailing..."
            << std::endl;
        return( -510 );
    }
    else
    {
        std::cerr << std::endl
            << "Merged Total Counts Match."
            << std::endl;
        std::cerr << std::endl
            << "Calculated Merged Total Counts of "
            << merged_total_counts
            << std::endl << "   Equals Merged Event ID Vector Size of "
            << merged_event_id.size() << "."
            << std::endl;
    }

    // Open Destination NeXus File
    if ( m_h5nx.H5NXopen_file( nexus_dest ) != SUCCEED )
    {
        std::cerr << std::endl
            << "Error Opening NeXus Destination Data File! Bailing..."
            << std::endl;
        return( -600 );
    }
    else if ( verbose )
    {
        std::cout << std::endl
            << "Opened NeXus Destination Data File for Processing:"
            << std::endl << std::endl
            << "   [" << nexus_dest << "]"
            << std::endl << std::flush;
    }

    // Overwrite/Extend Destination Bank Event ID Dataset
    if ( m_h5nx.H5NXwrite_slab( dest_path + "/event_id",
            merged_event_id,
            merged_event_id.size(), 0 ) != SUCCEED )
    {
        std::cerr << std::endl
            << "Error Overwriting/Extending"
            << " Bank Event ID Dataset!"
            << std::endl << "Bailing..." << std::endl;
        return( -610 );
    }
    else if ( verbose )
    {
        std::cout << std::endl
            << "Successfully Overwrote/Extended"
            << " Bank Event ID Dataset."
            << std::endl << std::flush;
    }

    // Overwrite/Extend Destination Bank Event Time Offset Dataset
    if ( m_h5nx.H5NXwrite_slab( dest_path + "/event_time_offset",
            merged_event_time_offset,
            merged_event_time_offset.size(), 0 ) != SUCCEED )
    {
        std::cerr << std::endl
            << "Error Overwriting/Extending"
            << " Bank Event Time Offset Dataset!"
            << std::endl << "Bailing..." << std::endl;
        return( -620 );
    }
    else if ( verbose )
    {
        std::cout << std::endl
            << "Successfully Overwrote/Extended"
            << " Bank Event Time Offset Dataset."
            << std::endl << std::flush;
    }

    // Overwrite/Extend Destination Bank Event Index Dataset
    if ( m_h5nx.H5NXwrite_slab( dest_path + "/event_index",
            merged_event_index,
            merged_event_index.size(), 0 ) != SUCCEED )
    {
        std::cerr << std::endl
            << "Error Overwriting/Extending"
            << " Bank Event Index Dataset!"
            << std::endl << "Bailing..." << std::endl;
        return( -630 );
    }
    else if ( verbose )
    {
        std::cout << std::endl
            << "Successfully Overwrote/Extended"
            << " Bank Event Index Dataset."
            << std::endl << std::flush;
    }

    // Move Original Unmapped Instrument Bank Data Out of the Way...
    if ( m_h5nx.H5NXmove_link( "/entry/instrument/bank_unmapped",
            "/entry/instrument/bank_retrieved" ) != SUCCEED )
    {
        std::cerr << std::endl
            << "Error Renaming/Moving Unmapped Instrument Bank Group!"
            << std::endl << "   from \"/entry/instrument/bank_unmapped\""
            << std::endl << "   to \"/entry/instrument/bank_retrieved\""
            << std::endl << "Bailing..." << std::endl;
        return( -640 );
    }
    else if ( verbose )
    {
        std::cerr << std::endl
            << "Successfully Renamed/Moved"
            << " Unmapped Instrument Bank Group" << std::endl
            << "   from \"/entry/instrument/bank_unmapped\"" << std::endl
            << "   to \"/entry/instrument/bank_retrieved\"" << std::endl;
    }

    // Move Original Unmapped Bank Data Out of the Way...
    if ( m_h5nx.H5NXmove_link( "/entry/bank_unmapped_events",
            "/entry/bank_retrieved_events" ) != SUCCEED )
    {
        std::cerr << std::endl
            << "Error Renaming/Moving Unmapped Bank Group!"
            << std::endl << "   from \"/entry/bank_unmapped_events\""
            << std::endl << "   to \"/entry/bank_retrieved_events\""
            << std::endl << "Bailing..." << std::endl;
        return( -640 );
    }
    else if ( verbose )
    {
        std::cerr << std::endl
            << "Successfully Renamed/Moved"
            << " Unmapped Bank Group" << std::endl
            << "   from \"/entry/bank_unmapped_events\"" << std::endl
            << "   to \"/entry/bank_retrieved_events\"" << std::endl;
    }

    // Overwrite Destination Bank Total Counts
    if ( m_h5nx.H5NXwrite_dataset_scalar( dest_path + "/total_counts",
            merged_total_counts ) != SUCCEED )
    {
        std::cerr << std::endl
            << "Error Overwriting Bank Total Counts Dataset!"
            << std::endl << "   merged_total_counts=" << merged_total_counts
            << std::endl << "Bailing..." << std::endl;
        return( -650 );
    }
    else if ( verbose )
    {
        std::cout << std::endl
            << "Successfully Overwrote Bank Total Counts Dataset."
            << std::endl << "   merged_total_counts=" << merged_total_counts
            << std::endl << std::flush;
    }

    // Overwrite Destination File Overall Total Counts...

    // Read Destination File Overall Total Counts
    if ( m_h5nx.H5NXread_dataset_scalar( "/entry/total_counts",
            total_counts ) != SUCCEED )
    {
        std::cerr << std::endl
            << "Error Reading Destination File Overall Total Counts!"
            << " Bailing..."
            << std::endl;
        return( -660 );
    }
    else if ( verbose )
    {
        std::cout << std::endl
            << "Original Destination File Overall Total Counts = "
            << total_counts
            << std::endl << std::flush;
    }

    total_counts += unmapped_total_counts;

    // Overwrite Destination File Overall Total Counts
    if ( m_h5nx.H5NXwrite_dataset_scalar( "/entry/total_counts",
            total_counts ) != SUCCEED )
    {
        std::cerr << std::endl
            << "Error Overwriting File Overall Total Counts Dataset!"
            << std::endl << "   total_counts=" << total_counts
            << std::endl << "Bailing..." << std::endl;
        return( -670 );
    }
    else if ( verbose )
    {
        std::cout << std::endl
            << "Successfully Overwrote File Overall Total Counts Dataset."
            << std::endl << "   total_counts=" << total_counts
            << std::endl << std::flush;
    }

    // Overwrite Destination File Total Uncounted Counts...

    // Read Destination File Total Uncounted Counts
    if ( m_h5nx.H5NXread_dataset_scalar( "/entry/total_uncounted_counts",
            total_uncounted_counts ) != SUCCEED )
    {
        std::cerr << std::endl
            << "Error Reading Destination File Total Uncounted Counts!"
            << " Bailing..."
            << std::endl;
        return( -680 );
    }
    else if ( verbose )
    {
        std::cout << std::endl
            << "Original Destination File Total Uncounted Counts = "
            << total_uncounted_counts
            << std::endl << std::flush;
    }

    total_uncounted_counts -= unmapped_total_counts;

    // Overwrite Destination File Total Uncounted Counts
    if ( m_h5nx.H5NXwrite_dataset_scalar( "/entry/total_uncounted_counts",
            total_uncounted_counts ) != SUCCEED )
    {
        std::cerr << std::endl
            << "Error Overwriting File Total Uncounted Counts Dataset!"
            << std::endl << "   total_uncounted_counts="
            << total_uncounted_counts
            << std::endl << "Bailing..." << std::endl;
        return( -690 );
    }
    else if ( verbose )
    {
        std::cout << std::endl
            << "Successfully Overwrote File Total Uncounted Counts Dataset."
            << std::endl << "   total_uncounted_counts="
            << total_uncounted_counts
            << std::endl << std::flush;
    }

    // Overwrite Destination File "Events Have No Bank" Count...

    // Read Destination File "Events Have No Bank" Counts
    if ( m_h5nx.H5NXread_attribute_scalar(
            "/entry/total_uncounted_counts",
            "events_have_no_bank", events_have_no_bank ) != SUCCEED )
    {
        std::cerr << std::endl
            << "Error Reading Destination File"
            << " \"Events Have No Bank\" Counts!" << std::endl
            << "Skipping...!"
            << std::endl;
    }
    else
    {
        if ( verbose )
        {
            std::cout << std::endl
                << "Original Destination File"
                << " \"Events Have No Bank\" Counts = "
                << events_have_no_bank
                << std::endl << std::flush;
        }

        // Deduct Formerly Unmapped Counts...
        if ( events_have_no_bank < unmapped_total_counts )
        {
            std::cout << std::endl
                << "WARNING: \"Events Have No Bank\" Counts is"
                << " LESS Than the Unmapped Total Counts Deducted!!"
                << std::endl
                << "   events_have_no_bank=" << events_have_no_bank
                << " unmapped_total_counts=" << unmapped_total_counts
                << std::endl
                << "   Setting events_have_no_bank=0" << std::endl;
            events_have_no_bank = 0;
        }
        else
        {
            events_have_no_bank -= unmapped_total_counts;
        }

        // Overwrite Destination File "Events Have No Bank" Counts
        if ( m_h5nx.H5NXwrite_attribute_scalar(
                "/entry/total_uncounted_counts",
                "events_have_no_bank", events_have_no_bank ) != SUCCEED )
        {
            std::cerr << std::endl
                << "Error Overwriting File"
                << " \"Events Have No Bank\" Counts Dataset!"
                << std::endl
                << "   events_have_no_bank=" << events_have_no_bank
                << std::endl << "Bailing..." << std::endl;
            return( -700 );
        }
        else if ( verbose )
        {
            std::cout << std::endl
                << "Successfully Overwrote File"
                << " \"Events Have No Bank\" Counts Dataset."
                << std::endl
                << "   events_have_no_bank=" << events_have_no_bank
                << std::endl << std::flush;
        }
    }

    // Close NeXus Destination Data File
    if ( m_h5nx.H5NXclose_file() != SUCCEED )
    {
        std::cerr << std::endl
            << "Error Closing NeXus Destination Data File! Bailing..."
            << std::endl;
        return( -800 );
    }
    else if ( verbose )
    {
        std::cout << std::endl
            << "Closed NeXus Destination Data File."
            << std::endl << std::flush;
    }

    // Done

    std::cout << std::endl << std::flush;

    return( 0 );
}

// vim: expandtab

