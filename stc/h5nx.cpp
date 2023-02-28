/*
 *NeXus writer interface for HD5 and NeXus
 */

#include "h5nx.hpp"
#include <stdint.h>
#include <iostream>
#include <assert.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include "ADARAUtils.h"

#include <syslog.h>

extern pid_t g_pid;

#define H5NX_SANITY_CHECK

using namespace std;


//////////////////////////////////////////////////////////////////////////
//H5nx
//////////////////////////////////////////////////////////////////////////

H5nx::H5nx( unsigned short a_compression_level )
    : m_compression_level(a_compression_level)
{
    m_fid = -1;
    m_fapl = -1;
    m_cache_size = 0;
    modify_chunk_cache = false;
}

//////////////////////////////////////////////////////////////////////////
//H5NXopen_file
//opens an existing HDF5 file
//////////////////////////////////////////////////////////////////////////
int H5nx::H5NXopen_file( const std::string &file_name )
{
    if ( modify_chunk_cache )
    {
        assert ( m_cache_size );

        int     mdc_nelmts;
        size_t  rdcc_nelmts;
        size_t  rdcc_nbytes;
        double  rdcc_w0;

        if (( m_fapl = H5Pcreate( H5P_FILE_ACCESS )) < 0 )
        {
            syslog( LOG_ERR, "[%i] %s in %s(): Error in %s() %s=%ld",
                g_pid, "STC Error", "H5nx::H5NXopen_file", "H5Pcreate",
                "m_fapl", (long) m_fapl );
            give_syslog_a_chance;
            H5NXdumperr(
                "H5nx::H5NXopen_file(): H5Pcreate() Create File Access");
            return FAIL;
        }

        //same as NeXus
        if ( H5Pset_fclose_degree( m_fapl, H5F_CLOSE_STRONG) < 0 )
        {
            syslog( LOG_ERR, "[%i] %s in %s(): Error in %s() %s=%ld",
                g_pid, "STC Error", "H5nx::H5NXopen_file",
                "H5Pset_fclose_degree", "m_fapl", (long) m_fapl );
            give_syslog_a_chance;
            H5NXdumperr("H5nx::H5NXopen_file(): H5Pset_fclose_degree()");
            return FAIL;
        }

        if ( H5Pget_cache(m_fapl, &mdc_nelmts,
                &rdcc_nelmts, &rdcc_nbytes, &rdcc_w0) < 0 )
        {
            syslog( LOG_ERR, "[%i] %s in %s(): Error in %s() %s=%ld",
                g_pid, "STC Error", "H5nx::H5NXopen_file",
                "H5Pget_cache", "m_fapl", (long) m_fapl );
            give_syslog_a_chance;
            H5NXdumperr("H5nx::H5NXopen_file(): H5Pget_cache()");
            return FAIL;
        }

        rdcc_nbytes = m_cache_size;
        rdcc_nelmts = rdcc_nelmts * 100;

        if ( H5Pset_cache(m_fapl, mdc_nelmts,
                rdcc_nelmts, rdcc_nbytes, rdcc_w0) < 0 )
        {
            syslog( LOG_ERR,
                "[%i] %s in %s(): Error in %s() %s=%ld %s=%ld %s=%ld",
                g_pid, "STC Error", "H5nx::H5NXopen_file",
                "H5Pset_cache", "m_fapl", (long) m_fapl,
                "rdcc_nbytes", (long) rdcc_nbytes,
                "rdcc_nelmts", (long) rdcc_nelmts );
            give_syslog_a_chance;
            H5NXdumperr("H5nx::H5NXopen_file(): H5Pset_cache()");
            return FAIL;
        }

        if (( m_fid = H5Fopen( file_name.c_str(), H5F_ACC_RDWR,
                m_fapl )) < 0 )
        {
            syslog( LOG_ERR, "[%i] %s in %s(): Error in %s() %s=%s",
                g_pid, "STC Error", "H5nx::H5NXopen_file",
                "H5Fopen", "file_name", file_name.c_str() );
            give_syslog_a_chance;
            H5NXdumperr(
                "H5nx::H5NXopen_file(): H5Fopen() Open File "
                + file_name);
            return FAIL;
        }

#if defined (H5NX_SANITY_CHECK)
        if ( H5Pget_cache(m_fapl, &mdc_nelmts,
                &rdcc_nelmts, &rdcc_nbytes, &rdcc_w0) < 0 )
        {
            syslog( LOG_ERR, "[%i] %s in %s(): Error in %s() %s=%ld %s",
                g_pid, "STC Error", "H5nx::H5NXopen_file",
                "H5Pget_cache", "m_fapl", (long) m_fapl, "Sanity Check" );
            give_syslog_a_chance;
            H5NXdumperr(
                "H5nx::H5NXopen_file(): H5Pget_cache() Sanity Check");
            return FAIL;
        }
        assert( rdcc_nbytes == m_cache_size );
#endif
    }

    else
    {
        if (( m_fid = H5Fopen( file_name.c_str(), H5F_ACC_RDWR,
                H5P_DEFAULT )) < 0 )
        {
            syslog( LOG_ERR, "[%i] %s in %s(): Error in %s() %s=%s",
                g_pid, "STC Error", "H5nx::H5NXopen_file",
                "H5Fopen", "file_name", file_name.c_str() );
            give_syslog_a_chance;
            H5NXdumperr(
                "H5nx::H5NXopen_file(): H5Fopen() Open File "
                + file_name);
            return FAIL;
        }
    }

    return SUCCEED;
}

//////////////////////////////////////////////////////////////////////////
//H5NXclose_file
//////////////////////////////////////////////////////////////////////////
int H5nx::H5NXclose_file()
{
    if ( modify_chunk_cache )
    {
        assert ( m_fapl != - 1 );
        if ( H5Pclose(m_fapl) < 0 )
        {
            syslog( LOG_ERR, "[%i] %s in %s(): Error in %s() m_fapl=%ld",
                g_pid, "STC Error", "H5nx::H5NXclose_file", "H5Pclose",
                (long) m_fapl );
            give_syslog_a_chance;
            H5NXdumperr(
                "H5nx::H5NXclose_file(): H5Pclose() Close File Access");
            return FAIL;
        }
        m_fapl = -1;
    }

    if ( H5Fclose(m_fid) < 0 )
    {
        syslog( LOG_ERR, "[%i] %s in %s(): Error in %s() m_fid=%ld",
            g_pid, "STC Error", "H5nx::H5NXclose_file", "H5Fclose",
            (long) m_fid );
        give_syslog_a_chance;
        H5NXdumperr(
            "H5nx::H5NXclose_file(): H5Fclose() Close File");
        return FAIL;
    }
    m_fid = -1;

    return SUCCEED;
}

//////////////////////////////////////////////////////////////////////////
//H5NXcreate_file
//create the HDF5 file
//////////////////////////////////////////////////////////////////////////
void H5nx::H5NXset_cache_size( size_t size )
{
    m_cache_size = size;
    modify_chunk_cache = true;
}

//////////////////////////////////////////////////////////////////////////
//H5NXcreate_file
//create the HDF5 file
//////////////////////////////////////////////////////////////////////////
int H5nx::H5NXcreate_file( const std::string &file_name )
{
    if ( modify_chunk_cache )
    {
        assert ( m_cache_size );

        int     mdc_nelmts;
        size_t  rdcc_nelmts;
        size_t  rdcc_nbytes;
        double  rdcc_w0;

        if (( m_fapl = H5Pcreate( H5P_FILE_ACCESS )) < 0 )
        {
            syslog( LOG_ERR, "[%i] %s in %s(): Error in %s() %s=%ld",
                g_pid, "STC Error", "H5nx::H5NXcreate_file", "H5Pcreate",
                "m_fapl", (long) m_fapl );
            give_syslog_a_chance;
            H5NXdumperr(
                "H5nx::H5NXcreate_file(): H5Pcreate() Create File Access");
            return FAIL;
        }

        //same as NeXus
        if ( H5Pset_fclose_degree( m_fapl, H5F_CLOSE_STRONG) < 0 )
        {
            syslog( LOG_ERR, "[%i] %s in %s(): Error in %s() %s=%ld",
                g_pid, "STC Error", "H5nx::H5NXcreate_file",
                "H5Pset_fclose_degree", "m_fapl", (long) m_fapl );
            give_syslog_a_chance;
            H5NXdumperr("H5nx::H5NXcreate_file(): H5Pset_fclose_degree()");
            return FAIL;
        }

        if ( H5Pget_cache(m_fapl, &mdc_nelmts,
                &rdcc_nelmts, &rdcc_nbytes, &rdcc_w0) < 0 )
        {
            syslog( LOG_ERR, "[%i] %s in %s(): Error in %s() %s=%ld",
                g_pid, "STC Error", "H5nx::H5NXcreate_file",
                "H5Pget_cache", "m_fapl", (long) m_fapl );
            give_syslog_a_chance;
            H5NXdumperr("H5nx::H5NXcreate_file(): H5Pget_cache()");
            return FAIL;
        }

        rdcc_nbytes = m_cache_size;
        rdcc_nelmts = rdcc_nelmts * 100;

        if ( H5Pset_cache(m_fapl, mdc_nelmts,
                rdcc_nelmts, rdcc_nbytes, rdcc_w0) < 0 )
        {
            syslog( LOG_ERR,
                "[%i] %s in %s(): Error in %s() %s=%ld %s=%ld %s=%ld",
                g_pid, "STC Error", "H5nx::H5NXcreate_file",
                "H5Pset_cache", "m_fapl", (long) m_fapl,
                "rdcc_nbytes", (long) rdcc_nbytes,
                "rdcc_nelmts", (long) rdcc_nelmts );
            give_syslog_a_chance;
            H5NXdumperr("H5nx::H5NXcreate_file(): H5Pset_cache()");
            return FAIL;
        }

        if (( m_fid = H5Fcreate( file_name.c_str(), H5F_ACC_TRUNC,
                H5P_DEFAULT, m_fapl )) < 0 )
        {
            syslog( LOG_ERR, "[%i] %s in %s(): Error in %s() %s=%s",
                g_pid, "STC Error", "H5nx::H5NXcreate_file",
                "H5Fcreate", "file_name", file_name.c_str() );
            give_syslog_a_chance;
            H5NXdumperr(
                "H5nx::H5NXcreate_file(): H5Fcreate() Create File "
                + file_name);
            return FAIL;
        }

#if defined (H5NX_SANITY_CHECK)
        if ( H5Pget_cache(m_fapl, &mdc_nelmts,
                &rdcc_nelmts, &rdcc_nbytes, &rdcc_w0) < 0 )
        {
            syslog( LOG_ERR, "[%i] %s in %s(): Error in %s() %s=%ld %s",
                g_pid, "STC Error", "H5nx::H5NXcreate_file",
                "H5Pget_cache", "m_fapl", (long) m_fapl, "Sanity Check" );
            give_syslog_a_chance;
            H5NXdumperr(
                "H5nx::H5NXcreate_file(): H5Pget_cache() Sanity Check");
            return FAIL;
        }
        assert( rdcc_nbytes == m_cache_size );
#endif
    }

    else
    {
        if (( m_fid = H5Fcreate( file_name.c_str(), H5F_ACC_TRUNC,
                H5P_DEFAULT, H5P_DEFAULT )) < 0 )
        {
            syslog( LOG_ERR, "[%i] %s in %s(): Error in %s() %s=%s",
                g_pid, "STC Error", "H5nx::H5NXcreate_file",
                "H5Fcreate", "file_name", file_name.c_str() );
            give_syslog_a_chance;
            H5NXdumperr(
                "H5nx::H5NXcreate_file(): H5Fcreate() Create File "
                + file_name);
            return FAIL;
        }
    }

    if ( this->write_root_metadata( file_name.c_str() ) < 0 )
    {
        syslog( LOG_ERR, "[%i] %s in %s(): Error in %s() %s=%s",
            g_pid, "STC Error", "H5nx::H5NXcreate_file",
            "write_root_metadata", "file_name", file_name.c_str() );
        give_syslog_a_chance;
        return FAIL;
    }

    return SUCCEED;
}



//////////////////////////////////////////////////////////////////////////
//H5NXmake_group
//////////////////////////////////////////////////////////////////////////
int H5nx::H5NXmake_group( const std::string &group_name,
    const std::string &class_name )
{
    hid_t     gid;
    hid_t     aid;
    hid_t     tid;
    hid_t     sid;

    if ( (gid = H5Gcreate2( this->m_fid, group_name.c_str(),
            H5P_DEFAULT, H5P_DEFAULT, H5P_DEFAULT)) < 0 )
    {
        syslog( LOG_ERR, "[%i] %s in %s(): Error in %s() %s=%s",
            g_pid, "STC Error", "H5nx::H5NXmake_group",
            "H5Gcreate2", "group_name", group_name.c_str() );
        give_syslog_a_chance;
        H5NXdumperr("H5nx::H5NXmake_group(): H5Gcreate2() Create Group "
            + group_name);
        return FAIL;
    }

    if ( (sid = H5Screate(H5S_SCALAR)) < 0 )
    {
        syslog( LOG_ERR, "[%i] %s in %s(): Error in %s() Create Dataspace",
            g_pid, "STC Error", "H5nx::H5NXmake_group", "H5Screate" );
        give_syslog_a_chance;
        H5NXdumperr(
            "H5nx::H5NXmake_group(): H5Screate() Create Dataspace");
        return FAIL;
    }

    if ( (tid = H5Tcopy(H5T_C_S1)) < 0 )
    {
        syslog( LOG_ERR, "[%i] %s in %s(): Error in %s() Write Type",
            g_pid, "STC Error", "H5nx::H5NXmake_group", "H5Tcopy" );
        give_syslog_a_chance;
        H5NXdumperr("H5nx::H5NXmake_group(): H5Tcopy() Write Type");
        return FAIL;
    }

    if ( H5Tset_size( tid, strlen(class_name.c_str()) ) < 0 )
    {
        syslog( LOG_ERR, "[%i] %s in %s(): Error in %s() %s=%s %s=%ld",
            g_pid, "STC Error", "H5nx::H5NXmake_group", "H5Tset_size",
            "class_name", class_name.c_str(),
            "strlen", (long) strlen( class_name.c_str() ) );
        give_syslog_a_chance;
        H5NXdumperr(
            "H5nx::H5NXmake_group(): H5Tset_size() Class Name Size "
            + class_name);
        return FAIL;
    }

    if ( (aid= H5Acreate2(gid, "NX_class", tid, sid,
            H5P_DEFAULT, H5P_DEFAULT)) < 0 )
    {
        syslog( LOG_ERR, "[%i] %s in %s(): Error in %s() %s",
            g_pid, "STC Error", "H5nx::H5NXmake_group", "H5Acreate2",
            "Create NX_class Attribute" );
        give_syslog_a_chance;
        H5NXdumperr("H5nx::H5NXmake_group(): H5Acreate2()"
            + std::string(" Create NX_class Attribute"));
        return FAIL;
    }

    if ( H5Awrite( aid, tid, class_name.c_str()) < 0)
    {
        syslog( LOG_ERR, "[%i] %s in %s(): Error in %s() %s=%s %s",
            g_pid, "STC Error", "H5nx::H5NXmake_group", "H5Awrite",
            "class_name", class_name.c_str(), "Write NX_class Attribute" );
        give_syslog_a_chance;
        H5NXdumperr(
            "H5nx::H5NXmake_group(): H5Awrite() Write NX_class Attribute "
            + class_name);
        return FAIL;
    }

    if ( H5Tclose( tid ) < 0 )
    {
        syslog( LOG_ERR, "[%i] %s in %s(): Error in %s() %s",
            g_pid, "STC Error", "H5nx::H5NXmake_group", "H5Tclose",
            "Close Type" );
        give_syslog_a_chance;
        H5NXdumperr("H5nx::H5NXmake_group(): H5Tclose() Close Type");
        return FAIL;
    }

    if ( H5Sclose( sid ) < 0 )
    {
        syslog( LOG_ERR, "[%i] %s in %s(): Error in %s() %s",
            g_pid, "STC Error", "H5nx::H5NXmake_group", "H5Sclose",
            "Close Dataspace" );
        give_syslog_a_chance;
        H5NXdumperr("H5nx::H5NXmake_group(): H5Sclose() Close Dataspace");
        return FAIL;
    }

    if ( H5Aclose( aid ) < 0 )
    {
        syslog( LOG_ERR, "[%i] %s in %s(): Error in %s() %s",
            g_pid, "STC Error", "H5nx::H5NXmake_group", "H5Aclose",
            "Close Attribute" );
        give_syslog_a_chance;
        H5NXdumperr("H5nx::H5NXmake_group(): H5Aclose() Close Attribute");
        return FAIL;
    }

    if ( H5Gclose( gid ) < 0 )
    {
        syslog( LOG_ERR, "[%i] %s in %s(): Error in %s() %s",
            g_pid, "STC Error", "H5nx::H5NXmake_group", "H5Gclose",
            "Close Group" );
        give_syslog_a_chance;
        H5NXdumperr("H5nx::H5NXmake_group(): H5Gclose() Close Group");
        return FAIL;
    }

    return SUCCEED;
}

