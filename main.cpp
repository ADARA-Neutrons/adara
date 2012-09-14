#include <cstdlib>
#include <stdio.h>
#include <string.h>
#include <fcntl.h>
#include <boost/lexical_cast.hpp>
#include <boost/filesystem.hpp>
#include "../SMS/ADARA.h"
#include "TransCompletePkt.h"
#include "NxGen.h"

using namespace std;

bool            interact = false;
bool            verbose = false;
bool            gather_stats = false;
bool            generate_adara = true;
bool            generate_nexus = true;
bool            move_files = false;
bool            strict = false;
std::string     work_path;
unsigned long   chunk_size = 2048;
unsigned short  evt_buf_size = 20;
unsigned short  anc_buf_size = 2;
unsigned long   cache_size = 1024;
unsigned short  compression_level = 0;
const char*     in_file = 0;

void
showUsage()
{
    cout << "Usage: sts [options]" << endl;
    cout << "Options:" << endl;
    cout << "  --help: show help" << endl;
    cout << "  +i: interactive mode" << endl;
    cout << "  +v: verbose output (interactive mode only)" << endl;
    cout << "  +s: enable strict protocol parsing" << endl;
    cout << "  +c: prep files for cataloging (forces strict parsing)" << endl;
    cout << "  +r: report stream statistics" << endl;
    cout << "  -n: suppress nexus output file generation" << endl;
    cout << "  -a: suppress adara output stream generation" << endl;
    cout << "  file=name: accept input from file instead of stdin" << endl;
    cout << "  work=path: working directory" << endl;
    cout << "  chunk=n: set hdf5 chunk size to 'n' bytes" << endl;
    cout << "  cache=n: set hdf5 cache to 'n' KB" << endl;
    cout << "  ebuf=n: set event buffers to 'n' chunks" << endl;
    cout << "  abuf=n: set ancillary buffers to 'n' chunks" << endl;
    cout << "  comp=n: set nexus compression leve to 'n' (0=off)" << endl;
}

void
parseCmdLine( int argc, char** argv )
{
    for ( int i = 1; i < argc; ++i )
    {
        if ( !strcmp(argv[i],"+v"))
            verbose = true;
        else if ( !strcmp(argv[i],"+i"))
            interact = true;
        else if ( !strcmp(argv[i],"+r"))
            gather_stats = true;
        else if ( !strcmp(argv[i],"+s"))
            strict = true;
        else if ( !strcmp(argv[i],"-a"))
            generate_adara = false;
        else if ( !strcmp(argv[i],"-n"))
            generate_nexus = false;
        else if ( !strcmp(argv[i],"+c"))
            move_files = true;
        else if ( !strncmp(argv[i],"chunk=",6))
            chunk_size = atol(&argv[i][6]);
        else if ( !strncmp(argv[i],"cache=",6))
            cache_size = atol(&argv[i][6]) * 1024;
        else if ( !strncmp(argv[i],"ebuf=",5))
            evt_buf_size = atoi(&argv[i][5]);
        else if ( !strncmp(argv[i],"abuf=",5))
            anc_buf_size = atoi(&argv[i][5]);
        else if ( !strncmp(argv[i],"file=",5))
            in_file = &argv[i][5];
        else if ( !strncmp(argv[i],"work=",5))
            work_path = &argv[i][5];
        else if ( !strncmp(argv[i],"comp=",5))
            compression_level = atoi(&argv[i][5]);
        else if ( !strcmp(argv[i],"--help"))
        {
            showUsage();
            exit(0);
        }
        else
        {
            cout << "Invalid option: " << argv[i] << endl;
            showUsage();
            throw std::runtime_error("Invalid command line argument(s)");
        }
    }
}

void
moveFile( const string &a_source, const string &a_dest_path, const string &a_dest_filename )
{
    boost::filesystem::create_directories( boost::filesystem::path( a_dest_path ));
    boost::filesystem::remove( boost::filesystem::path( a_dest_path+"/"+a_dest_filename ));
    boost::filesystem::rename( boost::filesystem::path( a_source ), boost::filesystem::path( a_dest_path+"/"+a_dest_filename ));
}

