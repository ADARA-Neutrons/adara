
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
    bool            verbose;
    bool            dump;

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
                ("compression-level,c", po::value<unsigned short>( &compression_level )->default_value( 0 ), "set nexus compression level (0=off,9=max)")
                ("chunk-size", po::value<unsigned long>( &chunk_size )->default_value( 49152 ),"set hdf5 chunk size (in Dataset Elements!)")
                ("cache-size", po::value<unsigned long>( &cache_size )->default_value( 1024 ),"set hdf5 cache size (in KB)")
                ;

        po::variables_map opt_map;
        po::store( po::parse_command_line(argc,argv,options), opt_map );
        po::notify( opt_map );

        if ( opt_map.count( "help" ))
        {
            std::cout << options << std::endl;
            return( -1 );
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

    // Read Datasets from Source NeXus File

    std::vector<uint32_t> unmapped_event_id;
    std::vector<uint32_t> unmapped_event_time_offset;

    std::vector<uint64_t> unmapped_event_index;

    uint64_t unmapped_total_counts;

    std::vector<hsize_t> dim_vec;
    hsize_t vec_size;
    int rank;

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

    // Read Unmapped Event ID Dataset

    dim_vec.reserve( H5S_MAX_RANK );

    if ( m_h5nx.H5NXget_dataset_dims(
            "/entry/instrument/bank_unmapped/event_id",
            rank, dim_vec ) != SUCCEED )
    {
        std::cerr << std::endl
            << "Error Getting Unmapped Event ID Dataset Dimensions!"
            << " Bailing..." << std::endl;
        return( -110 );
    }
    else if ( verbose )
    {
        std::cout << std::endl
            << "Got Unmapped Event ID Dataset Dimensions"
            << " rank=" << rank << ", dim[0]=" << dim_vec[0]
            << std::endl << std::flush;
    }

    vec_size = m_h5nx.H5NXget_vector_size( rank, dim_vec );

    if ( verbose )
    {
        std::cout << std::endl
            << "Unmapped Event ID Vector has " << vec_size << " Elements."
            << std::endl << std::flush;
    }

    unmapped_event_id.reserve( vec_size );

    if ( m_h5nx.H5NXread_slab(
            "/entry/instrument/bank_unmapped/event_id",
            unmapped_event_id, vec_size, 0 ) != SUCCEED )
    {
        std::cerr << std::endl
            << "Error Reading Event ID Dataset! Bailing..."
            << std::endl;
        return( -120 );
    }
    else if ( dump )
    {
        std::cout << std::endl
            << "Unmapped Event ID Vector:" << std::endl;
        for ( hsize_t i=0 ; i < vec_size ; i++ )
        {
            std::cout << i << ": "
                << unmapped_event_id[i]
                << " (0x" << std::hex << unmapped_event_id[i]
                    << std::dec << ")"
                << std::endl;
        }
        std::cout << std::flush;
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
    for ( hsize_t i=0 ; i < vec_size ; i++ )
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

    if ( m_h5nx.H5NXget_dataset_dims(
            "/entry/instrument/bank_unmapped/event_time_offset",
            rank, dim_vec ) != SUCCEED )
    {
        std::cerr << std::endl
            << "Error Getting Unmapped Event Time Offset Dimensions!"
            << " Bailing..." << std::endl;
        return( -210 );
    }
    else if ( verbose )
    {
        std::cout << std::endl
            << "Got Unmapped Event Time Offset Dataset Dimensions"
            << " rank=" << rank << ", dim[0]=" << dim_vec[0]
            << std::endl << std::flush;
    }

    vec_size = m_h5nx.H5NXget_vector_size( rank, dim_vec );

    if ( verbose )
    {
        std::cout << std::endl
            << "Unmapped Event Time Offset Vector has "
            << vec_size << " Elements."
            << std::endl << std::flush;
    }

    unmapped_event_time_offset.reserve( vec_size );

    if ( m_h5nx.H5NXread_slab(
            "/entry/instrument/bank_unmapped/event_time_offset",
            unmapped_event_time_offset, vec_size, 0 ) != SUCCEED )
    {
        std::cerr << std::endl
            << "Error Reading Event Time Offset Dataset! Bailing..."
            << std::endl;
        return( -220 );
    }
    else if ( dump )
    {
        std::cout << std::endl
            << "Unmapped Event Time Offset Vector:" << std::endl;
        for ( hsize_t i=0 ; i < vec_size ; i++ )
        {
            std::cout << i << ": "
                << unmapped_event_time_offset[i]
                << " (0x" << std::hex << unmapped_event_time_offset[i]
                    << std::dec << ")"
                << std::endl;
        }
        std::cout << std::flush;
    }

    // Read Unmapped Event Index Dataset

    if ( m_h5nx.H5NXget_dataset_dims(
            "/entry/instrument/bank_unmapped/event_index",
            rank, dim_vec ) != SUCCEED )
    {
        std::cerr << std::endl
            << "Error Getting Unmapped Event Index Dimensions!"
            << " Bailing..." << std::endl;
        return( -310 );
    }
    else if ( verbose )
    {
        std::cout << std::endl
            << "Got Unmapped Event Index Dataset Dimensions"
            << " rank=" << rank << ", dim[0]=" << dim_vec[0]
            << std::endl << std::flush;
    }

    vec_size = m_h5nx.H5NXget_vector_size( rank, dim_vec );

    if ( verbose )
    {
        std::cout << std::endl
            << "Unmapped Event Index Vector has "
            << vec_size << " Elements."
            << std::endl << std::flush;
    }

    unmapped_event_index.reserve( vec_size );

    if ( m_h5nx.H5NXread_slab(
            "/entry/instrument/bank_unmapped/event_index",
            unmapped_event_index, vec_size, 0 ) != SUCCEED )
    {
        std::cerr << std::endl
            << "Error Reading Event Index Dataset! Bailing..."
            << std::endl;
        return( -320 );
    }
    else if ( dump )
    {
        std::cout << std::endl
            << "Unmapped Event Index Vector:" << std::endl;
        for ( hsize_t i=0 ; i < vec_size ; i++ )
        {
            std::cout << i << ": "
                << unmapped_event_index[i]
                << " (0x" << std::hex << unmapped_event_index[i]
                    << std::dec << ")"
                << std::endl;
        }
        std::cout << std::flush;
    }

    // Read Unmapped Bank Total Counts

    if ( m_h5nx.H5NXread_dataset_scalar(
            "/entry/instrument/bank_unmapped/total_counts",
            unmapped_total_counts ) != SUCCEED )
    {
        std::cerr << std::endl
            << "Error Reading Unmapped Bank Total Counts! Bailing..."
            << std::endl;
        return( -400 );
    }
    else if ( verbose )
    {
        std::cout << std::endl
            << "Total Unmapped Event Count = " << unmapped_total_counts
            << std::endl << std::flush;
    }

    // Close NeXus Source Data File

    if ( m_h5nx.H5NXclose_file() != SUCCEED )
    {
        std::cerr << std::endl
            << "Error Closing NeXus Source Data File! Bailing..."
            << std::endl;
        return( -700 );
    }
    else if ( verbose )
    {
        std::cout << std::endl
            << "Closed NeXus Source Data File."
            << std::endl << std::flush;
    }

    // Open Destination NeXus File


    // Done

    std::cout << std::endl << std::flush;

    return( 0 );
}

// vim: expandtab