//////////////////////////////////////////////////////////////////////////
//nx_to_hdf5_type
//////////////////////////////////////////////////////////////////////////
hid_t H5nx::nx_to_hdf5_type( int nx_datatype )
{
    hid_t type;
    if (nx_datatype == NX_CHAR)
    {
        type=H5T_C_S1;
    }
    else if (nx_datatype == NX_INT8)
    {
        type=H5T_NATIVE_CHAR;
    }
    else if (nx_datatype == NX_UINT8)
    {
        type=H5T_NATIVE_UCHAR;
    }
    else if (nx_datatype == NX_INT16)
    {
        type=H5T_NATIVE_SHORT;
    }
    else if (nx_datatype == NX_UINT16)
    {
        type=H5T_NATIVE_USHORT;
    }
    else if (nx_datatype == NX_INT32)
    {
        type=H5T_NATIVE_INT;
    }
    else if (nx_datatype == NX_UINT32)
    {
        type=H5T_NATIVE_UINT;
    }
    else if (nx_datatype == NX_INT64)
    {
        type = H5T_NATIVE_INT64;
    }
    else if (nx_datatype == NX_UINT64)
    {
        type = H5T_NATIVE_UINT64;
    }
    else if (nx_datatype == NX_FLOAT32)
    {
        type=H5T_NATIVE_FLOAT;
    }
    else if (nx_datatype == NX_FLOAT64)
    {
        type=H5T_NATIVE_DOUBLE;
    }
    else
    {
        syslog( LOG_ERR, "[%i] %s in %s(): Error Unknown Type %s=%ld",
            g_pid, "STC Error", "H5nx::nx_to_hdf5_type",
            "nx_datatype", (long) nx_datatype );
        give_syslog_a_chance;
        type = -1;
        assert ( 0 );
    }
    return type;
}

///////////////////////////////////////////////////////////////////
// H5NXmake_attribute_string
////////////////////////////////////////////////////////////////////
int H5nx::H5NXmake_attribute_string( const std::string &dataset_path,
        const std::string &attr_name, const std::string &attr_value  )
{
    hid_t   aid;  // attribute ID
    hid_t   sid;  // dataspace ID
    hid_t   tid;  // nx_datatype ID
    hid_t   did;  // dataset ID

    if ( (did = H5Dopen2( this->m_fid, dataset_path.c_str(),
            H5P_DEFAULT )) < 0 )
    {
        syslog( LOG_ERR, "[%i] %s in %s(): Error in %s() %s=%s %s",
            g_pid, "STC Error", "H5nx::H5NXmake_attribute_string",
            "H5Dopen2", "dataset_path", dataset_path.c_str(),
            "Open Dataset" );
        give_syslog_a_chance;
        H5NXdumperr(
            "H5nx::H5NXmake_attribute_string(): H5Dopen2() Open Dataset "
            + dataset_path);
        return FAIL;
    }

    // create a scalar dataspace
    if ( (sid = H5Screate( H5S_SCALAR )) < 0 )
    {
        syslog( LOG_ERR, "[%i] %s in %s(): Error in %s() %s",
            g_pid, "STC Error", "H5nx::H5NXmake_attribute_string",
            "H5Screate", "Create Dataspace" );
        give_syslog_a_chance;
        H5NXdumperr("H5nx::H5NXmake_attribute_string():"
            + std::string(" H5Screate() Create Dataspace"));
        return FAIL;
    }

    // copy type
    if ( (tid = H5Tcopy(H5T_C_S1)) < 0 )
    {
        syslog( LOG_ERR, "[%i] %s in %s(): Error in %s() %s",
            g_pid, "STC Error", "H5nx::H5NXmake_attribute_string",
            "H5Tcopy", "Write Type" );
        give_syslog_a_chance;
        H5NXdumperr(
            "H5nx::H5NXmake_attribute_string(): H5Tcopy() Write Type");
        return FAIL;
    }

    // length of data
    size_t size_attr = attr_value.size();

    if ( H5Tset_size( tid, size_attr ) < 0 )
    {
        syslog( LOG_ERR, "[%i] %s in %s(): Error in %s() %s=%ld",
            g_pid, "STC Error", "H5nx::H5NXmake_attribute_string",
            "H5Tset_size", "size_attr", (long) size_attr );
        give_syslog_a_chance;
        H5NXdumperr(
            "H5nx::H5NXmake_attribute_string(): H5Tset_size()");
        return FAIL;
    }

    if ( H5Tset_strpad(tid, H5T_STR_NULLTERM) < 0 )
    {
        syslog( LOG_ERR, "[%i] %s in %s(): Error in %s()",
            g_pid, "STC Error", "H5nx::H5NXmake_attribute_string",
            "H5Tset_strpad" );
        give_syslog_a_chance;
        H5NXdumperr("H5nx::H5NXmake_attribute_string(): H5Tset_strpad()");
        return FAIL;
    }

    if ( (aid = H5Acreate( did, attr_name.c_str(), tid, sid,
            H5P_DEFAULT, H5P_DEFAULT)) < 0 )
    {
        syslog( LOG_ERR, "[%i] %s in %s(): Error in %s() %s=%s %s",
            g_pid, "STC Error", "H5nx::H5NXmake_attribute_string",
            "H5Acreate", "attr_name", attr_name.c_str(),
            "Create Attribute" );
        give_syslog_a_chance;
        H5NXdumperr("H5nx::H5NXmake_attribute_string():"
            + std::string(" H5Acreate() Create Attribute ")
            + attr_name);
        return FAIL;
    }

    if ( H5Awrite( aid, tid, &(attr_value[0]) ) < 0 )
    {
        syslog( LOG_ERR, "[%i] %s in %s(): Error in %s() %s=%s %s",
            g_pid, "STC Error", "H5nx::H5NXmake_attribute_string",
            "H5Awrite", "attr_value", attr_value.c_str(),
            "Write Attribute" );
        give_syslog_a_chance;
        H5NXdumperr("H5nx::H5NXmake_attribute_string():"
            + std::string(" H5Awrite() Write Attribute ")
            + attr_value);
        return FAIL;
    }

    if ( H5Tclose( tid ) < 0 )
    {
        syslog( LOG_ERR, "[%i] %s in %s(): Error in %s() %s",
            g_pid, "STC Error", "H5nx::H5NXmake_attribute_string",
            "H5Tclose", "Close Type" );
        give_syslog_a_chance;
        H5NXdumperr(
            "H5nx::H5NXmake_attribute_string(): H5Tclose() Close Type");
        return FAIL;
    }

    if ( H5Sclose( sid ) < 0 )
    {
        syslog( LOG_ERR, "[%i] %s in %s(): Error in %s() %s",
            g_pid, "STC Error", "H5nx::H5NXmake_attribute_string",
            "H5Sclose", "Close Dataspace" );
        give_syslog_a_chance;
        H5NXdumperr("H5nx::H5NXmake_attribute_string():"
            + std::string(" H5Sclose() Close Dataspace"));
        return FAIL;
    }

    if ( H5Aclose( aid ) < 0 )
    {
        syslog( LOG_ERR, "[%i] %s in %s(): Error in %s() %s",
            g_pid, "STC Error", "H5nx::H5NXmake_attribute_string",
            "H5Aclose", "Close Attribute" );
        give_syslog_a_chance;
        H5NXdumperr("H5nx::H5NXmake_attribute_string():"
            + std::string(" H5Aclose() Close Attribute"));
        return FAIL;
    }

    if ( H5Dclose( did ) < 0 )
    {
        syslog( LOG_ERR, "[%i] %s in %s(): Error in %s() %s",
            g_pid, "STC Error", "H5nx::H5NXmake_attribute_string",
            "H5Dclose", "Close Dataset" );
        give_syslog_a_chance;
        H5NXdumperr(
            "H5nx::H5NXmake_attribute_string(): H5Dclose() Close Dataset");
        return FAIL;
    }

    return SUCCEED;
}

///////////////////////////////////////////////////////////////////
// H5NXcheck_attribute_string - create & set IFF not already set
////////////////////////////////////////////////////////////////////
int H5nx::H5NXcheck_attribute_string( const std::string &dataset_path,
        const std::string &attr_name, const std::string &attr_value,
        std::string &existing_attr_value, bool &wasSet )
{
    hid_t   aid;  // attribute ID
    hid_t   sid;  // dataspace ID
    hid_t   tid;  // nx_datatype ID
    hid_t   did;  // dataset ID

    if ( (did = H5Dopen2( this->m_fid, dataset_path.c_str(),
            H5P_DEFAULT )) < 0 )
    {
        syslog( LOG_ERR, "[%i] %s in %s(): Error in %s() %s=%s %s",
            g_pid, "STC Error", "H5nx::H5NXcheck_attribute_string",
            "H5Dopen2", "dataset_path", dataset_path.c_str(),
            "Open Dataset" );
        give_syslog_a_chance;
        H5NXdumperr(
            "H5nx::H5NXcheck_attribute_string(): H5Dopen2() Open Dataset "
            + dataset_path);
        return FAIL;
    }

    htri_t attr_exists;
    if ( (attr_exists = H5Aexists( did, attr_name.c_str() )) < 0 )
    {
        syslog( LOG_ERR, "[%i] %s in %s(): Error in %s() %s=%s %s",
            g_pid, "STC Error", "H5nx::H5NXcheck_attribute_string",
            "H5Aexists", "attr_name", attr_name.c_str(),
            "Check Attr Exists" );
        give_syslog_a_chance;
        H5NXdumperr(
     "H5nx::H5NXcheck_attribute_string(): H5Aexists() Check Attr Exists "
            + attr_name);
        return FAIL;
    }

    // attribute already exists, read it...
    if ( attr_exists > 0 )
    {
        // open attribute
        if ( (aid = H5Aopen( did, attr_name.c_str(), H5P_DEFAULT)) < 0 )
        {
            syslog( LOG_ERR, "[%i] %s in %s(): Error in %s() %s=%s %s",
                g_pid, "STC Error", "H5nx::H5NXcheck_attribute_string",
                "H5Aopen", "attr_name", attr_name.c_str(),
                "Open Attribute" );
            give_syslog_a_chance;
            H5NXdumperr("H5nx::H5NXcheck_attribute_string():"
                + std::string(" H5Acreate() Open Attribute ")
                + attr_name);
            return FAIL;
        }

        // get attribute storage size
        hsize_t attr_size = H5Aget_storage_size( aid );

        // create character buffer
        char *buffer = new char[ attr_size + 1 ];

        // copy type
        if ( (tid = H5Tcopy(H5T_C_S1)) < 0 )
        {
            syslog( LOG_ERR, "[%i] %s in %s(): Error in %s() %s",
                g_pid, "STC Error", "H5nx::H5NXcheck_attribute_string",
                "H5Tcopy", "Read Type" );
            give_syslog_a_chance;
            H5NXdumperr(
                "H5nx::H5NXcheck_attribute_string(): H5Tcopy() Read Type");
            return FAIL;
        }

        if ( H5Tset_size( tid, attr_size ) < 0 )
        {
            syslog( LOG_ERR, "[%i] %s in %s(): Error in %s() %s=%ld",
                g_pid, "STC Error", "H5nx::H5NXcheck_attribute_string",
                "H5Tset_size", "attr_size", (long) attr_size );
            give_syslog_a_chance;
            H5NXdumperr(
                "H5nx::H5NXcheck_attribute_string(): H5Tset_size()");
            return FAIL;
        }

        // read attribute value
        if ( H5Aread( aid, tid, buffer ) < 0 )
        {
            syslog( LOG_ERR, "[%i] %s in %s(): Error in %s() %s",
                g_pid, "STC Error", "H5nx::H5NXcheck_attribute_string",
                "H5Aread", "Read Attribute" );
            give_syslog_a_chance;
            H5NXdumperr("H5nx::H5NXcheck_attribute_string():"
                + std::string(" H5Awrite() Read Attribute"));
            return FAIL;
        }

        if ( H5Tclose( tid ) < 0 )
        {
            syslog( LOG_ERR, "[%i] %s in %s(): Error in %s() %s",
                g_pid, "STC Error", "H5nx::H5NXcheck_attribute_string",
                "H5Tclose", "Close Type" );
            give_syslog_a_chance;
            H5NXdumperr(
              "H5nx::H5NXcheck_attribute_string(): H5Tclose() Close Type");
            return FAIL;
        }

        if ( H5Aclose( aid ) < 0 )
        {
            syslog( LOG_ERR, "[%i] %s in %s(): Error in %s() %s",
                g_pid, "STC Error", "H5nx::H5NXcheck_attribute_string",
                "H5Aclose", "Close Attribute" );
            give_syslog_a_chance;
            H5NXdumperr("H5nx::H5NXcheck_attribute_string():"
                + std::string(" H5Aclose() Close Attribute"));
            return FAIL;
        }

        buffer[attr_size] = '\0';
        existing_attr_value = std::string( buffer );
        delete [] buffer;

        wasSet = true;
    }

    // attribute doesn't yet exist, create it...
    else
    {
        // create a scalar dataspace
        if ( (sid = H5Screate( H5S_SCALAR )) < 0 )
        {
            syslog( LOG_ERR, "[%i] %s in %s(): Error in %s() %s",
                g_pid, "STC Error", "H5nx::H5NXcheck_attribute_string",
                "H5Screate", "Create Dataspace" );
            give_syslog_a_chance;
            H5NXdumperr("H5nx::H5NXcheck_attribute_string():"
                + std::string(" H5Screate() Create Dataspace"));
            return FAIL;
        }

        // copy type
        if ( (tid = H5Tcopy(H5T_C_S1)) < 0 )
        {
            syslog( LOG_ERR, "[%i] %s in %s(): Error in %s() %s",
                g_pid, "STC Error", "H5nx::H5NXcheck_attribute_string",
                "H5Tcopy", "Write Type" );
            give_syslog_a_chance;
            H5NXdumperr(
               "H5nx::H5NXcheck_attribute_string(): H5Tcopy() Write Type");
            return FAIL;
        }

        // length of data
        size_t size_attr = attr_value.size();

        if ( H5Tset_size( tid, size_attr ) < 0 )
        {
            syslog( LOG_ERR, "[%i] %s in %s(): Error in %s() %s=%ld",
                g_pid, "STC Error", "H5nx::H5NXcheck_attribute_string",
                "H5Tset_size", "size_attr", (long) size_attr );
            give_syslog_a_chance;
            H5NXdumperr(
                "H5nx::H5NXcheck_attribute_string(): H5Tset_size()");
            return FAIL;
        }

        if ( H5Tset_strpad(tid, H5T_STR_NULLTERM) < 0 )
        {
            syslog( LOG_ERR, "[%i] %s in %s(): Error in %s()",
                g_pid, "STC Error", "H5nx::H5NXcheck_attribute_string",
                "H5Tset_strpad" );
            give_syslog_a_chance;
            H5NXdumperr(
                "H5nx::H5NXcheck_attribute_string(): H5Tset_strpad()");
            return FAIL;
        }

        if ( (aid = H5Acreate( did, attr_name.c_str(), tid, sid,
                H5P_DEFAULT, H5P_DEFAULT)) < 0 )
        {
            syslog( LOG_ERR, "[%i] %s in %s(): Error in %s() %s=%s %s",
                g_pid, "STC Error", "H5nx::H5NXcheck_attribute_string",
                "H5Acreate", "attr_name", attr_name.c_str(),
                "Create Attribute" );
            give_syslog_a_chance;
            H5NXdumperr("H5nx::H5NXcheck_attribute_string():"
                + std::string(" H5Acreate() Create Attribute ")
                + attr_name);
            return FAIL;
        }

        if ( H5Awrite( aid, tid, &(attr_value[0]) ) < 0 )
        {
            syslog( LOG_ERR, "[%i] %s in %s(): Error in %s() %s=%s %s",
                g_pid, "STC Error", "H5nx::H5NXcheck_attribute_string",
                "H5Awrite", "attr_value", attr_value.c_str(),
                "Write Attribute" );
            give_syslog_a_chance;
            H5NXdumperr("H5nx::H5NXcheck_attribute_string():"
                + std::string(" H5Awrite() Write Attribute ")
                + attr_value);
            return FAIL;
        }

        if ( H5Tclose( tid ) < 0 )
        {
            syslog( LOG_ERR, "[%i] %s in %s(): Error in %s() %s",
                g_pid, "STC Error", "H5nx::H5NXcheck_attribute_string",
                "H5Tclose", "Close Type" );
            give_syslog_a_chance;
            H5NXdumperr(
              "H5nx::H5NXcheck_attribute_string(): H5Tclose() Close Type");
            return FAIL;
        }

        if ( H5Sclose( sid ) < 0 )
        {
            syslog( LOG_ERR, "[%i] %s in %s(): Error in %s() %s",
                g_pid, "STC Error", "H5nx::H5NXcheck_attribute_string",
                "H5Sclose", "Close Dataspace" );
            give_syslog_a_chance;
            H5NXdumperr("H5nx::H5NXcheck_attribute_string():"
                + std::string(" H5Sclose() Close Dataspace"));
            return FAIL;
        }

        if ( H5Aclose( aid ) < 0 )
        {
            syslog( LOG_ERR, "[%i] %s in %s(): Error in %s() %s",
                g_pid, "STC Error", "H5nx::H5NXcheck_attribute_string",
                "H5Aclose", "Close Attribute" );
            give_syslog_a_chance;
            H5NXdumperr("H5nx::H5NXcheck_attribute_string():"
                + std::string(" H5Aclose() Close Attribute"));
            return FAIL;
        }

        existing_attr_value = "";
        wasSet = false;
    }

    if ( H5Dclose( did ) < 0 )
    {
        syslog( LOG_ERR, "[%i] %s in %s(): Error in %s() %s",
            g_pid, "STC Error", "H5nx::H5NXcheck_attribute_string",
            "H5Dclose", "Close Dataset" );
        give_syslog_a_chance;
        H5NXdumperr(
          "H5nx::H5NXcheck_attribute_string(): H5Dclose() Close Dataset");
        return FAIL;
    }

    return SUCCEED;
}

