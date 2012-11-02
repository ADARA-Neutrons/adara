#include <cstdlib>
#include <stdio.h>
#include <string.h>
#include <fcntl.h>
#include <boost/lexical_cast.hpp>
#include <boost/filesystem.hpp>
#include <boost/program_options.hpp>
#include "ADARA.h"
#include "TransCompletePkt.h"
#include "TraceException.h"
#include "NxGen.h"

using namespace std;

#define STS_VERSION "0.1.3"

void
moveFile( const string &a_source, const string &a_dest_path, const string &a_dest_filename )
{
    try
    {
        boost::filesystem::create_directories( boost::filesystem::path( a_dest_path ));
        boost::filesystem::remove( boost::filesystem::path( a_dest_path+"/"+a_dest_filename ));
        boost::filesystem::rename( boost::filesystem::path( a_source ), boost::filesystem::path( a_dest_path+"/"+a_dest_filename ));
    }
    catch( boost::filesystem::filesystem_error &e )
    {
        THROW_TRACE( STS::ERR_OUTPUT_FAILURE, "Move of " << a_source << " to " << a_dest_path << "/" << a_dest_filename << " failed. {" << e.what() << "}" )
    }
}


int main(int argc, char** argv)
{
    int                         infd = 0;
    int                         outfd = 1;
    STS::TranslationStatusCode  sms_code = STS::TS_SUCCESS;
    string                      sms_reason;
    bool                        interact;
    string                      work_path;
    string                      base_path;
    unsigned long               chunk_size;
    unsigned short              evt_buf_size;
    unsigned short              anc_buf_size;
    unsigned long               cache_size;
    unsigned short              compression_level;


    try
    {
        bool strict;
        bool move;
        bool verbose;
        bool gather_stats;
        bool suppress_adara;
        bool suppress_nexus;

        namespace po = boost::program_options;
        po::options_description options( "sts program options" );
        options.add_options()
                ("help,h", "show help")
                ("version", "show version number")
                ("interactive,i", po::bool_switch( &interact )->default_value( false ), "interactive mode")
                ("verbose,v", po::bool_switch( &verbose )->default_value( false ), "verbose output (interactive mode only)")
                ("stict,s", po::bool_switch( &strict )->default_value( false ), "enable strict protocol parsing")
                ("move,m", po::bool_switch( &move )->default_value( false ), "move output nexus file to cataloging location (forces strict parsing)")
                ("report,r", po::bool_switch( &gather_stats )->default_value( false ), "report stream statistics")
                ("no-nexus,n", po::bool_switch( &suppress_nexus )->default_value( false ), "suppress nexus output file generation")
                ("no-adara,a", po::bool_switch( &suppress_adara )->default_value( false ), "suppress adara output stream generation")
                ("file,f",po::value<string>(),"read input from file instead of stdin")
                ("work-path,w",po::value<string>( &work_path ),"set path to working directory")
                ("base-path,b",po::value<string>( &base_path ),"set base cataloging path (none by defualt)")
                ("compression-level,c", po::value<unsigned short>( &compression_level )->default_value( 0 ), "set nexus compression level (0=off,9=max)")
                ("chunk-size", po::value<unsigned long>( &chunk_size )->default_value( 2048 ),"set hdf5 chunk size (in bytes)")
                ("cache-size", po::value<unsigned long>( &cache_size )->default_value( 1024 ),"set hdf5 cache size (in KB)")
                ("event-buf-size", po::value<unsigned short>( &evt_buf_size )->default_value( 20 ),"set event buffers to (in chunks)")
                ("anc-buf-size", po::value<unsigned short>( &anc_buf_size )->default_value( 2 ),"set ancillary buffers (in chunks)")
                ;

        po::variables_map opt_map;
        po::store( po::parse_command_line(argc,argv,options), opt_map );
        po::notify( opt_map );

        if ( opt_map.count( "help" ))
        {
            cout << options << endl;
            return STS::TS_TRANSIENT_ERROR;
        }
        else if ( opt_map.count( "version" ))
        {
            cout << STS_VERSION << endl;
            return STS::TS_TRANSIENT_ERROR;
        }

        // If user has requested cataloging, force sane options
        if ( move )
        {
            strict = true;
            suppress_adara = false;
            suppress_nexus = false;
        }

        if ( gather_stats && !interact )
            gather_stats = false;

        if ( work_path.size() )
        {
            if ( work_path[work_path.size()-1] != '/' )
                work_path += "/";
        }

        string tempName = genTempName();
        string nexus_outfile;
        string adara_outfile;

        if ( !suppress_adara )
            adara_outfile = work_path + tempName + ".adara";

        if ( !suppress_nexus )
            nexus_outfile = work_path + tempName + ".nxs";

        if ( interact && verbose )
        {
            cout << "sts information" << endl;
            cout << "  version      : " << STS_VERSION << endl;
            cout << "  nexus file   : " << nexus_outfile << endl;
            cout << "  adara file   : " << adara_outfile << endl;
            cout << "  strict       : " << ( move ? "yes" : "no" ) << endl;
            cout << "  work path    : " << work_path << endl;
            cout << "  base path    : " << base_path << endl;
            cout << "  move nexus   : " << ( move ? "yes" : "no" ) << endl;
            cout << "  chunk size   : " << chunk_size << " (bytes)" << endl;
            cout << "  cache size   : " << cache_size << " (bytes)" << endl;
            cout << "  evt buf size : " << evt_buf_size << " (chunks)" << endl;
            cout << "  anc buf size : " << anc_buf_size << " (chunks)" << endl;
            cout << "  comp lev     : " << compression_level <<  endl;
            cout << "  gather stats : " << ( gather_stats ? "yes" : "no" ) << endl;
        }

        if ( opt_map.count( "file" ))
        {
            infd = open( opt_map["file"].as<string>().c_str(), O_RDONLY );
            if ( infd < 0 )
                throw std::runtime_error("Failed to open input file");
        }

        if ( infd >= 0 )
        {
            if ( !interact )
            {
                // In non-interactive mode, must hack around chatty HDF5 library: remap stdout and stderr to /dev/null
                outfd = dup( 1 );
                int nullfd = open( "/dev/null", O_RDWR );
                dup2( nullfd, 1 );
                dup2( nullfd, 2 );
            }

            NxGen    nxgen( infd, adara_outfile, nexus_outfile, strict, gather_stats, chunk_size, evt_buf_size,
                            anc_buf_size, cache_size, compression_level );

            nxgen.processStream();

            if ( move )
            {
                if ( base_path.empty() )
                    base_path = "/";
                else if ( base_path[base_path.length()-1] != '/')
                    base_path += "/";

                string cat_path = base_path + nxgen.getFacilityName() + "/" + nxgen.getBeamShortName() + "/" + nxgen.getProposalID() + "/";
                string cat_name = nxgen.getBeamShortName() + "_" + boost::lexical_cast<string>(nxgen.getRunNumber());

                moveFile( adara_outfile, cat_path + "adara", cat_name + ".adara" );
                moveFile( nexus_outfile, cat_path + "nexus", cat_name + ".nxs.h5" );
            }

            if ( gather_stats )
                nxgen.printStats( cout );
        }
    }
    catch( TraceException &e )
    {
        // Generally, output errors are transient, others are permanent
        if ( e.getErrorCode() == STS::ERR_OUTPUT_FAILURE )
            sms_code = STS::TS_TRANSIENT_ERROR;
        else
            sms_code = STS::TS_PERM_ERROR;
        sms_reason = e.toString( true, true );
    }
    catch( exception &e )
    {
        // Unexpected exception
        sms_code = STS::TS_PERM_ERROR;
        sms_reason = e.what();
    }
    catch( ... )
    {
        // Really unexpected exception
        sms_code = STS::TS_PERM_ERROR;
        sms_reason = "Unhandled exception";
    }

    if ( !interact )
    {
        STS::TransCompletePkt ack_pkt( sms_code, sms_reason );
        ::write( outfd, ack_pkt.getBuffer(), ack_pkt.getBufferLength());

        // TODO If code is not success, write reason to a log file somewhere?
    }
    else if ( sms_code != STS::TS_SUCCESS )
    {
        cout << sms_reason << endl;
    }

    return sms_code != STS::TS_SUCCESS;
}