/*!
 * 
 */
int main(int argc, char** argv)
{
    int ifd = fileno(stdin);
    int ofd = fileno(stdout);
    STS::TranslationStatusCode sms_code = STS::TS_SUCCESS;
    string sms_reason;

    try
    {
        parseCmdLine( argc, argv );

        // If user has requested cataloging, force sane options
        if ( move_files )
        {
            strict = true;
            generate_adara = true;
            generate_nexus = true;
        }

        if ( gather_stats && !interact )
            gather_stats = false;

        //google::InitGoogleLogging(argv[0]);
        //google::SetLogDestination( google::INFO, "/tmp/sts.log." );

        string          nexus_outfile;
        string          adara_outfile;

        if ( work_path.size())
        {
            if ( work_path[work_path.size()-1] != '/' )
                work_path += "/";
        }

        string tempName = genTempName();

        if ( generate_adara )
            adara_outfile = work_path + tempName + ".adara";

        if ( generate_nexus )
            nexus_outfile = work_path + tempName + ".nxs";

        if ( interact && verbose )
        {
            cout << "ADARA Stream Parser" << endl;
            cout << "  nexus file   : " << nexus_outfile << endl;
            cout << "  adara file   : " << adara_outfile << endl;
            cout << "  strict       : " << strict << endl;
            cout << "  work path    : " << work_path << endl;
            cout << "  cat. prep    : " << (move_files?"true":"false") << endl;
            cout << "  chunk size   : " << chunk_size << " (bytes)" << endl;
            cout << "  cache size   : " << cache_size << " (bytes)" << endl;
            cout << "  evt buf size : " << evt_buf_size << " (chunks)" << endl;
            cout << "  anc buf size : " << anc_buf_size << " (chunks)" << endl;
            cout << "  comp lev     : " << compression_level <<  endl;
            cout << "  gather stats : " << (gather_stats?"yes":"no") << endl;
        }


        if ( in_file )
        {
            ifd = open( in_file, O_RDONLY );
            if ( ifd < 0 )
                throw std::runtime_error("Failed to open input file");
        }

        if ( ifd >= 0 )
        {
            NxGen    nxgen( ifd, adara_outfile, nexus_outfile, strict, gather_stats, chunk_size, evt_buf_size, anc_buf_size, cache_size, compression_level );

            nxgen.processStream();

            if ( move_files )
            {
                string cat_path = string("/") + nxgen.getFacilityName() + "/" + nxgen.getBeamShortName() + "/" + nxgen.getProposalID() + "/";
                string cat_name = nxgen.getBeamShortName() + "_" + boost::lexical_cast<string>(nxgen.getRunNumber());

                moveFile( adara_outfile, cat_path + "adara", cat_name + ".adara" );
                moveFile( nexus_outfile, cat_path + "nexus", cat_name + ".nxs.h5" );
            }

            if ( gather_stats )
                nxgen.printStats( cout );
        }
    }
    catch( STS::TranslationException &e )
    {
        sms_code = e.getStatusCode();
        sms_reason = e.what();
    }
    catch( boost::filesystem::filesystem_error &e )
    {
        sms_code = STS::TS_TRANSIENT_ERROR;
        sms_reason = e.what();
    }
    catch( exception &e )
    {
        sms_code = STS::TS_PERM_ERROR;
        sms_reason = e.what();
    }
    catch( ... )
    {
        sms_code = STS::TS_PERM_ERROR;
        sms_reason = "Unknown error";
    }

    if ( !interact )
    {
        STS::TransCompletePkt ack_pkt( sms_code, sms_reason );
        ::write( ofd, ack_pkt.getBuffer(), ack_pkt.getBufferLength());
    }
    else if ( sms_code != STS::TS_SUCCESS )
    {
        cout << "Error: " << sms_reason << endl;
    }

    return sms_code != STS::TS_SUCCESS;
}