///////////////////////////////////////////////////////////////////
// H5NXmake_dataset_string
////////////////////////////////////////////////////////////////////
int H5nx::H5NXmake_dataset_string( const std::string &group_path,
        const std::string &dataset_name, const std::string &data )
{

    hid_t   did;  // dataset ID
    hid_t   sid;  // space ID
    hid_t   tid;  // nx_datatype ID
    hsize_t dim[1] = {1};

    std::string absolute_dataset_name = group_path + "/" + dataset_name;

    // create a 1D dataspace with a dimension of 1 element
    if ( (sid = H5Screate_simple( 1, dim, NULL )) < 0 )
    {
        syslog( LOG_ERR, "[%i] %s in %s(): Error in %s() %s",
            g_pid, "STC Error", "H5nx::H5NXmake_dataset_string",
            "H5Screate_simple", "Create Dataspace" );
        give_syslog_a_chance;
        H5NXdumperr("H5nx::H5NXmake_dataset_string(): H5Screate_simple()"
            + std::string(" Create Dataspace"));
        return FAIL;
    }

    if ( (tid = H5Tcopy(H5T_C_S1)) < 0 )
    {
        syslog( LOG_ERR, "[%i] %s in %s(): Error in %s() %s",
            g_pid, "STC Error", "H5nx::H5NXmake_dataset_string",
            "H5Tcopy", "Write Type" );
        give_syslog_a_chance;
        H5NXdumperr(
            "H5nx::H5NXmake_dataset_string(): H5Tcopy() Write Type");
        return FAIL;
    }

    // length of data
    size_t size_data = data.size();

    if ( H5Tset_size( tid, size_data ) < 0 )
    {
        syslog( LOG_ERR, "[%i] %s in %s(): Error in %s() %s=%ld",
            g_pid, "STC Error", "H5nx::H5NXmake_dataset_string",
            "H5Tset_size", "size_data", (long) size_data );
        give_syslog_a_chance;
        H5NXdumperr("H5nx::H5NXmake_dataset_string(): H5Tset_size()");
        return FAIL;
    }

    if ( H5Tset_strpad(tid, H5T_STR_NULLTERM) < 0 )
    {
        syslog( LOG_ERR, "[%i] %s in %s(): Error in %s()",
            g_pid, "STC Error", "H5nx::H5NXmake_dataset_string",
            "H5Tset_strpad" );
        give_syslog_a_chance;
        H5NXdumperr("H5nx::H5NXmake_dataset_string(): H5Tset_strpad()");
        return FAIL;
    }

    if ( (did = H5Dcreate2( m_fid, absolute_dataset_name.c_str(), tid, sid,
            H5P_DEFAULT, H5P_DEFAULT, H5P_DEFAULT )) < 0 )
    {
        syslog( LOG_ERR, "[%i] %s in %s(): Error in %s() %s=%s %s",
            g_pid, "STC Error", "H5nx::H5NXmake_dataset_string",
            "H5Dcreate2",
            "absolute_dataset_name", absolute_dataset_name.c_str(),
            "Create Dataset" );
        give_syslog_a_chance;
        H5NXdumperr("H5nx::H5NXmake_dataset_string(): H5Dcreate2()"
            + std::string(" Create Dataset ")
            + absolute_dataset_name);
        return FAIL;
    }

    if ( H5Dwrite( did, tid,
            H5S_ALL, H5S_ALL, H5P_DEFAULT, &(data[0]) ) < 0 )
    {
        syslog( LOG_ERR, "[%i] %s in %s(): Error in %s() %s",
            g_pid, "STC Error", "H5nx::H5NXmake_dataset_string",
            "H5Dwrite", "Write Dataset" );
        give_syslog_a_chance;
        H5NXdumperr(
            "H5nx::H5NXmake_dataset_string(): H5Dwrite() Write Dataset");
        return FAIL;
    }

    if ( H5Tclose( tid ) < 0 )
    {
        syslog( LOG_ERR, "[%i] %s in %s(): Error in %s() %s",
            g_pid, "STC Error", "H5nx::H5NXmake_dataset_string",
            "H5Tclose", "Close Type" );
        give_syslog_a_chance;
        H5NXdumperr(
            "H5nx::H5NXmake_dataset_string(): H5Tclose() Close Type");
        return FAIL;
    }

    if ( H5Sclose( sid ) < 0 )
    {
        syslog( LOG_ERR, "[%i] %s in %s(): Error in %s() %s",
            g_pid, "STC Error", "H5nx::H5NXmake_dataset_string",
            "H5Sclose", "Close Dataspace" );
        give_syslog_a_chance;
        H5NXdumperr(
            "H5nx::H5NXmake_dataset_string(): H5Sclose() Close Dataspace");
        return FAIL;
    }

    if ( H5Dclose( did ) < 0 )
    {
        syslog( LOG_ERR, "[%i] %s in %s(): Error in %s() %s",
            g_pid, "STC Error", "H5nx::H5NXmake_dataset_string",
            "H5Dclose", "Close Dataset" );
        give_syslog_a_chance;
        H5NXdumperr(
            "H5nx::H5NXmake_dataset_string(): H5Dclose() Close Dataset");
        return FAIL;
    }

    return SUCCEED;
}

///////////////////////////////////////////////////////////////////
// H5NXcheck_dataset_path
////////////////////////////////////////////////////////////////////
int H5nx::H5NXcheck_dataset_path( const std::string &group_path,
        const std::string &dataset_name, bool &exists )
{
    hid_t   gid;  // group ID
    hid_t   did;  // dataset ID

    // Try to Open the Enclosing Group First...
    if ( (gid = H5Gopen2( this->m_fid, group_path.c_str(),
            H5P_DEFAULT)) < 0 )
    {
        syslog( LOG_ERR, "[%i] %s in %s(): Error in %s() for %s - %s",
            g_pid, "STC Error", "H5nx::H5NXcheck_dataset_path", "H5Gopen2",
            group_path.c_str(),
            "Open Group Failed - Group Doesn't Exist...?" );
        give_syslog_a_chance;
        exists = false;
        return SUCCEED;
    }
    else {
        exists = true;
    }

    // If Successful, Try to Close It... ;-D
    if ( H5Gclose( gid ) < 0 )
    {
        syslog( LOG_ERR, "[%i] %s in %s(): Error in %s() for %s - %s",
            g_pid, "STC Error", "H5nx::H5NXcheck_dataset_path",
            "H5Gclose", group_path.c_str(), "Failed to Close Group" );
        give_syslog_a_chance;
        H5NXdumperr( "H5nx::H5NXcheck_dataset_path(): H5Gclose()"
            + std::string(" Failed to Close Group ") + group_path );
        return FAIL;
    }

    // If No Dataset Name, We're Done! Just Check Group Existence...
    if ( dataset_name.size() == 0 ) {
        return SUCCEED;
    }

    // Now Check Dataset Path in Group...
    std::string absolute_dataset_name = group_path + "/" + dataset_name;

    // Try to Open the Dataset Path...
    if ( (did = H5Dopen2( this->m_fid, absolute_dataset_name.c_str(),
            H5P_DEFAULT )) < 0 )
    {
        // Don't Log Here, This is the Expected Outcome... ;-D
        exists = false;
        return SUCCEED;
    }
    else {
        exists = true;
    }

    // If Successful, Try to Close It... ;-D
    if ( H5Dclose( did ) < 0 )
    {
        syslog( LOG_ERR, "[%i] %s in %s(): Error in %s() %s",
            g_pid, "STC Error", "H5nx::H5NXcheck_dataset_path",
            "H5Dclose", "Close Dataset" );
        give_syslog_a_chance;
        H5NXdumperr(
            "H5nx::H5NXcheck_dataset_path(): H5Dclose() Close Dataset "
            + absolute_dataset_name );
        return FAIL;
    }

    return SUCCEED;
}

///////////////////////////////////////////////////////////////////
// to_nx_type
////////////////////////////////////////////////////////////////////

//   NeXus::NXnumtype
//
//    FLOAT32 = NX_FLOAT32,
//    FLOAT64 = NX_FLOAT64,
//    INT8 = NX_INT8,
//    UINT8 = NX_UINT8,
//    BOOLEAN = NX_BOOLEAN,
//    INT16 = NX_INT16,
//    UINT16 = NX_UINT16,
//    INT32 = NX_INT32,
//    UINT32 = NX_UINT32,
//    INT64 = NX_INT64,
//    UINT64 = NX_UINT64,
//    CHAR = NX_CHAR,
//    BINARY = NX_BINARY


template <typename NumT>
NeXus::NXnumtype to_nx_type( NumT number = NumT() );

template<>
NeXus::NXnumtype to_nx_type( uint16_t UNUSED(number) )
{
    return NeXus::UINT16;
}

template<>
NeXus::NXnumtype to_nx_type( double UNUSED(number) )
{
    return NeXus::FLOAT64;
}

template<>
NeXus::NXnumtype to_nx_type( uint32_t UNUSED(number) )
{
    return NeXus::UINT32;
}

template<>
NeXus::NXnumtype to_nx_type( uint64_t UNUSED(number) )
{
    return NeXus::UINT64;
}

template<>
NeXus::NXnumtype to_nx_type( float UNUSED(number) )
{
    return NeXus::FLOAT32;
}


///////////////////////////////////////////////////////////////////
// H5NXmake_attribute_scalar
// create/write a SCALAR NUMERICAL attribute
////////////////////////////////////////////////////////////////////

// declare instantiations of the types of templated functions needed
template
int H5nx::H5NXmake_attribute_scalar( const std::string &dataset_path,
        const std::string &attr_name, const uint16_t &value );
template
int H5nx::H5NXmake_attribute_scalar( const std::string &dataset_path,
        const std::string &attr_name, const uint32_t &value );
template
int H5nx::H5NXmake_attribute_scalar( const std::string &dataset_path,
        const std::string &attr_name, const uint64_t &value );

template <typename NumT>
int H5nx::H5NXmake_attribute_scalar( const std::string &dataset_path,
        const std::string &attr_name, const NumT &value )
{
    hid_t   did;  // dataset ID
    hid_t   aid;  // attribute ID
    hid_t   sid;  // space ID
    hid_t   tid;  // nx_datatype ID
    hid_t   wtid; // nx_datatype ID

    if ( (did = H5Dopen2( this->m_fid, dataset_path.c_str(),
            H5P_DEFAULT )) < 0 )
    {
        syslog( LOG_ERR, "[%i] %s in %s(): Error in %s() %s=%s %s",
            g_pid, "STC Error", "H5nx::H5NXmake_attribute_scalar",
            "H5Dopen2", "dataset_path", dataset_path.c_str(),
            "Open Dataset" );
        give_syslog_a_chance;
        H5NXdumperr(
            "H5nx::H5NXmake_attribute_scalar(): H5Dopen2() Open Dataset "
            + dataset_path);
        return FAIL;
    }

    // get the NeXus type; some template magic here
    NeXus::NXnumtype nx_numtype = to_nx_type<NumT>();

    // get the HDF5 type from the NeXus type
    tid = nx_to_hdf5_type( nx_numtype );

    // create a scalar dataspace
    if ( (sid = H5Screate( H5S_SCALAR )) < 0 )
    {
        syslog( LOG_ERR, "[%i] %s in %s(): Error in %s() %s",
            g_pid, "STC Error", "H5nx::H5NXmake_attribute_scalar",
            "H5Screate", "Create Dataspace" );
        give_syslog_a_chance;
        H5NXdumperr("H5nx::H5NXmake_attribute_scalar(): H5Screate()"
            + std::string(" Create Dataspace"));
        return FAIL;
    }

    if ( (wtid = H5Tcopy( tid )) < 0 )
    {
        syslog( LOG_ERR, "[%i] %s in %s(): Error in %s() %s",
            g_pid, "STC Error", "H5nx::H5NXmake_attribute_scalar",
            "H5Tcopy", "Write Type" );
        give_syslog_a_chance;
        H5NXdumperr(
            "H5nx::H5NXmake_attribute_scalar(): H5Tcopy() Write Type");
        return FAIL;
    }

    if ( (aid = H5Acreate( did, attr_name.c_str(), wtid, sid,
            H5P_DEFAULT, H5P_DEFAULT)) < 0 )
    {
        syslog( LOG_ERR, "[%i] %s in %s(): Error in %s() %s=%s %s",
            g_pid, "STC Error", "H5nx::H5NXmake_attribute_scalar",
            "H5Acreate", "attr_name", attr_name.c_str(),
            "Create Attribute" );
        give_syslog_a_chance;
        H5NXdumperr("H5nx::H5NXmake_attribute_scalar(): H5Acreate()"
            + std::string(" Create Attribute ")
            + attr_name);
        return FAIL;
    }

    if ( H5Awrite( aid, wtid, &value ) < 0 )
    {
        syslog( LOG_ERR, "[%i] %s in %s(): Error in %s() %s=%u %s",
            g_pid, "STC Error", "H5nx::H5NXmake_attribute_scalar",
            "H5Awrite", attr_name.c_str(), (unsigned) value,
            "Write Attribute" );
        give_syslog_a_chance;
        H5NXdumperr("H5nx::H5NXmake_attribute_scalar(): H5Awrite()"
            + std::string(" Write Attribute"));
        return FAIL;
    }

    if ( H5Tclose( wtid ) < 0 )
    {
        syslog( LOG_ERR, "[%i] %s in %s(): Error in %s() %s",
            g_pid, "STC Error", "H5nx::H5NXmake_attribute_scalar",
            "H5Tclose", "Close Type" );
        give_syslog_a_chance;
        H5NXdumperr(
            "H5nx::H5NXmake_attribute_scalar(): H5Tclose() Close Type");
        return FAIL;
    }

    if ( H5Sclose( sid ) < 0 )
    {
        syslog( LOG_ERR, "[%i] %s in %s(): Error in %s() %s",
            g_pid, "STC Error", "H5nx::H5NXmake_attribute_scalar",
            "H5Sclose", "Close Dataspace" );
        give_syslog_a_chance;
        H5NXdumperr("H5nx::H5NXmake_attribute_scalar(): H5Sclose()"
            + std::string(" Close Dataspace"));
        return FAIL;
    }

    if ( H5Aclose( aid ) < 0 )
    {
        syslog( LOG_ERR, "[%i] %s in %s(): Error in %s() %s",
            g_pid, "STC Error", "H5nx::H5NXmake_attribute_scalar",
            "H5Aclose", "Close Attribute" );
        give_syslog_a_chance;
        H5NXdumperr("H5nx::H5NXmake_attribute_scalar(): H5Aclose()"
            + std::string(" Close Attribute"));
        return FAIL;
    }

    if ( H5Dclose( did ) < 0 )
    {
        syslog( LOG_ERR, "[%i] %s in %s(): Error in %s() %s",
            g_pid, "STC Error", "H5nx::H5NXmake_attribute_scalar",
            "H5Dclose", "Close Dataset" );
        give_syslog_a_chance;
        H5NXdumperr(
            "H5nx::H5NXmake_attribute_scalar(): H5Dclose() Close Dataset");
        return FAIL;
    }

    return SUCCEED;
}

///////////////////////////////////////////////////////////////////
// H5NXwrite_attribute_scalar
// write a SCALAR NUMERICAL attribute
////////////////////////////////////////////////////////////////////

// declare instantiations of the types of templated functions needed
template
int H5nx::H5NXwrite_attribute_scalar( const std::string &dataset_path,
        const std::string &attr_name, const uint16_t &value );
template
int H5nx::H5NXwrite_attribute_scalar( const std::string &dataset_path,
        const std::string &attr_name, const uint32_t &value );
template
int H5nx::H5NXwrite_attribute_scalar( const std::string &dataset_path,
        const std::string &attr_name, const uint64_t &value );

template <typename NumT>
int H5nx::H5NXwrite_attribute_scalar( const std::string &dataset_path,
        const std::string &attr_name, const NumT &value )
{
    hid_t   did;  // dataset ID
    hid_t   aid;  // attribute ID
    hid_t   tid;  // nx_datatype ID

    if ( (did = H5Dopen2( this->m_fid, dataset_path.c_str(),
            H5P_DEFAULT )) < 0 )
    {
        syslog( LOG_ERR, "[%i] %s in %s(): Error in %s() %s=%s %s",
            g_pid, "STC Error", "H5nx::H5NXwrite_attribute_scalar",
            "H5Dopen2", "dataset_path", dataset_path.c_str(),
            "Open Dataset" );
        give_syslog_a_chance;
        H5NXdumperr(
            "H5nx::H5NXwrite_attribute_scalar(): H5Dopen2() Open Dataset "
            + dataset_path);
        return FAIL;
    }

    if ( (aid = H5Aopen( did, attr_name.c_str(), H5P_DEFAULT )) < 0 )
    {
        syslog( LOG_ERR, "[%i] %s in %s(): Error in %s() %s=%s %s",
            g_pid, "STC Error", "H5nx::H5NXwrite_attribute_scalar",
            "H5Aopen", "attr_name", attr_name.c_str(),
            "Open Attribute" );
        give_syslog_a_chance;
        H5NXdumperr("H5nx::H5NXwrite_attribute_scalar(): H5Aopen()"
            + std::string(" Open Attribute ")
            + attr_name);
        return FAIL;
    }

    // get the NeXus type; some template magic here
    NeXus::NXnumtype nx_numtype = to_nx_type<NumT>();

    // get the HDF5 type from the NeXus type
    tid = nx_to_hdf5_type( nx_numtype );

    if ( H5Awrite( aid, tid, &value ) < 0 )
    {
        syslog( LOG_ERR, "[%i] %s in %s(): Error in %s() %s=%u %s",
            g_pid, "STC Error", "H5nx::H5NXwrite_attribute_scalar",
            "H5Awrite", attr_name.c_str(), (unsigned) value,
            "Write Attribute" );
        give_syslog_a_chance;
        H5NXdumperr("H5nx::H5NXwrite_attribute_scalar(): H5Awrite()"
            + std::string(" Write Attribute"));
        return FAIL;
    }

    if ( H5Aclose( aid ) < 0 )
    {
        syslog( LOG_ERR, "[%i] %s in %s(): Error in %s() %s",
            g_pid, "STC Error", "H5nx::H5NXwrite_attribute_scalar",
            "H5Aclose", "Close Attribute" );
        give_syslog_a_chance;
        H5NXdumperr("H5nx::H5NXwrite_attribute_scalar(): H5Aclose()"
            + std::string(" Close Attribute"));
        return FAIL;
    }

    if ( H5Dclose( did ) < 0 )
    {
        syslog( LOG_ERR, "[%i] %s in %s(): Error in %s() %s",
            g_pid, "STC Error", "H5nx::H5NXwrite_attribute_scalar",
            "H5Dclose", "Close Dataset" );
        give_syslog_a_chance;
        H5NXdumperr("H5nx::H5NXwrite_attribute_scalar(): H5Dclose()"
            + std::string(" Close Dataset"));
        return FAIL;
    }

    return SUCCEED;
}

///////////////////////////////////////////////////////////////////
// H5NXread_attribute_scalar
// read a SCALAR NUMERICAL attribute
////////////////////////////////////////////////////////////////////

// declare instantiations of the types of templated functions needed
template
int H5nx::H5NXread_attribute_scalar( const std::string &dataset_path,
        const std::string &attr_name, uint16_t &value );
template
int H5nx::H5NXread_attribute_scalar( const std::string &dataset_path,
        const std::string &attr_name, uint32_t &value );
template
int H5nx::H5NXread_attribute_scalar( const std::string &dataset_path,
        const std::string &attr_name, uint64_t &value );

template <typename NumT>
int H5nx::H5NXread_attribute_scalar( const std::string &dataset_path,
        const std::string &attr_name, NumT &value )
{
    hid_t   did;  // dataset ID
    hid_t   aid;  // attribute ID
    hid_t   tid;  // nx_datatype ID

    if ( (did = H5Dopen2( this->m_fid, dataset_path.c_str(),
            H5P_DEFAULT )) < 0 )
    {
        syslog( LOG_ERR, "[%i] %s in %s(): Error in %s() %s=%s %s",
            g_pid, "STC Error", "H5nx::H5NXread_attribute_scalar",
            "H5Dopen2", "dataset_path", dataset_path.c_str(),
            "Open Dataset" );
        give_syslog_a_chance;
        H5NXdumperr(
            "H5nx::H5NXread_attribute_scalar(): H5Dopen2() Open Dataset "
            + dataset_path);
        return FAIL;
    }

    if ( (aid = H5Aopen( did, attr_name.c_str(), H5P_DEFAULT )) < 0 )
    {
        syslog( LOG_ERR, "[%i] %s in %s(): Error in %s() %s=%s %s",
            g_pid, "STC Error", "H5nx::H5NXread_attribute_scalar",
            "H5Aopen", "attr_name", attr_name.c_str(),
            "Open Attribute" );
        give_syslog_a_chance;
        H5NXdumperr("H5nx::H5NXread_attribute_scalar(): H5Aopen()"
            + std::string(" Open Attribute ")
            + attr_name);
        return FAIL;
    }

    // get the NeXus type; some template magic here
    NeXus::NXnumtype nx_numtype = to_nx_type<NumT>();

    // get the HDF5 type from the NeXus type
    tid = nx_to_hdf5_type( nx_numtype );

    if ( H5Aread( aid, tid, &value ) < 0 )
    {
        syslog( LOG_ERR, "[%i] %s in %s(): Error in %s() %s=%u %s",
            g_pid, "STC Error", "H5nx::H5NXread_attribute_scalar",
            "H5Aread", attr_name.c_str(), (unsigned) value,
            "Write Attribute" );
        give_syslog_a_chance;
        H5NXdumperr("H5nx::H5NXread_attribute_scalar(): H5Aread()"
            + std::string(" Write Attribute"));
        return FAIL;
    }

    if ( H5Aclose( aid ) < 0 )
    {
        syslog( LOG_ERR, "[%i] %s in %s(): Error in %s() %s",
            g_pid, "STC Error", "H5nx::H5NXread_attribute_scalar",
            "H5Aclose", "Close Attribute" );
        give_syslog_a_chance;
        H5NXdumperr("H5nx::H5NXread_attribute_scalar(): H5Aclose()"
            + std::string(" Close Attribute"));
        return FAIL;
    }

    if ( H5Dclose( did ) < 0 )
    {
        syslog( LOG_ERR, "[%i] %s in %s(): Error in %s() %s",
            g_pid, "STC Error", "H5nx::H5NXread_attribute_scalar",
            "H5Dclose", "Close Dataset" );
        give_syslog_a_chance;
        H5NXdumperr("H5nx::H5NXread_attribute_scalar(): H5Dclose()"
            + std::string(" Close Dataset"));
        return FAIL;
    }

    return SUCCEED;
}


///////////////////////////////////////////////////////////////////
// H5NXmake_dataset_scalar
// create/write a SCALAR NUMERICAL dataset
////////////////////////////////////////////////////////////////////

// declare instantiations of the types of templated functions needed
template
int H5nx::H5NXmake_dataset_scalar( const std::string &group_path,
        const std::string &dataset_name, const uint16_t &value );

template
int H5nx::H5NXmake_dataset_scalar( const std::string &group_path,
        const std::string &dataset_name, const double &value );

template
int H5nx::H5NXmake_dataset_scalar( const std::string &group_path,
        const std::string &dataset_name, const float &value );

template
int H5nx::H5NXmake_dataset_scalar( const std::string &group_path,
        const std::string &dataset_name, const uint32_t &value );

template
int H5nx::H5NXmake_dataset_scalar( const std::string &group_path,
        const std::string &dataset_name, const uint64_t &value );

template <typename NumT>
int H5nx::H5NXmake_dataset_scalar( const std::string &group_path,
        const std::string &dataset_name, const NumT &value )
{
    hid_t   did;                   // dataset ID
    hid_t   sid;                   // space ID
    hid_t   tid;                   // nx_datatype ID
    hsize_t dim[1] = {1};

    //get the NeXus type; some template magic here
    NeXus::NXnumtype nx_numtype = to_nx_type<NumT>();

    //get the HDF5 type from the NeXus type
    tid = nx_to_hdf5_type( nx_numtype );

    //construct the dataset absolute path
    std::string absolute_dataset_name = group_path + "/" + dataset_name;

    //create a 1D dataspace with a dimension of 1 element
    if ( (sid = H5Screate_simple( 1, dim, NULL )) < 0 )
    {
        syslog( LOG_ERR, "[%i] %s in %s(): Error in %s() %s",
            g_pid, "STC Error", "H5nx::H5NXmake_dataset_scalar",
            "H5Screate_simple", "Create Dataspace" );
        give_syslog_a_chance;
        H5NXdumperr("H5nx::H5NXmake_dataset_scalar(): H5Screate_simple()"
            + std::string(" Create Dataspace"));
        return FAIL;
    }

    //create dataset
    if ( (did = H5Dcreate2(m_fid, absolute_dataset_name.c_str(), tid, sid,
            H5P_DEFAULT, H5P_DEFAULT, H5P_DEFAULT )) < 0 )
    {
        syslog( LOG_ERR, "[%i] %s in %s(): Error in %s() %s=%s %s",
            g_pid, "STC Error", "H5nx::H5NXmake_dataset_scalar",
            "H5Dcreate2",
            "absolute_dataset_name", absolute_dataset_name.c_str(),
            "Create Dataset" );
        give_syslog_a_chance;
        H5NXdumperr("H5nx::H5NXmake_dataset_scalar(): H5Dcreate2()"
            + std::string(" Create Dataset ")
            + absolute_dataset_name);
        return FAIL;
    }

    if ( H5Dwrite( did, tid, H5S_ALL, H5S_ALL, H5P_DEFAULT, &value ) < 0 )
    {
        syslog( LOG_ERR, "[%i] %s in %s(): Error in %s() %s",
            g_pid, "STC Error", "H5nx::H5NXmake_dataset_scalar",
            "H5Dwrite", "Write Dataset" );
        give_syslog_a_chance;
        H5NXdumperr(
            "H5nx::H5NXmake_dataset_scalar(): H5Dwrite() Write Dataset");
        return FAIL;
    }

    if ( H5Sclose( sid ) < 0 )
    {
        syslog( LOG_ERR, "[%i] %s in %s(): Error in %s() %s",
            g_pid, "STC Error", "H5nx::H5NXmake_dataset_scalar",
            "H5Sclose", "Close Dataspace" );
        give_syslog_a_chance;
        H5NXdumperr(
            "H5nx::H5NXmake_dataset_scalar(): H5Sclose() Close Dataspace");
        return FAIL;
    }

    //close dataset
    if ( H5Dclose( did ) < 0 )
    {
        syslog( LOG_ERR, "[%i] %s in %s(): Error in %s() %s",
            g_pid, "STC Error", "H5nx::H5NXmake_dataset_scalar",
            "H5Dclose", "Close Dataset" );
        give_syslog_a_chance;
        H5NXdumperr(
            "H5nx::H5NXmake_dataset_scalar(): H5Dclose() Close Dataset");
        return FAIL;
    }

    return SUCCEED;
}

///////////////////////////////////////////////////////////////////
// H5NXwrite_dataset_scalar
// create/write a SCALAR NUMERICAL dataset
////////////////////////////////////////////////////////////////////

// declare instantiations of the types of templated functions needed
template
int H5nx::H5NXwrite_dataset_scalar( const std::string &dataset_path,
        uint16_t &value );

template
int H5nx::H5NXwrite_dataset_scalar( const std::string &dataset_path,
        double &value );

template
int H5nx::H5NXwrite_dataset_scalar( const std::string &dataset_path,
        float &value );

template
int H5nx::H5NXwrite_dataset_scalar( const std::string &dataset_path,
        uint32_t &value );

template
int H5nx::H5NXwrite_dataset_scalar( const std::string &dataset_path,
        uint64_t &value );

template <typename NumT>
int H5nx::H5NXwrite_dataset_scalar( const std::string &dataset_path,
        NumT &value )
{
    hid_t   did;                   // dataset ID
    hid_t   tid;                   // nx_datatype ID

    //open dataset
    if ( (did = H5Dopen2( this->m_fid, dataset_path.c_str(),
            H5P_DEFAULT )) < 0 )
    {
        syslog( LOG_ERR, "[%i] %s in %s(): Error in %s() %s=%s %s",
            g_pid, "STC Error", "H5nx::H5NXwrite_dataset_scalar",
            "H5Dopen2", "dataset_path", dataset_path.c_str(),
            "Open Dataset" );
        give_syslog_a_chance;
        H5NXdumperr("H5nx::H5NXwrite_dataset_scalar(): H5Dopen2()"
            + std::string(" Open Dataset ")
            + dataset_path);
        return FAIL;
    }

    //get the NeXus type; some template magic here
    NeXus::NXnumtype nx_numtype = to_nx_type<NumT>();

    //get the HDF5 type from the NeXus type
    tid = nx_to_hdf5_type( nx_numtype );

    if ( H5Dwrite( did, tid, H5S_ALL, H5S_ALL, H5P_DEFAULT, &value ) < 0 )
    {
        syslog( LOG_ERR, "[%i] %s in %s(): Error in %s() %s",
            g_pid, "STC Error", "H5nx::H5NXwrite_dataset_scalar",
            "H5Dwrite", "Read Dataset" );
        give_syslog_a_chance;
        H5NXdumperr(
            "H5nx::H5NXwrite_dataset_scalar(): H5Dwrite() Read Dataset");
        return FAIL;
    }

    //close dataset
    if ( H5Dclose( did ) < 0 )
    {
        syslog( LOG_ERR, "[%i] %s in %s(): Error in %s() %s",
            g_pid, "STC Error", "H5nx::H5NXwrite_dataset_scalar",
            "H5Dclose", "Close Dataset" );
        give_syslog_a_chance;
        H5NXdumperr(
            "H5nx::H5NXwrite_dataset_scalar(): H5Dclose() Close Dataset");
        return FAIL;
    }

    return SUCCEED;
}

///////////////////////////////////////////////////////////////////
// H5NXread_dataset_scalar
// create/write a SCALAR NUMERICAL dataset
////////////////////////////////////////////////////////////////////

// declare instantiations of the types of templated functions needed
template
int H5nx::H5NXread_dataset_scalar( const std::string &dataset_path,
        uint16_t &value );

template
int H5nx::H5NXread_dataset_scalar( const std::string &dataset_path,
        double &value );

template
int H5nx::H5NXread_dataset_scalar( const std::string &dataset_path,
        float &value );

template
int H5nx::H5NXread_dataset_scalar( const std::string &dataset_path,
        uint32_t &value );

template
int H5nx::H5NXread_dataset_scalar( const std::string &dataset_path,
        uint64_t &value );

template <typename NumT>
int H5nx::H5NXread_dataset_scalar( const std::string &dataset_path,
        NumT &value )
{
    hid_t   did;                   // dataset ID
    hid_t   tid;                   // nx_datatype ID

    //open dataset
    if ( (did = H5Dopen2( this->m_fid, dataset_path.c_str(),
            H5P_DEFAULT )) < 0 )
    {
        syslog( LOG_ERR, "[%i] %s in %s(): Error in %s() %s=%s %s",
            g_pid, "STC Error", "H5nx::H5NXread_dataset_scalar",
            "H5Dopen2", "dataset_path", dataset_path.c_str(),
            "Open Dataset" );
        give_syslog_a_chance;
        H5NXdumperr("H5nx::H5NXread_dataset_scalar(): H5Dopen2()"
            + std::string(" Open Dataset ")
            + dataset_path);
        return FAIL;
    }

    //get the NeXus type; some template magic here
    NeXus::NXnumtype nx_numtype = to_nx_type<NumT>();

    //get the HDF5 type from the NeXus type
    tid = nx_to_hdf5_type( nx_numtype );

    if ( H5Dread( did, tid, H5S_ALL, H5S_ALL, H5P_DEFAULT, &value ) < 0 )
    {
        syslog( LOG_ERR, "[%i] %s in %s(): Error in %s() %s",
            g_pid, "STC Error", "H5nx::H5NXread_dataset_scalar",
            "H5Dread", "Read Dataset" );
        give_syslog_a_chance;
        H5NXdumperr(
            "H5nx::H5NXread_dataset_scalar(): H5Dread() Read Dataset");
        return FAIL;
    }

    //close dataset
    if ( H5Dclose( did ) < 0 )
    {
        syslog( LOG_ERR, "[%i] %s in %s(): Error in %s() %s",
            g_pid, "STC Error", "H5nx::H5NXread_dataset_scalar",
            "H5Dclose", "Close Dataset" );
        give_syslog_a_chance;
        H5NXdumperr(
            "H5nx::H5NXread_dataset_scalar(): H5Dclose() Close Dataset");
        return FAIL;
    }

    return SUCCEED;
}

///////////////////////////////////////////////////////////////////
// H5NXmake_dataset_vector
// create/write a SCALAR NUMERICAL dataset
////////////////////////////////////////////////////////////////////

// Declare instantiations of the types of templated functions needed

template int H5nx::H5NXmake_dataset_vector( const string &group_path,
        const string &dataset_name,
        const vector<double> &vec,
        int rank,
        const vector<hsize_t> &dim_vec );

template int H5nx::H5NXmake_dataset_vector( const string &group_path,
        const string &dataset_name,
        const vector<uint32_t> &vec,
        int rank,
        const vector<hsize_t> &dim_vec );

template int H5nx::H5NXmake_dataset_vector( const string &group_path,
        const string &dataset_name,
        const vector<uint64_t> &vec,
        int rank,
        const vector<hsize_t> &dim_vec );

template int H5nx::H5NXmake_dataset_vector( const string &group_path,
        const string &dataset_name,
        const vector<float> &vec,
        int rank,
        const vector<hsize_t> &dim_vec );

template <typename NumT>
int H5nx::H5NXmake_dataset_vector( const string &group_path,
        const string &dataset_name,
        const vector<NumT> &vec,
        int rank,
        const vector<hsize_t> &dim_vec )
{
    hid_t did = -1;
    hid_t sid = -1;
    hid_t tid = -1;

    assert ( (size_t)rank == dim_vec.size() );

    hsize_t dims[H5S_MAX_RANK];     // dataset dimensions

    for ( int i = 0 ; i < rank ; i++ )
    {
        dims[ i ] = dim_vec.at( i );
    }

    //get the NeXus type; some template magic here
    NeXus::NXnumtype nx_numtype = to_nx_type<NumT>();

    //get the HDF5 type from the NeXus type
    tid = nx_to_hdf5_type( nx_numtype );

    // create the data space for the dataset
    if ( (sid = H5Screate_simple(rank, dims, NULL)) < 0 )
    {
        syslog( LOG_ERR, "[%i] %s in %s(): Error in %s() %s",
            g_pid, "STC Error", "H5nx::H5NXmake_dataset_vector",
            "H5Screate_simple", "Create Dataspace" );
        give_syslog_a_chance;
        H5NXdumperr("H5nx::H5NXmake_dataset_vector(): H5Screate_simple()"
            + std::string(" Create Dataspace"));
        return FAIL;
    }

    //construct the dataset absolute path
    std::string dataset_path = group_path + "/" + dataset_name;

    // create the dataset
    if ( (did = H5Dcreate2( this->m_fid, dataset_path.c_str(), tid, sid,
            H5P_DEFAULT, H5P_DEFAULT, H5P_DEFAULT)) < 0 )
    {
        syslog( LOG_ERR, "[%i] %s in %s(): Error in %s() %s=%s %s",
            g_pid, "STC Error", "H5nx::H5NXmake_dataset_vector",
            "H5Dcreate2", "dataset_path", dataset_path.c_str(),
            "Create Dataset" );
        give_syslog_a_chance;
        H5NXdumperr("H5nx::H5NXmake_dataset_vector(): H5Dcreate2()"
            + std::string(" Create Dataset ")
            + dataset_path);
        return FAIL;
    }

    // write the dataset only if there is data to write
    if ( vec.size() )
    {
        if ( H5Dwrite(did, tid,
                H5S_ALL, H5S_ALL, H5P_DEFAULT, &(vec[0])) < 0 )
        {
            syslog( LOG_ERR, "[%i] %s in %s(): Error in %s() %s",
                g_pid, "STC Error", "H5nx::H5NXmake_dataset_vector",
                "H5Dwrite", "Write Dataset" );
            give_syslog_a_chance;
            H5NXdumperr("H5nx::H5NXmake_dataset_vector(): H5Dwrite()"
                + std::string(" Write Dataset"));
            return FAIL;
        }
    }

    // end access to the dataset and release resources used by it
    if ( H5Dclose(did) < 0 )
    {
        syslog( LOG_ERR, "[%i] %s in %s(): Error in %s() %s",
            g_pid, "STC Error", "H5nx::H5NXmake_dataset_vector",
            "H5Dclose", "Close Dataset" );
        give_syslog_a_chance;
        H5NXdumperr(
            "H5nx::H5NXmake_dataset_vector(): H5Dclose() Close Dataset");
        return FAIL;
    }

    // terminate access to the data space
    if ( H5Sclose(sid) < 0 )
    {
        syslog( LOG_ERR, "[%i] %s in %s(): Error in %s() %s",
            g_pid, "STC Error", "H5nx::H5NXmake_dataset_vector",
            "H5Sclose", "Close Dataspace" );
        give_syslog_a_chance;
        H5NXdumperr(
            "H5nx::H5NXmake_dataset_vector(): H5Sclose() Close Dataspace");
        return FAIL;
    }

    return SUCCEED;
}

int H5nx::H5NXmake_dataset_vector( const string &group_path,
        const string &dataset_name,
        const vector<string> &vec,
        int rank,
        const vector<hsize_t> &dim_vec )
{
    hid_t did = -1;
    hid_t sid = -1;
    hid_t tid = -1;

    assert ( (size_t)rank == dim_vec.size() );

    hsize_t dims[H5S_MAX_RANK];     // dataset dimensions
    
    for ( int i = 0 ; i < rank ; i++ )
    {
        dims[ i ] = dim_vec.at( i );
    }

    // make a copy of H5T_C_S1
    if ( (tid = H5Tcopy( H5T_C_S1 )) < 0 )
    {
        syslog( LOG_ERR, "[%i] %s in %s(): Error in %s() %s",
            g_pid, "STC Error", "H5nx::H5NXmake_dataset_vector",
            "H5Tcopy", "Copy String Type" );
        give_syslog_a_chance;
        H5NXdumperr("H5nx::H5NXmake_dataset_vector(): H5Tcopy()"
            + std::string(" Copy String Type"));
        return FAIL;
    }

    // set size of string
    if ( H5Tset_size( tid, dims[ rank - 1 ] ) < 0 )
    {
        syslog( LOG_ERR, "[%i] %s in %s(): Error in %s() %s",
            g_pid, "STC Error", "H5nx::H5NXmake_dataset_vector",
            "H5Tset_size", "Set String Element Size" );
        give_syslog_a_chance;
        H5NXdumperr("H5nx::H5NXmake_dataset_vector(): H5Tset_size()"
            + std::string(" Set String Element Size"));
        return FAIL;
    }

    // now the last dimension collapses... ;-D
    dims[ rank - 1 ] = 1;

    if ( H5Tset_strpad( tid, H5T_STR_NULLTERM ) < 0 )
    {
        syslog( LOG_ERR, "[%i] %s in %s(): Error in %s()",
            g_pid, "STC Error", "H5nx::H5NXmake_dataset_vector",
            "H5Tset_strpad" );
        give_syslog_a_chance;
        H5NXdumperr("H5nx::H5NXmake_dataset_vector(): H5Tset_strpad()");
        return FAIL;
    }

    // create the data space for the dataset
    if ( (sid = H5Screate_simple(rank, dims, NULL)) < 0 )
    {
        syslog( LOG_ERR, "[%i] %s in %s(): Error in %s() %s",
            g_pid, "STC Error", "H5nx::H5NXmake_dataset_vector",
            "H5Screate_simple", "Create Dataspace" );
        give_syslog_a_chance;
        H5NXdumperr("H5nx::H5NXmake_dataset_vector(): H5Screate_simple()"
            + std::string(" Create Dataspace"));
        return FAIL;
    }

    //construct the dataset absolute path
    std::string dataset_path = group_path + "/" + dataset_name;

    // create the dataset
    if ( (did = H5Dcreate2( this->m_fid, dataset_path.c_str(), tid, sid,
            H5P_DEFAULT, H5P_DEFAULT, H5P_DEFAULT)) < 0 )
    {
        syslog( LOG_ERR, "[%i] %s in %s(): Error in %s() %s=%s %s",
            g_pid, "STC Error", "H5nx::H5NXmake_dataset_vector",
            "H5Dcreate2", "dataset_path", dataset_path.c_str(),
            "Create Dataset" );
        give_syslog_a_chance;
        H5NXdumperr("H5nx::H5NXmake_dataset_vector(): H5Dcreate2()"
            + std::string(" Create Dataset ")
            + dataset_path);
        return FAIL;
    }

    // write the dataset only if there is data to write
    if ( vec.size() )
    {
        std::string str_array;
        for ( uint32_t i=0 ; i < vec.size() ; i++ )
            str_array.append( vec[i] );

        if ( H5Dwrite( did, tid,
                H5S_ALL, H5S_ALL, H5P_DEFAULT, &(str_array[0]) ) < 0 )
        {
            syslog( LOG_ERR, "[%i] %s in %s(): Error in %s() %s",
                g_pid, "STC Error", "H5nx::H5NXmake_dataset_vector",
                "H5Dwrite", "Write Dataset" );
            give_syslog_a_chance;
            H5NXdumperr("H5nx::H5NXmake_dataset_vector(): H5Dwrite()"
                + std::string(" Write Dataset"));
            return FAIL;
        }
    }

    // end access to the dataset and release resources used by it
    if ( H5Dclose( did ) < 0 )
    {
        syslog( LOG_ERR, "[%i] %s in %s(): Error in %s() %s",
            g_pid, "STC Error", "H5nx::H5NXmake_dataset_vector",
            "H5Dclose", "Close Dataset" );
        give_syslog_a_chance;
        H5NXdumperr(
            "H5nx::H5NXmake_dataset_vector(): H5Dclose() Close Dataset");
        return FAIL;
    }

    // terminate access to the data space
    if ( H5Sclose( sid ) < 0 )
    {
        syslog( LOG_ERR, "[%i] %s in %s(): Error in %s() %s",
            g_pid, "STC Error", "H5nx::H5NXmake_dataset_vector",
            "H5Sclose", "Close Dataspace" );
        give_syslog_a_chance;
        H5NXdumperr(
            "H5nx::H5NXmake_dataset_vector(): H5Sclose() Close Dataspace");
        return FAIL;
    }

    if ( H5Tclose( tid ) < 0 )
    {
        syslog( LOG_ERR, "[%i] %s in %s(): Error in %s() %s",
            g_pid, "STC Error", "H5nx::H5NXmake_dataset_vector",
            "H5Tclose", "Close String Type" );
        give_syslog_a_chance;
        H5NXdumperr("H5nx::H5NXmake_dataset_vector(): H5Tclose()"
            + std::string(" Close String Type"));
        return FAIL;
    }

    return SUCCEED;
}

//////////////////////////////////////////////////////////////////////////
//H5MPcreate_dataset_extend
// 1D dataset
//////////////////////////////////////////////////////////////////////////
int H5nx::H5NXcreate_dataset_extend( const std::string &group_path,
        const std::string &dataset_name,
        int nxdatatype,
        hsize_t chunk_size )
{
    hsize_t maxdim[H5S_MAX_RANK];    // dataset dimensions
    hsize_t dim[H5S_MAX_RANK];       // dataset dimensions
    hsize_t chunk_dim[H5S_MAX_RANK]; // chunk dimensions

    hid_t   dcpl = -1;          // dataset creation property list
    hid_t   sid = -1;           // dataspace
    hid_t   tid = -1;           // datatype
    hid_t   did = -1;           // dataset ID

    std::string path = group_path + "/" + dataset_name;

    dim[0] = 0;
    maxdim[0] = H5S_UNLIMITED;
    chunk_dim[0] = chunk_size;
        // NOTE: Chunk Size is measured in *Dataset Elements*...! :-O

    //create  dataspace
    if ( (sid = H5Screate_simple( 1, dim, maxdim)) < 0 )
    {
        syslog( LOG_ERR, "[%i] %s in %s(): Error in %s() %s",
            g_pid, "STC Error", "H5nx::H5NXcreate_dataset_extend",
            "H5Screate_simple", "Create Dataspace" );
        give_syslog_a_chance;
        H5NXdumperr("H5nx::H5NXcreate_dataset_extend(): H5Screate_simple()"
            + std::string(" Create Dataspace"));
        return FAIL;
    }

    // dataset creation property list
    if ( (dcpl = H5Pcreate(H5P_DATASET_CREATE)) < 0 )
    {
        syslog( LOG_ERR, "[%i] %s in %s(): Error in %s() %s",
            g_pid, "STC Error", "H5nx::H5NXcreate_dataset_extend",
            "H5Pcreate", "Create Property List" );
        give_syslog_a_chance;
        H5NXdumperr("H5nx::H5NXcreate_dataset_extend(): H5Pcreate()"
            + std::string(" Create Property List"));
        return FAIL;
    }

    //set chunking
    if ( H5Pset_chunk(dcpl, 1, chunk_dim) < 0 )
    {
        syslog( LOG_ERR, "[%i] %s in %s(): Error in %s() %s=%ld %s",
            g_pid, "STC Error", "H5nx::H5NXcreate_dataset_extend",
            "H5Pset_chunk", "chunk_dim", (long) chunk_dim[0],
            "Set Chunk Size" );
        give_syslog_a_chance;
        H5NXdumperr("H5nx::H5NXcreate_dataset_extend(): H5Pset_chunk()"
            + std::string(" Set Chunk Size"));
        return FAIL;
    }

    if ( m_compression_level > 0 && m_compression_level < 10 )
    {
        //set compression
        if ( H5Pset_deflate(dcpl, m_compression_level) < 0 )
        {
            syslog( LOG_ERR, "[%i] %s in %s(): Error in %s() %s=%ld %s",
                g_pid, "STC Error", "H5nx::H5NXcreate_dataset_extend",
                "H5Pset_deflate",
                "m_compression_level", (long) m_compression_level,
                "Set Compression Level" );
            give_syslog_a_chance;
            H5NXdumperr(
                "H5nx::H5NXcreate_dataset_extend(): H5Pset_deflate()"
                    + std::string(" Set Compression Level"));
            return FAIL;
        }

        //shuffle data byte order to aid compression
        if ( H5Pset_shuffle(dcpl) < 0 )
        {
            syslog( LOG_ERR, "[%i] %s in %s(): Error in %s() %s",
                g_pid, "STC Error", "H5nx::H5NXcreate_dataset_extend",
                "H5Pset_shuffle", "Set Shuffle Mode" );
            give_syslog_a_chance;
            H5NXdumperr(
                "H5nx::H5NXcreate_dataset_extend(): H5Pset_shuffle()"
                + std::string(" Set Shuffle Mode"));
            return FAIL;
        }
    }

    tid = nx_to_hdf5_type( nxdatatype );

    //create dataset with modified dcpl
    if ( (did = H5Dcreate2(m_fid, path.c_str(), tid, sid,
            H5P_DEFAULT, dcpl, H5P_DEFAULT )) < 0 )
    {
        syslog( LOG_ERR, "[%i] %s in %s(): Error in %s() %s=%s %s",
            g_pid, "STC Error", "H5nx::H5NXcreate_dataset_extend",
            "H5Dcreate2", "path", path.c_str(),
            "Create Modified Dataset" );
        give_syslog_a_chance;
        H5NXdumperr("H5nx::H5NXcreate_dataset_extend(): H5Dcreate2()"
            + std::string(" Create Modified Dataset ")
            + path);
        return FAIL;
    }

    if ( H5Pclose(dcpl) < 0 )
    {
        syslog( LOG_ERR, "[%i] %s in %s(): Error in %s() %s",
            g_pid, "STC Error", "H5nx::H5NXcreate_dataset_extend",
            "H5Pclose", "Close Property List" );
        give_syslog_a_chance;
        H5NXdumperr("H5nx::H5NXcreate_dataset_extend(): H5Pclose()"
            + std::string(" Close Property List"));
        return FAIL;
    }

    if ( H5Dclose( did ) < 0 )
    {
        syslog( LOG_ERR, "[%i] %s in %s(): Error in %s() %s",
            g_pid, "STC Error", "H5nx::H5NXcreate_dataset_extend",
            "H5Dclose", "Close Dataset" );
        give_syslog_a_chance;
        H5NXdumperr("H5nx::H5NXcreate_dataset_extend(): H5Dclose()"
            + std::string(" Close Dataset"));
        return FAIL;
    }

    return SUCCEED;
}

///////////////////////////////////////////////////////////////////
// H5NXget_dataset_dims
////////////////////////////////////////////////////////////////////

int H5nx::H5NXget_dataset_dims( const std::string &dataset_path,
    int &rank, std::vector<hsize_t> &dim_vec )
{
    hid_t   did;
    hid_t   sid;
    hsize_t dims[H5S_MAX_RANK];     // dataset dimensions

    //open dataset
    if ( (did = H5Dopen2( this->m_fid, dataset_path.c_str(),
            H5P_DEFAULT )) < 0 )
    {
        syslog( LOG_ERR, "[%i] %s in %s(): Error in %s() %s=%s %s",
            g_pid, "STC Error", "H5nx::H5NXget_dataset_dims", "H5Dopen2",
            "dataset_path", dataset_path.c_str(), "Open Dataset" );
        give_syslog_a_chance;
        H5NXdumperr("H5nx::H5NXget_dataset_dims(): H5Dopen2()"
            + std::string(" Open Dataset ")
            + dataset_path);
        return FAIL;
    }

    //get the dataspace
    if ( (sid = H5Dget_space( did )) < 0 )
    {
        syslog( LOG_ERR, "[%i] %s in %s(): Error in %s() %s",
            g_pid, "STC Error", "H5nx::H5NXget_dataset_dims",
            "H5Dget_space", "Get Dataspace" );
        give_syslog_a_chance;
        H5NXdumperr(
            "H5nx::H5NXget_dataset_dims(): H5Dget_space()"
                + std::string(" Get Dataspace"));
        return FAIL;
    }

    //get dataset extent dims
    if ( (rank = H5Sget_simple_extent_dims( sid, dims, NULL )) < 0 )
    {
        syslog( LOG_ERR, "[%i] %s in %s(): Error in %s() %s",
            g_pid, "STC Error", "H5nx::H5NXget_dataset_dims",
            "H5Sget_simple_extent_dims",
            "Get Dataspace Extent Dimensions" );
        give_syslog_a_chance;
        H5NXdumperr("H5nx::H5NXget_dataset_dims(): H5Screate_simple()"
            + std::string(" Create Memory Dataspace"));
        return FAIL;
    }

    for ( int i = 0 ; i < rank ; i++ )
    {
        dim_vec[ i ] = dims[ i ];
    }

    //close dataspace
    if ( H5Sclose( sid ) < 0 )
    {
        syslog( LOG_ERR, "[%i] %s in %s(): Error in %s() %s",
            g_pid, "STC Error", "H5nx::H5NXget_dataset_dims", "H5Sclose",
            "Close Data Space" );
        give_syslog_a_chance;
        H5NXdumperr("H5nx::H5NXget_dataset_dims(): H5Sclose()"
            + std::string(" Close Data Space"));
        return FAIL;
    }

    //close dataset
    if ( H5Dclose( did ) < 0 )
    {
        syslog( LOG_ERR, "[%i] %s in %s(): Error in %s() %s",
            g_pid, "STC Error", "H5nx::H5NXget_dataset_dims", "H5Dclose",
            "Close Dataset" );
        give_syslog_a_chance;
        H5NXdumperr("H5nx::H5NXget_dataset_dims(): H5Dclose()"
            + std::string(" Close Dataset"));
        return FAIL;
    }

    return SUCCEED;
}

///////////////////////////////////////////////////////////////////
// H5NXget_vector_size()
////////////////////////////////////////////////////////////////////

hsize_t H5nx::H5NXget_vector_size( int rank,
        std::vector<hsize_t> &dim_vec )
{
    hsize_t size = 1;
    for ( int i=0 ; i < rank ; i++ )
    {
        size *= dim_vec[i];
    }
    return( size );
}

///////////////////////////////////////////////////////////////////
// H5NXwrite_slab
////////////////////////////////////////////////////////////////////

template
int H5nx::H5NXwrite_slab( const std::string &dataset_path,
        const std::vector<double> &slab, uint64_t slab_size,
        uint64_t cur_size );

template
int H5nx::H5NXwrite_slab( const std::string &dataset_path,
        const std::vector<float> &slab, uint64_t slab_size,
        uint64_t cur_size );

template
int H5nx::H5NXwrite_slab( const std::string &dataset_path,
        const std::vector<uint16_t> &slab, uint64_t slab_size,
        uint64_t cur_size );

template
int H5nx::H5NXwrite_slab( const std::string &dataset_path,
        const std::vector<int16_t> &slab, uint64_t slab_size,
        uint64_t cur_size );

template
int H5nx::H5NXwrite_slab( const std::string &dataset_path,
        const std::vector<uint32_t> &slab, uint64_t slab_size,
        uint64_t cur_size );

template
int H5nx::H5NXwrite_slab( const std::string &dataset_path,
        const std::vector<int32_t> &slab, uint64_t slab_size,
        uint64_t cur_size );

template
int H5nx::H5NXwrite_slab( const std::string &dataset_path,
        const std::vector<uint64_t> &slab, uint64_t slab_size,
        uint64_t cur_size );

template
int H5nx::H5NXwrite_slab( const std::string &dataset_path,
        const std::vector<int64_t> &slab, uint64_t slab_size,
        uint64_t cur_size );

template
int H5nx::H5NXwrite_slab( const std::string &dataset_path,
        const std::vector<char> &slab, uint64_t slab_size,
        uint64_t cur_size );

template <typename NumT>
int H5nx::H5NXwrite_slab( const std::string &dataset_path,
        const std::vector<NumT> &vec, uint64_t slab_size,
        uint64_t cur_size )
{
    hid_t   did;
    hid_t   tid;
    hid_t   sid;
    hid_t   msid;
    hsize_t dims[H5S_MAX_RANK];
    hsize_t count[H5S_MAX_RANK];
    hsize_t start[H5S_MAX_RANK];

    ///////////////////////////////////////////////////////////////////
    // FOR 1D DATASET
    ////////////////////////////////////////////////////////////////////

    //open dataset
    if ( (did = H5Dopen2( this->m_fid, dataset_path.c_str(),
            H5P_DEFAULT )) < 0 )
    {
        syslog( LOG_ERR, "[%i] %s in %s(): Error in %s() %s=%s %s",
            g_pid, "STC Error", "H5nx::H5NXwrite_slab", "H5Dopen2",
            "dataset_path", dataset_path.c_str(), "Open Dataset" );
        give_syslog_a_chance;
        H5NXdumperr("H5nx::H5NXwrite_slab(): H5Dopen2() Open Dataset "
            + dataset_path);
        return FAIL;
    }

    //get type
    if ( (tid = H5Dget_type( did )) < 0 )
    {
        syslog( LOG_ERR, "[%i] %s in %s(): Error in %s() %s",
            g_pid, "STC Error", "H5nx::H5NXwrite_slab", "H5Dget_type",
            "Get Type" );
        give_syslog_a_chance;
        H5NXdumperr("H5nx::H5NXwrite_slab(): H5Dget_type() Get Type");
        return FAIL;
    }

    dims[0] = cur_size + slab_size;

    if ( H5Dextend( did, dims ) < 0 )
    {
        syslog( LOG_ERR, "[%i] %s in %s(): Error in %s() %s=%lu %s",
            g_pid, "STC Error", "H5nx::H5NXwrite_slab", "H5Dextend",
            "dims[0]", (unsigned long) dims[0], "Extend Dataset" );
        give_syslog_a_chance;
        H5NXdumperr("H5nx::H5NXwrite_slab(): H5Dextend() Extend Dataset");
        return FAIL;
    }

    //get the new (updated) space
    if ( (sid = H5Dget_space( did )) < 0 )
    {
        syslog( LOG_ERR, "[%i] %s in %s(): Error in %s() %s",
            g_pid, "STC Error", "H5nx::H5NXwrite_slab", "H5Dget_space",
            "Get Dataspace" );
        give_syslog_a_chance;
        H5NXdumperr(
            "H5nx::H5NXwrite_slab(): H5Dget_space() Get Dataspace");
        return FAIL;
    }

    count[0] = slab_size;
    start[0] = cur_size;

    //select space on file
    if ( H5Sselect_hyperslab( sid, H5S_SELECT_SET,
            start, NULL, count, NULL ) < 0 )
    {
        syslog( LOG_ERR,
            "[%i] %s in %s(): Error in %s() %s=%lu %s=%lu %s",
            g_pid, "STC Error", "H5nx::H5NXwrite_slab",
            "H5Sselect_hyperslab",
            "start[0]", (unsigned long) start[0],
            "count[0]", (unsigned long) count[0],
            "Select Hyperslab File Space" );
        give_syslog_a_chance;
        H5NXdumperr("H5nx::H5NXwrite_slab(): H5Sselect_hyperslab()"
            + std::string("Select Hyperslab File Space"));
        return FAIL;
    }

    //memory space
    if ( (msid = H5Screate_simple( 1, count, count )) < 0 )
    {
        syslog( LOG_ERR, "[%i] %s in %s(): Error in %s() %s=%lu %s",
            g_pid, "STC Error", "H5nx::H5NXwrite_slab", "H5Screate_simple",
            "count[0]", (unsigned long) count[0],
            "Create Memory Dataspace" );
        give_syslog_a_chance;
        H5NXdumperr("H5nx::H5NXwrite_slab(): H5Screate_simple()"
            + std::string(" Create Memory Dataspace"));
        return FAIL;
    }

    //write
    if ( H5Dwrite( did, tid, msid, sid, H5P_DEFAULT, &(vec[0]) ) < 0 )
    {
        syslog( LOG_ERR, "[%i] %s in %s(): Error in %s() %s (Disk Space?)",
            g_pid, "STC Error", "H5nx::H5NXwrite_slab", "H5Dwrite",
            "Write Data" );
        give_syslog_a_chance;
        H5NXdumperr("H5nx::H5NXwrite_slab(): H5Dwrite() Write Data");
        return FAIL;
    }

    //close memory space
    if ( H5Sclose( msid ) < 0 )
    {
        syslog( LOG_ERR, "[%i] %s in %s(): Error in %s() %s",
            g_pid, "STC Error", "H5nx::H5NXwrite_slab", "H5Sclose",
            "Close Memory Dataspace" );
        give_syslog_a_chance;
        H5NXdumperr(
            "H5nx::H5NXwrite_slab(): H5Sclose() Close Memory Dataspace");
        return FAIL;
    }

    //close file space
    if ( H5Sclose( sid ) < 0 )
    {
        syslog( LOG_ERR, "[%i] %s in %s(): Error in %s() %s",
            g_pid, "STC Error", "H5nx::H5NXwrite_slab", "H5Sclose",
            "Close File Space" );
        give_syslog_a_chance;
        H5NXdumperr("H5nx::H5NXwrite_slab(): H5Sclose() Close File Space");
        return FAIL;
    }

    //close type
    if ( H5Tclose( tid ) < 0 )
    {
        syslog( LOG_ERR, "[%i] %s in %s(): Error in %s() %s",
            g_pid, "STC Error", "H5nx::H5NXwrite_slab", "H5Tclose",
            "Close Type" );
        give_syslog_a_chance;
        H5NXdumperr("H5nx::H5NXwrite_slab(): H5Tclose() Close Type");
        return FAIL;
    }

    //close dataset
    if ( H5Dclose( did ) < 0 )
    {
        syslog( LOG_ERR, "[%i] %s in %s(): Error in %s() %s",
            g_pid, "STC Error", "H5nx::H5NXwrite_slab", "H5Dclose",
            "Close Dataset" );
        give_syslog_a_chance;
        H5NXdumperr("H5nx::H5NXwrite_slab(): H5Dclose() Close Dataset");
        return FAIL;
    }

    return SUCCEED;
}

///////////////////////////////////////////////////////////////////
// H5NXread_slab
////////////////////////////////////////////////////////////////////

// NOTE: Supplied Vector Buffer Must Be Pre-Allocated and Initialized
// to Hold "slab_size" Elements...! (Use "assign()", Not "reserve()"!)

template
int H5nx::H5NXread_slab( const std::string &dataset_path,
        std::vector<double> &slab, uint64_t slab_size,
        uint64_t slab_offset );

template
int H5nx::H5NXread_slab( const std::string &dataset_path,
        std::vector<float> &slab, uint64_t slab_size,
        uint64_t slab_offset );

template
int H5nx::H5NXread_slab( const std::string &dataset_path,
        std::vector<uint16_t> &slab, uint64_t slab_size,
        uint64_t slab_offset );

template
int H5nx::H5NXread_slab( const std::string &dataset_path,
        std::vector<int16_t> &slab, uint64_t slab_size,
        uint64_t slab_offset );

template
int H5nx::H5NXread_slab( const std::string &dataset_path,
        std::vector<uint32_t> &slab, uint64_t slab_size,
        uint64_t slab_offset );

template
int H5nx::H5NXread_slab( const std::string &dataset_path,
        std::vector<int32_t> &slab, uint64_t slab_size,
        uint64_t slab_offset );

template
int H5nx::H5NXread_slab( const std::string &dataset_path,
        std::vector<uint64_t> &slab, uint64_t slab_size,
        uint64_t slab_offset );

template
int H5nx::H5NXread_slab( const std::string &dataset_path,
        std::vector<int64_t> &slab, uint64_t slab_size,
        uint64_t slab_offset );

template
int H5nx::H5NXread_slab( const std::string &dataset_path,
        std::vector<char> &slab, uint64_t slab_size,
        uint64_t slab_offset );

template <typename NumT>
int H5nx::H5NXread_slab( const std::string &dataset_path,
        std::vector<NumT> &vec, uint64_t slab_size,
        uint64_t slab_offset )
{
    hid_t   did;
    hid_t   tid;
    hid_t   sid;
    hid_t   msid;
    hsize_t count[H5S_MAX_RANK];
    hsize_t start[H5S_MAX_RANK];

    // NOTE: Supplied Vector Buffer Must Be Pre-Allocated and Initialized
    // to Hold "slab_size" Elements...! (Use "assign()", Not "reserve()"!)

    ///////////////////////////////////////////////////////////////////
    // FOR 1D DATASET
    ////////////////////////////////////////////////////////////////////

    //open dataset
    if ( (did = H5Dopen2( this->m_fid, dataset_path.c_str(),
            H5P_DEFAULT )) < 0 )
    {
        syslog( LOG_ERR, "[%i] %s in %s(): Error in %s() %s=%s %s",
            g_pid, "STC Error", "H5nx::H5NXread_slab", "H5Dopen2",
            "dataset_path", dataset_path.c_str(), "Open Dataset" );
        give_syslog_a_chance;
        H5NXdumperr("H5nx::H5NXread_slab(): H5Dopen2() Open Dataset "
            + dataset_path);
        return FAIL;
    }

    //get type
    if ( (tid = H5Dget_type( did )) < 0 )
    {
        syslog( LOG_ERR, "[%i] %s in %s(): Error in %s() %s",
            g_pid, "STC Error", "H5nx::H5NXread_slab", "H5Dget_type",
            "Get Type" );
        give_syslog_a_chance;
        H5NXdumperr("H5nx::H5NXread_slab(): H5Dget_type() Get Type");
        return FAIL;
    }

    //get the dataspace
    if ( (sid = H5Dget_space( did )) < 0 )
    {
        syslog( LOG_ERR, "[%i] %s in %s(): Error in %s() %s",
            g_pid, "STC Error", "H5nx::H5NXread_slab", "H5Dget_space",
            "Get Dataspace" );
        give_syslog_a_chance;
        H5NXdumperr(
            "H5nx::H5NXread_slab(): H5Dget_space() Get Dataspace");
        return FAIL;
    }

    count[0] = slab_size;
    start[0] = slab_offset;

    //select space on file
    if ( H5Sselect_hyperslab( sid, H5S_SELECT_SET,
            start, NULL, count, NULL ) < 0 )
    {
        syslog( LOG_ERR,
            "[%i] %s in %s(): Error in %s() %s=%lu %s=%lu %s",
            g_pid, "STC Error", "H5nx::H5NXread_slab",
            "H5Sselect_hyperslab",
            "start[0]", (unsigned long) start[0],
            "count[0]", (unsigned long) count[0],
            "Select Hyperslab File Space" );
        give_syslog_a_chance;
        H5NXdumperr("H5nx::H5NXread_slab(): H5Sselect_hyperslab()"
            + std::string("Select Hyperslab File Space"));
        return FAIL;
    }

    //memory space
    if ( (msid = H5Screate_simple( 1, count, count )) < 0 )
    {
        syslog( LOG_ERR, "[%i] %s in %s(): Error in %s() %s=%lu %s",
            g_pid, "STC Error", "H5nx::H5NXread_slab", "H5Screate_simple",
            "count[0]", (unsigned long) count[0],
            "Create Memory Dataspace" );
        give_syslog_a_chance;
        H5NXdumperr("H5nx::H5NXread_slab(): H5Screate_simple()"
            + std::string(" Create Memory Dataspace"));
        return FAIL;
    }

    //read
    if ( H5Dread( did, tid, msid, sid, H5P_DEFAULT, &(vec[0]) ) < 0 )
    {
        syslog( LOG_ERR, "[%i] %s in %s(): Error in %s() %s (Disk Space?)",
            g_pid, "STC Error", "H5nx::H5NXread_slab", "H5Dread",
            "Read Data" );
        give_syslog_a_chance;
        H5NXdumperr("H5nx::H5NXread_slab(): H5Dread() Read Data");
        return FAIL;
    }

    //close memory space
    if ( H5Sclose( msid ) < 0 )
    {
        syslog( LOG_ERR, "[%i] %s in %s(): Error in %s() %s",
            g_pid, "STC Error", "H5nx::H5NXread_slab", "H5Sclose",
            "Close Memory Dataspace" );
        give_syslog_a_chance;
        H5NXdumperr(
            "H5nx::H5NXread_slab(): H5Sclose() Close Memory Dataspace");
        return FAIL;
    }

    //close file space
    if ( H5Sclose( sid ) < 0 )
    {
        syslog( LOG_ERR, "[%i] %s in %s(): Error in %s() %s",
            g_pid, "STC Error", "H5nx::H5NXread_slab", "H5Sclose",
            "Close File Space" );
        give_syslog_a_chance;
        H5NXdumperr("H5nx::H5NXread_slab(): H5Sclose() Close File Space");
        return FAIL;
    }

    //close type
    if ( H5Tclose( tid ) < 0 )
    {
        syslog( LOG_ERR, "[%i] %s in %s(): Error in %s() %s",
            g_pid, "STC Error", "H5nx::H5NXread_slab", "H5Tclose",
            "Close Type" );
        give_syslog_a_chance;
        H5NXdumperr("H5nx::H5NXread_slab(): H5Tclose() Close Type");
        return FAIL;
    }

    //close dataset
    if ( H5Dclose( did ) < 0 )
    {
        syslog( LOG_ERR, "[%i] %s in %s(): Error in %s() %s",
            g_pid, "STC Error", "H5nx::H5NXread_slab", "H5Dclose",
            "Close Dataset" );
        give_syslog_a_chance;
        H5NXdumperr("H5nx::H5NXread_slab(): H5Dclose() Close Dataset");
        return FAIL;
    }

    return SUCCEED;
}

//////////////////////////////////////////////////////////////////////////
//private functions
//////////////////////////////////////////////////////////////////////////


//////////////////////////////////////////////////////////////////////////
//write_root_metadata
//////////////////////////////////////////////////////////////////////////
int H5nx::write_root_metadata( const char *file_name )
{
    char      *time_buffer = NULL;
    char      version_nr[10];
    unsigned  int vers_major, vers_minor, vers_release;
    hid_t     gid;
    hid_t     aid;
    hid_t     tid;
    hid_t     sid;

    if ( (gid = H5Gopen2( this->m_fid, "/", H5P_DEFAULT)) < 0 )
    {
        syslog( LOG_ERR, "[%i] %s in %s(): Error in %s() %s",
            g_pid, "STC Error", "H5nx::write_root_metadata", "H5Gopen2",
            "Open Root Group" );
        give_syslog_a_chance;
        H5NXdumperr(
            "H5nx::write_root_metadata(): H5Gopen2() Open Root Group");
        return FAIL;
    }

    // NeXus_version

    if ( (sid = H5Screate(H5S_SCALAR)) < 0 )
    {
        syslog( LOG_ERR, "[%i] %s in %s(): Error in %s() %s",
            g_pid, "STC Error", "H5nx::write_root_metadata", "H5Screate",
            "Create NeXus Version Dataspace" );
        give_syslog_a_chance;
        H5NXdumperr("H5nx::write_root_metadata(): H5Screate()"
            + std::string(" Create NeXus Version Dataspace"));
        return FAIL;
    }

    if ( (tid = H5Tcopy(H5T_C_S1)) < 0 )
    {
        syslog( LOG_ERR, "[%i] %s in %s(): Error in %s() %s",
            g_pid, "STC Error", "H5nx::write_root_metadata", "H5Tcopy",
            "Copy NeXus Version Type" );
        give_syslog_a_chance;
        H5NXdumperr("H5nx::write_root_metadata(): H5Tcopy()"
            + std::string(" Copy NeXus Version Type"));
        return FAIL;
    }

    if ( H5Tset_size( tid, strlen(NEXUS_VERSION) ) < 0 )
    {
        syslog( LOG_ERR, "[%i] %s in %s(): Error in %s() %s",
            g_pid, "STC Error", "H5nx::write_root_metadata", "H5Tset_size",
            "Set NeXus Version Size" );
        give_syslog_a_chance;
        H5NXdumperr("H5nx::write_root_metadata(): H5Tset_size()"
            + std::string(" Set NeXus Version Size"));
        return FAIL;
    }

    if ( (aid = H5Acreate2( gid, "NeXus_version", tid, sid,
            H5P_DEFAULT, H5P_DEFAULT)) < 0 )
    {
        syslog( LOG_ERR, "[%i] %s in %s(): Error in %s() %s",
            g_pid, "STC Error", "H5nx::write_root_metadata", "H5Acreate2",
            "Create NeXus Version Attribute" );
        give_syslog_a_chance;
        H5NXdumperr("H5nx::write_root_metadata(): H5Acreate2()"
            + std::string(" Create NeXus Version Attribute"));
        return FAIL;
    }

    if ( H5Awrite( aid, tid, NEXUS_VERSION ) < 0 )
    {
        syslog( LOG_ERR, "[%i] %s in %s(): Error in %s() %s=%s %s",
            g_pid, "STC Error", "H5nx::write_root_metadata", "H5Awrite",
            "NEXUS_VERSION", NEXUS_VERSION,
            "Write NeXus Version Attribute" );
        give_syslog_a_chance;
        H5NXdumperr("H5nx::write_root_metadata(): H5Awrite()"
            + std::string(" Write NeXus Version Attribute"));
        return FAIL;
    }

    if ( H5Tclose( tid ) < 0 )
    {
        syslog( LOG_ERR, "[%i] %s in %s(): Error in %s() %s",
            g_pid, "STC Error", "H5nx::write_root_metadata", "H5Tclose",
            "Close NeXus Version Type" );
        give_syslog_a_chance;
        H5NXdumperr("H5nx::write_root_metadata(): H5Tclose()"
            + std::string(" Close NeXus Version Type"));
        return FAIL;
    }

    if ( H5Sclose( sid ) < 0 )
    {
        syslog( LOG_ERR, "[%i] %s in %s(): Error in %s() %s",
            g_pid, "STC Error", "H5nx::write_root_metadata", "H5Sclose",
            "Close NeXus Version Dataspace" );
        give_syslog_a_chance;
        H5NXdumperr("H5nx::write_root_metadata(): H5Sclose()"
            + std::string(" Close NeXus Version Dataspace"));
        return FAIL;
    }

    if ( H5Aclose( aid ) < 0 )
    {
        syslog( LOG_ERR, "[%i] %s in %s(): Error in %s() %s",
            g_pid, "STC Error", "H5nx::write_root_metadata", "H5Aclose",
            "Close NeXus Version Attribute" );
        give_syslog_a_chance;
        H5NXdumperr("H5nx::write_root_metadata(): H5Aclose()"
            + std::string(" Close NeXus Version Attribute"));
        return FAIL;
    }

    //file_name

    if ( (sid = H5Screate(H5S_SCALAR)) < 0 )
    {
        syslog( LOG_ERR, "[%i] %s in %s(): Error in %s() %s",
            g_pid, "STC Error", "H5nx::write_root_metadata", "H5Screate",
            "Create File Name Dataspace" );
        give_syslog_a_chance;
        H5NXdumperr("H5nx::write_root_metadata(): H5Screate()"
            + std::string(" Create File Name Dataspace"));
        return FAIL;
    }

    if ( (tid = H5Tcopy(H5T_C_S1)) < 0 )
    {
        syslog( LOG_ERR, "[%i] %s in %s(): Error in %s() %s",
            g_pid, "STC Error", "H5nx::write_root_metadata", "H5Tcopy",
            "Copy File Name Type" );
        give_syslog_a_chance;
        H5NXdumperr("H5nx::write_root_metadata(): H5Tcopy()"
            + std::string(" Copy File Name Type"));
        return FAIL;
    }

    if ( H5Tset_size( tid, strlen(file_name)) < 0 )
    {
        syslog( LOG_ERR, "[%i] %s in %s(): Error in %s() %s",
            g_pid, "STC Error", "H5nx::write_root_metadata", "H5Tset_size",
            "Set File Name Size" );
        give_syslog_a_chance;
        H5NXdumperr("H5nx::write_root_metadata(): H5Tset_size()"
            + std::string(" Set File Name Size ")
            + file_name);
        return FAIL;
    }

    if ( (aid = H5Acreate2( gid, "file_name", tid, sid,
            H5P_DEFAULT, H5P_DEFAULT)) < 0 )
    {
        syslog( LOG_ERR, "[%i] %s in %s(): Error in %s() %s",
            g_pid, "STC Error", "H5nx::write_root_metadata", "H5Acreate2",
            "Create File Name Attribute" );
        give_syslog_a_chance;
        H5NXdumperr("H5nx::write_root_metadata(): H5Acreate2()"
            + std::string(" Create File Name Attribute"));
        return FAIL;
    }

    if ( H5Awrite( aid, tid, (char *) file_name ) < 0 )
    {
        syslog( LOG_ERR, "[%i] %s in %s(): Error in %s() %s=%s %s",
            g_pid, "STC Error", "H5nx::write_root_metadata", "H5Awrite",
            "file_name", (char *) file_name,
            "Write File Name Attribute" );
        give_syslog_a_chance;
        H5NXdumperr("H5nx::write_root_metadata(): H5Awrite()"
            + std::string(" Write File Name Attribute ")
            + file_name);
        return FAIL;
    }

    if ( H5Tclose( tid ) < 0 )
    {
        syslog( LOG_ERR, "[%i] %s in %s(): Error in %s() %s",
            g_pid, "STC Error", "H5nx::write_root_metadata", "H5Tclose",
            "Close File Name Type" );
        give_syslog_a_chance;
        H5NXdumperr("H5nx::write_root_metadata(): H5Tclose()"
            + std::string(" Close File Name Type"));
        return FAIL;
    }

    if ( H5Sclose( sid ) < 0 )
    {
        syslog( LOG_ERR, "[%i] %s in %s(): Error in %s() %s",
            g_pid, "STC Error", "H5nx::write_root_metadata", "H5Sclose",
            "Close File Name Dataspace" );
        give_syslog_a_chance;
        H5NXdumperr("H5nx::write_root_metadata(): H5Sclose()"
            + std::string(" Close File Name Dataspace"));
        return FAIL;
    }

    if ( H5Aclose( aid ) < 0 )
    {
        syslog( LOG_ERR, "[%i] %s in %s(): Error in %s() %s",
            g_pid, "STC Error", "H5nx::write_root_metadata", "H5Aclose",
            "Close File Name Attribute" );
        give_syslog_a_chance;
        H5NXdumperr("H5nx::write_root_metadata(): H5Aclose()"
            + std::string(" Close File Name Attribute"));
        return FAIL;
    }

    // HDF5_Version

    H5get_libversion(&vers_major, &vers_minor, &vers_release);
    sprintf(version_nr, "%d.%d.%d", vers_major, vers_minor, vers_release);

    if ( (sid = H5Screate(H5S_SCALAR)) < 0 )
    {
        syslog( LOG_ERR, "[%i] %s in %s(): Error in %s() %s",
            g_pid, "STC Error", "H5nx::write_root_metadata", "H5Screate",
            "Create HDF5 Version Dataspace" );
        give_syslog_a_chance;
        H5NXdumperr("H5nx::write_root_metadata(): H5Screate()"
            + std::string(" Create HDF5 Version Dataspace"));
        return FAIL;
    }

    if ( (tid = H5Tcopy(H5T_C_S1)) < 0 )
    {
        syslog( LOG_ERR, "[%i] %s in %s(): Error in %s() %s",
            g_pid, "STC Error", "H5nx::write_root_metadata", "H5Tcopy",
            "Copy HDF5 Version Type" );
        give_syslog_a_chance;
        H5NXdumperr("H5nx::write_root_metadata(): H5Tcopy()"
            + std::string(" Copy HDF5 Version Type"));
        return FAIL;
    }

    if ( H5Tset_size( tid, strlen(version_nr)) < 0 )
    {
        syslog( LOG_ERR, "[%i] %s in %s(): Error in %s() %s",
            g_pid, "STC Error", "H5nx::write_root_metadata", "H5Tset_size",
            "Set HDF5 Version Size" );
        give_syslog_a_chance;
        H5NXdumperr("H5nx::write_root_metadata(): H5Tset_size()"
            + std::string(" Set HDF5 Version Size"));
        return FAIL;
    }

    if ( (aid = H5Acreate2( gid, "HDF5_Version", tid, sid,
            H5P_DEFAULT, H5P_DEFAULT)) < 0 )
    {
        syslog( LOG_ERR, "[%i] %s in %s(): Error in %s() %s",
            g_pid, "STC Error", "H5nx::write_root_metadata", "H5Acreate2",
            "Create HDF5 Version Attribute" );
        give_syslog_a_chance;
        H5NXdumperr("H5nx::write_root_metadata(): H5Acreate2()"
            + std::string(" Create HDF5 Version Attribute"));
        return FAIL;
    }

    if ( H5Awrite( aid, tid, (char *) version_nr ) < 0 )
    {
        syslog( LOG_ERR, "[%i] %s in %s(): Error in %s() %s=%s %s",
            g_pid, "STC Error", "H5nx::write_root_metadata", "H5Awrite",
            "version_nr", (char *) version_nr,
            "Write HDF5 Version Attribute" );
        give_syslog_a_chance;
        H5NXdumperr("H5nx::write_root_metadata(): H5Awrite()"
            + std::string(" Write HDF5 Version Attribute"));
        return FAIL;
    }

    if ( H5Tclose( tid ) < 0 )
    {
        syslog( LOG_ERR, "[%i] %s in %s(): Error in %s() %s",
            g_pid, "STC Error", "H5nx::write_root_metadata", "H5Tclose",
            "Close HDF5 Version Type" );
        give_syslog_a_chance;
        H5NXdumperr("H5nx::write_root_metadata(): H5Tclose()"
            + std::string(" Close HDF5 Version Type"));
        return FAIL;
    }

    if ( H5Sclose( sid ) < 0 )
    {
        syslog( LOG_ERR, "[%i] %s in %s(): Error in %s() %s",
            g_pid, "STC Error", "H5nx::write_root_metadata", "H5Sclose",
            "Close HDF5 Version Dataspace" );
        give_syslog_a_chance;
        H5NXdumperr("H5nx::write_root_metadata(): H5Sclose()"
            + std::string(" Close HDF5 Version Dataspace"));
        return FAIL;
    }

    if ( H5Aclose( aid ) < 0 )
    {
        syslog( LOG_ERR, "[%i] %s in %s(): Error in %s() %s",
            g_pid, "STC Error", "H5nx::write_root_metadata", "H5Aclose",
            "Close HDF5 Version Attribute" );
        give_syslog_a_chance;
        H5NXdumperr("H5nx::write_root_metadata(): H5Aclose()"
            + std::string(" Close HDF5 Version Attribute"));
        return FAIL;
    }

    time_buffer = this->format_nexus_time();

    if ( time_buffer != NULL )
    {
        if ( (sid = H5Screate(H5S_SCALAR)) < 0 )
        {
            syslog( LOG_ERR, "[%i] %s in %s(): Error in %s() %s",
                g_pid, "STC Error", "H5nx::write_root_metadata",
                "H5Screate", "Create File Time Dataspace" );
            give_syslog_a_chance;
            H5NXdumperr("H5nx::write_root_metadata(): H5Screate()"
                + std::string(" Create File Time Dataspace"));
            return FAIL;
        }

        if ( (tid = H5Tcopy(H5T_C_S1)) < 0 )
        {
            syslog( LOG_ERR, "[%i] %s in %s(): Error in %s() %s",
                g_pid, "STC Error", "H5nx::write_root_metadata",
                "H5Tcopy", "Copy File Time Type" );
            give_syslog_a_chance;
            H5NXdumperr("H5nx::write_root_metadata(): H5Tcopy()"
                + std::string(" Copy File Time Type"));
            return FAIL;
        }

        if ( H5Tset_size( tid, strlen(time_buffer)) < 0 )
        {
            syslog( LOG_ERR, "[%i] %s in %s(): Error in %s() %s",
                g_pid, "STC Error", "H5nx::write_root_metadata",
                "H5Tset_size", "Set File Time Size" );
            give_syslog_a_chance;
            H5NXdumperr("H5nx::write_root_metadata(): H5Tset_size()"
                + std::string(" Set File Time Size"));
            return FAIL;
        }

        if ( (aid = H5Acreate2( gid, "file_time", tid, sid,
                H5P_DEFAULT, H5P_DEFAULT)) < 0 )
        {
            syslog( LOG_ERR, "[%i] %s in %s(): Error in %s() %s",
                g_pid, "STC Error", "H5nx::write_root_metadata",
                "H5Acreate2", "Create File Time Attribute" );
            give_syslog_a_chance;
            H5NXdumperr("H5nx::write_root_metadata(): H5Acreate2()"
                + std::string(" Create File Time Attribute"));
            return FAIL;
        }

        if ( H5Awrite( aid, tid, time_buffer ) < 0 )
        {
            syslog( LOG_ERR, "[%i] %s in %s(): Error in %s() %s=%s %s",
                g_pid, "STC Error", "H5nx::write_root_metadata",
                "H5Awrite", "time_buffer", time_buffer,
                "Write File Time Attribute" );
            give_syslog_a_chance;
            H5NXdumperr("H5nx::write_root_metadata(): H5Awrite()"
                + std::string(" Write File Time Attribute"));
            return FAIL;
        }

        if ( H5Tclose( tid ) < 0 )
        {
            syslog( LOG_ERR, "[%i] %s in %s(): Error in %s() %s",
                g_pid, "STC Error", "H5nx::write_root_metadata",
                "H5Tclose", "Close File Time Type" );
            give_syslog_a_chance;
            H5NXdumperr("H5nx::write_root_metadata(): H5Tclose()"
                + std::string(" Close File Time Type"));
            return FAIL;
        }

        if ( H5Sclose( sid ) < 0 )
        {
            syslog( LOG_ERR, "[%i] %s in %s(): Error in %s() %s",
                g_pid, "STC Error", "H5nx::write_root_metadata",
                "H5Sclose", "Close File Time Dataspace" );
            give_syslog_a_chance;
            H5NXdumperr("H5nx::write_root_metadata(): H5Sclose()"
                + std::string(" Close File Time Dataspace"));
            return FAIL;
        }

        if ( H5Aclose( aid ) < 0 )
        {
            syslog( LOG_ERR, "[%i] %s in %s(): Error in %s() %s",
                g_pid, "STC Error", "H5nx::write_root_metadata",
                "H5Aclose", "Close File Time Attribute" );
            give_syslog_a_chance;
            H5NXdumperr("H5nx::write_root_metadata(): H5Aclose()"
                + std::string(" Close File Time Attribute"));
            return FAIL;
        }

        delete [] time_buffer;
    }

    if ( H5Gclose( gid ) < 0 )
    {
        syslog( LOG_ERR, "[%i] %s in %s(): Error in %s() %s",
            g_pid, "STC Error", "H5nx::write_root_metadata",
            "H5Gclose", "Close Root Group" );
        give_syslog_a_chance;
        H5NXdumperr("H5nx::write_root_metadata(): H5Gclose()"
            + std::string(" Close Root Group"));
        return FAIL;
    }

    return SUCCEED;
}

//////////////////////////////////////////////////////////////////////////
//format_nexus_time
//////////////////////////////////////////////////////////////////////////
char * H5nx::format_nexus_time()
{
    time_t timer;
    char* time_buffer = NULL;
    struct tm *time_info;
    const char* time_format;
    long gmt_offset = 0;

    //time_buffer = (char *)malloc(64*sizeof(char));
    time_buffer =  new char[64*sizeof(char)];
    time(&timer);

    time_info = gmtime(&timer);
    if ( time_info != NULL )
    {
        gmt_offset = (long) difftime( timer, mktime(time_info) );
    }

    time_info = localtime( &timer );
    if ( time_info != NULL )
    {
        if ( gmt_offset < 0 )
        {
            time_format = "%04d-%02d-%02dT%02d:%02d:%02d-%02d:%02d";
        }
        else
        {
            time_format = "%04d-%02d-%02dT%02d:%02d:%02d+%02d:%02d";
        }
        sprintf(time_buffer, time_format,
            1900 + time_info->tm_year,
            1 + time_info->tm_mon,
            time_info->tm_mday,
            time_info->tm_hour,
            time_info->tm_min,
            time_info->tm_sec,
            abs(gmt_offset / 3600),
            abs((gmt_offset % 3600) / 60)
            );
    }
    else
    {
        strcpy(time_buffer, "1970-01-01T00:00:00+00:00");
    }
    return time_buffer;
}

//////////////////////////////////////////////////////////////////////////
//H5NXmake_link
//////////////////////////////////////////////////////////////////////////
int H5nx::H5NXmake_link( const std::string &current_name,
        const std::string &destination_name )
{
    if ( H5Lcreate_hard( this->m_fid, current_name.c_str(),
            H5L_SAME_LOC, destination_name.c_str(),
            H5P_DEFAULT, H5P_DEFAULT) < 0 )
    {
        syslog( LOG_ERR, "[%i] %s in %s(): Error in %s() %s",
            g_pid, "STC Error", "H5nx::H5NXmake_link",
            "H5Lcreate_hard", "Create Hard Dataset Link" );
        give_syslog_a_chance;
        H5NXdumperr("H5nx::H5NXmake_link(): H5Lcreate_hard()"
            + std::string(" Create Hard Dataset Link ")
            + current_name + std::string(" -> ")
            + destination_name);
        return FAIL;
    }

    //target attribute
    char name[] = "target";

    hid_t did;
    hid_t sid;
    hid_t tid;
    hid_t aid;

    //open dataset
    if ( (did = H5Dopen2( this->m_fid, current_name.c_str(),
            H5P_DEFAULT )) < 0 )
    {
        syslog( LOG_ERR, "[%i] %s in %s(): Error in %s() %s",
            g_pid, "STC Error", "H5nx::H5NXmake_link",
            "H5Dopen2", "Open Linked Dataset" );
        give_syslog_a_chance;
        H5NXdumperr("H5nx::H5NXmake_link(): H5Dopen2()"
            + std::string(" Open Linked Dataset ")
            + current_name);
        return FAIL;
    }

    // disable error reporting
    H5E_BEGIN_TRY
    {
        hid_t already_exists =
            H5Aopen_by_name( did, ".", name, H5P_DEFAULT, H5P_DEFAULT );

        if ( already_exists > 0 )
        {
            if ( H5Aclose( already_exists ) < 0 )
            {
                syslog( LOG_ERR, "[%i] %s in %s(): Error in %s() %s",
                    g_pid, "STC Error", "H5nx::H5NXmake_link",
                    "H5Aclose", "Close Existing Linked Target Attribute" );
                give_syslog_a_chance;
                return FAIL;
            }

            if ( H5Dclose( did ) < 0 )
            {
                syslog( LOG_ERR, "[%i] %s in %s(): Error in %s() %s",
                    g_pid, "STC Error", "H5nx::H5NXmake_link",
                    "H5Aclose", "Close Existing Linked Dataset" );
                give_syslog_a_chance;
                return FAIL;
            }

            //return if already_exists

            return SUCCEED;
        }
        // enable error reporting
    } H5E_END_TRY;

    //create a scalar dataspace
    if ( (sid = H5Screate( H5S_SCALAR )) < 0 )
    {
        syslog( LOG_ERR, "[%i] %s in %s(): Error in %s() %s",
            g_pid, "STC Error", "H5nx::H5NXmake_link",
            "H5Screate", "Create Linked Target Dataspace" );
        give_syslog_a_chance;
        H5NXdumperr("H5nx::H5NXmake_link(): H5Screate()"
            + std::string(" Create Linked Target Dataspace"));
        return FAIL;
    }

    //make a copy of H5T_C_S1
    if ( (tid = H5Tcopy( H5T_C_S1 )) < 0 )
    {
        syslog( LOG_ERR, "[%i] %s in %s(): Error in %s() %s",
            g_pid, "STC Error", "H5nx::H5NXmake_link",
            "H5Tcopy", "Copy Linked Target Type" );
        give_syslog_a_chance;
        H5NXdumperr("H5nx::H5NXmake_link(): H5Tcopy()"
            + std::string(" Copy Linked Target Type"));
        return FAIL;
    }

    //set size of string
    if ( H5Tset_size( tid, strlen( current_name.c_str() )) < 0 )
    {
        syslog( LOG_ERR, "[%i] %s in %s(): Error in %s() %s",
            g_pid, "STC Error", "H5nx::H5NXmake_link",
            "H5Tset_size", "Set Linked Target Size" );
        give_syslog_a_chance;
        H5NXdumperr("H5nx::H5NXmake_link(): H5Tset_size()"
            + std::string(" Set Linked Target Size"));
        return FAIL;
    }

    //create
    if ( (aid = H5Acreate( did, name, tid, sid,
            H5P_DEFAULT, H5P_DEFAULT )) < 0 )
    {
        syslog( LOG_ERR, "[%i] %s in %s(): Error in %s() %s",
            g_pid, "STC Error", "H5nx::H5NXmake_link",
            "H5Acreate", "Create Linked Target Attribute" );
        give_syslog_a_chance;
        H5NXdumperr("H5nx::H5NXmake_link(): H5Acreate()"
            + std::string(" Create Linked Target Attribute"));
        return FAIL;
    }

    //write
    if ( H5Awrite( aid, tid, current_name.c_str() ) < 0 )
    {
        syslog( LOG_ERR, "[%i] %s in %s(): Error in %s() %s=%s %s",
            g_pid, "STC Error", "H5nx::H5NXmake_link",
            "H5Awrite", "current_name", current_name.c_str(),
            "Write Linked Target Attribute" );
        give_syslog_a_chance;
        H5NXdumperr("H5nx::H5NXmake_link(): H5Awrite()"
            + std::string(" Write Linked Target Attribute ")
            + current_name);
        return FAIL;
    }

    //close all
    if ( H5Tclose( tid ) < 0 )
    {
        syslog( LOG_ERR, "[%i] %s in %s(): Error in %s() %s",
            g_pid, "STC Error", "H5nx::H5NXmake_link",
            "H5Tclose", "Close Linked Target Type" );
        give_syslog_a_chance;
        H5NXdumperr("H5nx::H5NXmake_link(): H5Tclose()"
            + std::string(" Close Linked Target Type"));
        return FAIL;
    }

    if ( H5Sclose( sid ) < 0 )
    {
        syslog( LOG_ERR, "[%i] %s in %s(): Error in %s() %s",
            g_pid, "STC Error", "H5nx::H5NXmake_link",
            "H5Sclose", "Close Linked Target Dataspace" );
        give_syslog_a_chance;
        H5NXdumperr("H5nx::H5NXmake_link(): H5Sclose()"
            + std::string(" Close Linked Target Dataspace"));
        return FAIL;
    }

    if ( H5Aclose( aid ) < 0 )
    {
        syslog( LOG_ERR, "[%i] %s in %s(): Error in %s() %s",
            g_pid, "STC Error", "H5nx::H5NXmake_link",
            "H5Aclose", "Close Linked Target Attribute" );
        give_syslog_a_chance;
        H5NXdumperr("H5nx::H5NXmake_link(): H5Aclose()"
            + std::string(" Close Linked Target Attribute"));
        return FAIL;
    }

    if ( H5Dclose( did ) < 0 )
    {
        syslog( LOG_ERR, "[%i] %s in %s(): Error in %s() %s",
            g_pid, "STC Error", "H5nx::H5NXmake_link",
            "H5Dclose", "Close Linked Dataset" );
        give_syslog_a_chance;
        H5NXdumperr("H5nx::H5NXmake_link(): H5Dclose()"
            + std::string(" Close Linked Dataset"));
        return FAIL;
    }

    return SUCCEED;
}

//////////////////////////////////////////////////////////////////////////
//H5NXmake_group_link
//////////////////////////////////////////////////////////////////////////
int H5nx::H5NXmake_group_link( const std::string &current_name,
        const std::string &destination_name )
{

    if ( H5Lcreate_hard( this->m_fid, current_name.c_str(),
            H5L_SAME_LOC, destination_name.c_str(),
            H5P_DEFAULT, H5P_DEFAULT ) < 0 )
    {
        syslog( LOG_ERR, "[%i] %s in %s(): Error in %s() %s",
            g_pid, "STC Error", "H5nx::H5NXmake_group_link",
            "H5Lcreate_hard", "Create Hard Group Link" );
        give_syslog_a_chance;
        H5NXdumperr("H5nx::H5NXmake_group_link(): H5Lcreate_hard()"
            + std::string(" Create Hard Group Link ")
            + current_name + std::string(" -> ")
            + destination_name);
        return FAIL;
    }

    // Don't Do the Target Attribute for Groups...
    // (Create a scalar "target" string instead, allows multiple links...)

    return SUCCEED;
}

//////////////////////////////////////////////////////////////////////////
//H5NXmove_link (a.k.a. "Rename A Link/Dataset"...)
//////////////////////////////////////////////////////////////////////////
int H5nx::H5NXmove_link( const std::string &current_name,
        const std::string &destination_name )
{
    if ( H5Lmove( this->m_fid, current_name.c_str(),
            H5L_SAME_LOC, destination_name.c_str(),
            H5P_DEFAULT, H5P_DEFAULT ) < 0 )
    {
        syslog( LOG_ERR, "[%i] %s in %s(): Error in %s() %s",
            g_pid, "STC Error", "H5nx::H5NXmove_link",
            "H5Lmove", "Rename A Link/Dataset" );
        give_syslog_a_chance;
        H5NXdumperr("H5nx::H5NXmove_link(): H5Lmove()"
            + std::string(" Rename A Link/Dataset ")
            + current_name + std::string(" -> ")
            + destination_name);
        return FAIL;
    }

    return SUCCEED;
}


//////////////////////////////////////////////////////////////////////////
//H5NXflush
//call H5Fflush: causes all buffers associated with a file to be immediately flushed to disk
//////////////////////////////////////////////////////////////////////////
int H5nx::H5NXflush()
{
    if ( H5Fflush( m_fid, H5F_SCOPE_GLOBAL) < 0 )
    {
        syslog( LOG_ERR, "[%i] %s in %s(): Error in %s() %s",
            g_pid, "STC Error", "H5nx::H5NXflush",
            "H5Fflush", "Flushing All File Buffers" );
        give_syslog_a_chance;
        H5NXdumperr("H5nx::H5NXflush(): H5Fflush()"
            + std::string(" Flushing All File Buffers"));
        return FAIL;
    }

    return SUCCEED;
}

//////////////////////////////////////////////////////////////////////////
//H5NXdumperr
//call H5Eprint: dumps most recent HDF5 error stack to temporary file
//////////////////////////////////////////////////////////////////////////
void H5nx::H5NXdumperr( std::string msg )
{
    // Make the Temporary File Path Template...
    std::stringstream ss;
    ss << "/tmp/";
    ss << "STC.hdf5.err.";
    ss << g_pid;
    ss << ".XXXXXX";

    // Make a Copy of the Path String for mkstemp() Munging...
    char *path = strdup( ss.str().c_str() );
    if ( path == NULL )
    {
        syslog( LOG_ERR,
            "[%i] %s in %s(): Error copying temporary path string [%s]",
            g_pid, "STC Error", "H5nx::H5NXdumperr", ss.str().c_str() );
        give_syslog_a_chance;
        return;
    }

    // Generate the Unique Temporary File Path and Open File...
    int fd = mkstemp( path );
    if ( fd < 0 )
    {
        syslog( LOG_ERR,
            "[%i] %s in %s(): Error creating temporary file path=%s",
            g_pid, "STC Error", "H5nx::H5NXdumperr", path );
        give_syslog_a_chance;
        free( path );
        return;
    }

    // Get a Regular File Pointer...
    FILE *fp = fdopen( fd, "w" );
    if ( fp == NULL )
    {
        syslog( LOG_ERR,
            "[%i] %s in %s(): Error opening temporary file fd=%d path=%s",
            g_pid, "STC Error", "H5nx::H5NXdumperr", fd, path );
        give_syslog_a_chance;
        close( fd );
        free( path );
        return;
    }

    // Spew Caller Message into Temporary File...
    int rc = fprintf( fp,
        "[%i] %s(): Dumping HDF5 Error Stack for:\n   %s\n",
        g_pid, "H5nx::H5NXdumperr", msg.c_str() );
    if ( rc <= 0 )
    {
        syslog( LOG_ERR,
        "[%i] %s in %s(): Error %s [%s] to Temporary File (path=%s)",
            g_pid, "STC Error", "H5nx::H5NXdumperr",
            "Spewing Caller Message", msg.c_str(), path );
        give_syslog_a_chance;
        fclose( fp );
        free( path );
        return;
    }

    // Try to Dump the HDF5 Error Stack...
    if ( H5Eprint( H5E_DEFAULT, fp ) < 0 )
    {
        syslog( LOG_ERR, "[%i] %s in %s(): Error in %s() %s (path=%s)",
            g_pid, "STC Error", "H5nx::H5NXdumperr", "H5Eprint",
            "Obtaining HDF5 Error Stack", path );
        give_syslog_a_chance;
        fclose( fp );
        free( path );
        return;
    }

    // Spew Final Message into Temporary File...
    rc = fprintf( fp, "[%i] %s(): After (Any) HDF5 Error Stack.\n",
        g_pid, "H5nx::H5NXdumperr" );
    if ( rc <= 0 )
    {
        syslog( LOG_ERR,
            "[%i] %s in %s(): Error %s to Temporary File (path=%s)",
            g_pid, "STC Error", "H5nx::H5NXdumperr",
            "Spewing Final Message", path );
        give_syslog_a_chance;
        fclose( fp );
        free( path );
        return;
    }

    // Done, Flush and Close Temporary File...
    syslog( LOG_ERR,
        "[%i] %s - %s(): %s for [%s] to Temporary File (path=%s)",
        g_pid, "STC Error", "H5nx::H5NXdumperr",
        "Logged HDF5 Error Stack", msg.c_str(), path );
    give_syslog_a_chance;
    fflush( fp );
    fclose( fp );
    free( path );
    return;
}

// vim: expandtab

