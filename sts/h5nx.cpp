/*
 *NeXus writer interface for HD5 and NeXus
 */

#include "h5nx.hpp"
#include "stdint.h"
#include <iostream>
#include <assert.h>
#include <string.h>
#include <stdlib.h>

#define H5NX_SANITY_CHECK

using namespace std;


/////////////////////////////////////////////////////////////////////////////////////////////////////
//H5nx
/////////////////////////////////////////////////////////////////////////////////////////////////////

H5nx::H5nx( unsigned short a_compression_level )
    : m_compression_level(a_compression_level)
{
    m_fid = -1;
    m_fapl = -1;
    m_cache_size = 0;
    modify_chunk_cache = false;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////
//H5NXclose_file
/////////////////////////////////////////////////////////////////////////////////////////////////////
int H5nx::H5NXclose_file()
{
    if ( modify_chunk_cache )
    {
        assert ( m_fapl != - 1 );
        if ( H5Pclose(m_fapl) < 0 )
        {
            return FAIL;
        }
        m_fapl = -1;
    }

    if ( H5Fclose(m_fid) < 0 )
    {
        return FAIL;
    }
    m_fid = -1;

    return SUCCEED;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////
//H5NXcreate_file
//create the HDF5 file
/////////////////////////////////////////////////////////////////////////////////////////////////////
void H5nx::H5NXset_cache_size( size_t size )
{
    m_cache_size = size;
    modify_chunk_cache = true;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////
//H5NXcreate_file
//create the HDF5 file
/////////////////////////////////////////////////////////////////////////////////////////////////////
int H5nx::H5NXcreate_file(const std::string &file_name )
{
    if ( modify_chunk_cache )
    {

        assert ( m_cache_size );

        int     mdc_nelmts;
        size_t  rdcc_nelmts;
        size_t  rdcc_nbytes;
        double  rdcc_w0;

        if (( m_fapl = H5Pcreate( H5P_FILE_ACCESS )) < 0 )
            return FAIL;
        //same as NeXus
        if ( H5Pset_fclose_degree( m_fapl, H5F_CLOSE_STRONG)  < 0 )
            return FAIL;
        if ( H5Pget_cache(m_fapl, &mdc_nelmts, &rdcc_nelmts, &rdcc_nbytes, &rdcc_w0) < 0 )
            return FAIL;

        rdcc_nbytes = m_cache_size;
        rdcc_nelmts = rdcc_nelmts * 100;

        if ( H5Pset_cache(m_fapl, mdc_nelmts, rdcc_nelmts, rdcc_nbytes, rdcc_w0) < 0 )
            return FAIL;
        if (( m_fid = H5Fcreate( file_name.c_str(), H5F_ACC_TRUNC, H5P_DEFAULT, m_fapl )) < 0 )
            return FAIL;

#if defined (H5NX_SANITY_CHECK)
        if ( H5Pget_cache(m_fapl, &mdc_nelmts, &rdcc_nelmts, &rdcc_nbytes, &rdcc_w0) < 0 )
            return FAIL;
        assert( rdcc_nbytes == m_cache_size );
#endif
    }

    else

    {

        if (( m_fid = H5Fcreate( file_name.c_str(), H5F_ACC_TRUNC, H5P_DEFAULT, H5P_DEFAULT )) < 0 )
            return FAIL;

    }

    if ( this->write_root_metadata( file_name.c_str() ) < 0)
        return FAIL;

    return SUCCEED;
}



/////////////////////////////////////////////////////////////////////////////////////////////////////
//H5NXmake_group
/////////////////////////////////////////////////////////////////////////////////////////////////////
int H5nx::H5NXmake_group( const std::string &group_name, const std::string &class_name )
{
    hid_t     gid;
    hid_t     aid;
    hid_t     tid;
    hid_t     sid;

    if (( gid = H5Gcreate2( this->m_fid, group_name.c_str(), H5P_DEFAULT, H5P_DEFAULT, H5P_DEFAULT)) < 0)
    {
        return FAIL;
    }

    if (( sid = H5Screate(H5S_SCALAR)) < 0)
    {
        return FAIL;
    }

    if (( tid = H5Tcopy(H5T_C_S1)) < 0)
    {
        return FAIL;
    }

    if ( H5Tset_size(tid, strlen(class_name.c_str())) < 0)
    {
        return FAIL;
    }

    if (( aid= H5Acreate2(gid, "NX_class", tid, sid, H5P_DEFAULT, H5P_DEFAULT)) < 0)
    {
        return FAIL;
    }

    if ( H5Awrite( aid, tid, class_name.c_str()) < 0)
    {
        return FAIL;
    }

    if ( H5Tclose( tid ) < 0 )
    {
        return FAIL;
    }

    if ( H5Sclose( sid ) < 0 )
    {
        return FAIL;
    }

    if ( H5Aclose( aid ) < 0 )
    {
        return FAIL;
    }

    if ( H5Gclose( gid ) < 0 )
    {
        return FAIL;
    }
    return SUCCEED;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////
//nx_to_hdf5_type
/////////////////////////////////////////////////////////////////////////////////////////////////////
hid_t H5nx::nx_to_hdf5_type(int nx_datatype)
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
        type = -1;
        assert ( 0 );
    }
    return type;
}

///////////////////////////////////////////////////////////////////
// H5NXmake_attribute_string
////////////////////////////////////////////////////////////////////
int H5nx::H5NXmake_attribute_string( const std::string &dataset_path, const std::string &attr_name, const std::string &attr_value  )
{
    hid_t   aid;  // attribute ID
    hid_t   sid;  // dataspace ID
    hid_t   tid; // nx_datatype ID
    hid_t   did;  // dataset ID

    //lenght of data
    size_t size_attr = attr_value.size();

    //create a scalar dataspace
    if (( sid = H5Screate( H5S_SCALAR )) < 0)
    {
        return FAIL;
    }

    //copy type
    if (( tid = H5Tcopy(H5T_C_S1)) < 0)
    {
        return FAIL;
    }

    if ( H5Tset_size( tid, size_attr ) < 0)
    {
        return FAIL;
    }

    if (H5Tset_strpad(tid, H5T_STR_NULLTERM) < 0)
    {
        return FAIL;
    }

    if (( did = H5Dopen2( this->m_fid, dataset_path.c_str(), H5P_DEFAULT)) < 0)
    {
        return FAIL;
    }

    if (( aid = H5Acreate( did, attr_name.c_str(), tid, sid, H5P_DEFAULT, H5P_DEFAULT)) < 0)
    {
        return FAIL;
    }

    if ( H5Awrite( aid, tid , &(attr_value[0]) ) < 0 )
    {
        return FAIL;
    }

    if ( H5Tclose( tid ) < 0 )
    {
        return FAIL;
    }

    if ( H5Sclose( sid ) < 0 )
    {
        return FAIL;
    }

    if ( H5Aclose( aid ) < 0 )
    {
        return FAIL;
    }

    if ( H5Dclose( did ) < 0 )
    {
        return FAIL;
    }


    return SUCCEED;
}

///////////////////////////////////////////////////////////////////
// H5NXmake_dataset_string
////////////////////////////////////////////////////////////////////
int H5nx::H5NXmake_dataset_string(const std::string &group_path, const std::string &dataset_name, const std::string &data )
{

    hid_t   did;  // dataset ID
    hid_t   sid;  // space ID
    hid_t   tid;  // nx_datatype ID
    hsize_t dim[1] = {1};

    std::string absolute_dataset_name = group_path + "/" + dataset_name;

    //lenght of data
    size_t size_data = data.size();

    //create a 1D dataspace with a dimension of 1 element
    if (( sid = H5Screate_simple( 1, dim, NULL )) < 0)
    {
        return FAIL;
    }

    if (( tid = H5Tcopy(H5T_C_S1)) < 0)
    {
        return FAIL;
    }

    if ( H5Tset_size( tid, size_data ) < 0)
    {
        return FAIL;
    }

    if (H5Tset_strpad(tid, H5T_STR_NULLTERM) < 0)
    {
        return FAIL;
    }

    if ((did = H5Dcreate2 ( m_fid, absolute_dataset_name.c_str(), tid, sid, H5P_DEFAULT, H5P_DEFAULT, H5P_DEFAULT )) < 0)
    {
        return FAIL;
    }

    if ( H5Dwrite( did, tid , H5S_ALL, H5S_ALL, H5P_DEFAULT, &(data[0]) ) < 0 )
    {
        return FAIL;
    }

    if ( H5Tclose( tid ) < 0 )
    {
        return FAIL;
    }

    if ( H5Sclose( sid ) < 0 )
    {
        return FAIL;
    }

    if ( H5Dclose( did ) < 0 )
    {
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
NeXus::NXnumtype to_nx_type(NumT number = NumT());

template<>
NeXus::NXnumtype to_nx_type( uint16_t number)
{
    return NeXus::UINT16;
}

template<>
NeXus::NXnumtype to_nx_type( double number)
{
    return NeXus::FLOAT64;
}

template<>
NeXus::NXnumtype to_nx_type( uint32_t  number)
{
    return NeXus::UINT32;
}

template<>
NeXus::NXnumtype to_nx_type( uint64_t  number)
{
    return NeXus::UINT64;
}

template<>
NeXus::NXnumtype to_nx_type( float  number)
{
    return NeXus::FLOAT32;
}


///////////////////////////////////////////////////////////////////
// H5NXmake_attribute_scalar
// create/write a SCALAR NUMERICAL attribute
////////////////////////////////////////////////////////////////////

// declare instantiations of the types of templated functions needed
template
int H5nx::H5NXmake_attribute_scalar( const std::string &group_path, const std::string &dataset_name, const uint16_t &value );
template
int H5nx::H5NXmake_attribute_scalar( const std::string &group_path, const std::string &dataset_name, const uint32_t &value );

template <typename NumT>
int H5nx::H5NXmake_attribute_scalar( const std::string &dataset_path, const std::string &attr_name, const NumT &value )
{
    hid_t   did;  // dataset ID
    hid_t   aid;  // attribute ID
    hid_t   sid;  // space ID
    hid_t   tid;  // nx_datatype ID
    hid_t   wtid; // nx_datatype ID

     //get the NeXus type; some template magic here
    NeXus::NXnumtype nx_numtype = to_nx_type<NumT>();

    //get the HDF5 type from the NeXus type
    tid = nx_to_hdf5_type( nx_numtype );

    //create a scalar dataspace
    if (( sid = H5Screate( H5S_SCALAR )) < 0)
    {
        return FAIL;
    }

    if (( wtid = H5Tcopy( tid )) < 0)
    {
        return FAIL;
    }

    if (( did = H5Dopen2( this->m_fid, dataset_path.c_str(), H5P_DEFAULT)) < 0)
    {
        return FAIL;
    }

    if (( aid = H5Acreate( did, attr_name.c_str(), wtid, sid, H5P_DEFAULT, H5P_DEFAULT)) < 0)
    {
        return FAIL;
    }

    if ( H5Awrite( aid, wtid , &value ) < 0 )
    {
        return FAIL;
    }

    if ( H5Tclose( wtid ) < 0 )
    {
        return FAIL;
    }

    if ( H5Sclose( sid ) < 0 )
    {
        return FAIL;
    }

    if ( H5Aclose( aid ) < 0 )
    {
        return FAIL;
    }

    if ( H5Dclose( did ) < 0 )
    {
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
int H5nx::H5NXmake_dataset_scalar( const std::string &group_path, const std::string &dataset_name, const uint16_t &value );

template
int H5nx::H5NXmake_dataset_scalar( const std::string &group_path, const std::string &dataset_name, const double &value );

template
int H5nx::H5NXmake_dataset_scalar( const std::string &group_path, const std::string &dataset_name, const float &value );

template
int H5nx::H5NXmake_dataset_scalar( const std::string &group_path, const std::string &dataset_name, const uint64_t &value );

template <typename NumT>
int H5nx::H5NXmake_dataset_scalar( const std::string &group_path, const std::string &dataset_name, const NumT &value )
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
    if (( sid = H5Screate_simple( 1, dim, NULL )) < 0)
    {
        return FAIL;
    }

    //create dataset
    if (( did = H5Dcreate2(m_fid, absolute_dataset_name.c_str(), tid, sid, H5P_DEFAULT, H5P_DEFAULT, H5P_DEFAULT )) < 0)
    {
        return FAIL;
    }

    if ( H5Dwrite( did, tid , H5S_ALL, H5S_ALL, H5P_DEFAULT, &value ) < 0 )
    {
        return FAIL;
    }

    if ( H5Sclose( sid ) < 0 )
    {
        return FAIL;
    }

    //close dataset
    if ( H5Dclose( did ) < 0 )
    {
        return FAIL;
    }
    return SUCCEED;
}

///////////////////////////////////////////////////////////////////
// H5NXmake_dataset_vector
// create/write a SCALAR NUMERICAL dataset
////////////////////////////////////////////////////////////////////

// Declare instantiations of the types of templated functions needed

template int H5nx::H5NXmake_dataset_vector(const std::string &group_path, const std::string &dataset_name,
    const std::vector<double> &vec,
    int rank,
    const std::vector< hsize_t> &dim_vec);

template int H5nx::H5NXmake_dataset_vector(const std::string &group_path, const std::string &dataset_name,
    const std::vector<uint32_t> &vec,
    int rank,
    const std::vector< hsize_t> &dim_vec);

template int H5nx::H5NXmake_dataset_vector(const std::string &group_path, const std::string &dataset_name,
    const std::vector<uint64_t> &vec,
    int rank,
    const std::vector< hsize_t> &dim_vec);

template int H5nx::H5NXmake_dataset_vector(const std::string &group_path, const std::string &dataset_name,
    const std::vector<float> &vec,
    int rank,
    const std::vector< hsize_t> &dim_vec);

template <typename NumT>
int H5nx::H5NXmake_dataset_vector( const std::string &group_path, const std::string &dataset_name, const std::vector<NumT> &vec,
    int rank,
    const std::vector< hsize_t> &dim_vec )
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

    //construct the dataset absolute path
    std::string dataset_path = group_path + "/" + dataset_name;

    // create the data space for the dataset
    if ((sid = H5Screate_simple(rank, dims, NULL)) < 0)
    {
        return FAIL;
    }

    // create the dataset
    if ( (did = H5Dcreate2( this->m_fid, dataset_path.c_str(), tid, sid, H5P_DEFAULT, H5P_DEFAULT, H5P_DEFAULT)) < 0)
    {
        return FAIL;
    }

    // write the dataset only if there is data to write
    if ( vec.size() )
    {
        if ( H5Dwrite(did, tid, H5S_ALL, H5S_ALL, H5P_DEFAULT, &(vec[0])) < 0)
        {
            return FAIL;
        }
    }

    // end access to the dataset and release resources used by it
    if ( H5Dclose(did) < 0)
    {
        return FAIL;
    }

    // terminate access to the data space
    if ( H5Sclose(sid) < 0)
    {
        return FAIL;
    }

    return SUCCEED;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////
//H5MPcreate_dataset_extend
// 1D dataset
/////////////////////////////////////////////////////////////////////////////////////////////////////
int H5nx::H5NXcreate_dataset_extend(const std::string &group_path, const std::string &dataset_name,
                                    int nxdatatype,
                                    hsize_t chunk_size)
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

    //create  dataspace
    if ((sid = H5Screate_simple( 1, dim, maxdim)) < 0)
    {
        return FAIL;
    }

    // dataset creation property list
    if ((dcpl = H5Pcreate(H5P_DATASET_CREATE)) < 0)
    {
        return FAIL;
    }

    //set chunking
    if ( H5Pset_chunk(dcpl, 1, chunk_dim) < 0)
    {
        return FAIL;
    }

    if ( m_compression_level > 0 && m_compression_level < 10 )
    {
        //set compression
        if ( H5Pset_deflate(dcpl, m_compression_level) < 0)
        {
            return FAIL;
        }

        //shuffle data byte order to aid compression
        if ( H5Pset_shuffle(dcpl) < 0)
        {
            return FAIL;
        }
    }

    tid = nx_to_hdf5_type(nxdatatype);

    //create dataset with modified dcpl
    if ((did = H5Dcreate2(m_fid, path.c_str(), tid, sid, H5P_DEFAULT, dcpl, H5P_DEFAULT )) < 0)
    {
        return FAIL;
    }

    if ( H5Pclose(dcpl) < 0 )
    {
        return FAIL;
    }

    if ( H5Dclose( did ) < 0 )
    {
        return FAIL;
    }
    return SUCCEED;
}

///////////////////////////////////////////////////////////////////
// H5NXwrite_slab
////////////////////////////////////////////////////////////////////

template
int H5nx::H5NXwrite_slab(const std::string &dataset_path, const std::vector<double> &slab, uint64_t cur_size );

template
int H5nx::H5NXwrite_slab(const std::string &dataset_path, const std::vector<float> &slab, uint64_t cur_size );

template
int H5nx::H5NXwrite_slab(const std::string &dataset_path, const std::vector<uint16_t> &slab, uint64_t cur_size );

template
int H5nx::H5NXwrite_slab(const std::string &dataset_path, const std::vector<int16_t> &slab, uint64_t cur_size );

template
int H5nx::H5NXwrite_slab(const std::string &dataset_path, const std::vector<uint32_t> &slab, uint64_t cur_size );

template
int H5nx::H5NXwrite_slab(const std::string &dataset_path, const std::vector<int32_t> &slab, uint64_t cur_size );

template
int H5nx::H5NXwrite_slab(const std::string &dataset_path, const std::vector<uint64_t> &slab, uint64_t cur_size );

template
int H5nx::H5NXwrite_slab(const std::string &dataset_path, const std::vector<int64_t> &slab, uint64_t cur_size );

template
int H5nx::H5NXwrite_slab(const std::string &dataset_path, const std::vector<char> &slab, uint64_t cur_size );

template <typename NumT>
int H5nx::H5NXwrite_slab(const std::string &dataset_path, const std::vector<NumT> &vec, uint64_t cur_size )
{
    hid_t   did;
    hid_t   tid;
    hid_t   fsid;
    hid_t   msid;
    hsize_t dims[H5S_MAX_RANK];
    //hsize_t dims_curr[H5S_MAX_RANK];
    hsize_t count[H5S_MAX_RANK];
    hsize_t start[H5S_MAX_RANK];

    ///////////////////////////////////////////////////////////////////
    // FOR 1D DATASET
    ////////////////////////////////////////////////////////////////////

    hsize_t slab_size = vec.size();

    //open dataset
    if (( did = H5Dopen2( this->m_fid, dataset_path.c_str(), H5P_DEFAULT)) < 0)
    {
        return FAIL;
    }

    //get type
    if ((tid = H5Dget_type(did)) < 0)
    {
        return FAIL;
    }

    dims[0] = cur_size + slab_size;

    if (H5Dextend(did , dims) < 0)
    {
        return FAIL;
    }

    //get the new (updated) space
    if ((fsid = H5Dget_space(did)) < 0)
    {
        return FAIL;
    }

    count[0] = slab_size;
    start[0] = cur_size;

    //select space on file
    if (H5Sselect_hyperslab(fsid, H5S_SELECT_SET, start, NULL, count, NULL) < 0)
    {
        return FAIL;
    }

    //memory space
    if ((msid = H5Screate_simple( 1, count, count)) < 0)
    {
        return FAIL;
    }

    //write
    if (H5Dwrite(did, tid, msid, fsid, H5P_DEFAULT, &(vec[0])) < 0)
    {
        return FAIL;
    }

    //close memory space
    if (H5Sclose(msid) < 0)
    {
        return FAIL;
    }

    //close file space
    if (H5Sclose(fsid) < 0)
    {
        return FAIL;
    }

    //close type
    if (H5Tclose(tid) < 0)
    {
        return FAIL;
    }

    //close dataset
    if ( H5Dclose( did ) < 0 )
    {
        return FAIL;
    }


    return SUCCEED;
}


int  H5nx::H5NXslab_open( H5NXwrite_context &context, const std::string &dataset_path )
{
    //open dataset
    if (( context.did = H5Dopen2( m_fid, dataset_path.c_str(), H5P_DEFAULT)) < 0)
    {
        return FAIL;
    }

    //get type
    if ((context.tid = H5Dget_type(context.did)) < 0)
    {
        return FAIL;
    }

    //get file space
    if ((context.fsid = H5Dget_space(context.did)) < 0)
    {
        return FAIL;
    }

    //get current dimensions
    if (H5Sget_simple_extent_dims(context.fsid , context.dims_curr, NULL) < 0)
    {
        return FAIL;
    }

    //close file space
    if (H5Sclose(context.fsid) < 0)
    {
        return FAIL;
    }
    return SUCCEED;
}

template
int H5nx::H5NXslab_write( H5NXwrite_context &context, const std::vector<double> &slab);

template
int H5nx::H5NXslab_write( H5NXwrite_context &context, const std::vector<uint32_t> &slab);

template
int H5nx::H5NXslab_write( H5NXwrite_context &context, const std::vector<float> &slab);

template
int H5nx::H5NXslab_write( H5NXwrite_context &context, const std::vector<uint64_t> &slab);

template <typename T>
int H5nx::H5NXslab_write( H5NXwrite_context &context, const std::vector<T> &data )
{
    context.dims[0] = data.size() + context.dims_curr[0];

    if (H5Dset_extent(context.did , context.dims) < 0)
    {
        return FAIL;
    }

    //get the new (updated) space
    if ((context.fsid = H5Dget_space(context.did)) < 0)
    {
        return FAIL;
    }

    context.count[0] = data.size();
    context.start[0] = context.dims_curr[0];

    //select space on file
    if (H5Sselect_hyperslab( context.fsid, H5S_SELECT_SET, context.start, NULL, context.count, NULL) < 0)
    {
        return FAIL;
    }

    //memory space
    if (( context.msid = H5Screate_simple( 1, context.count, context.count)) < 0)
    {
        return FAIL;
    }

    //write
    if (H5Dwrite( context.did, context.tid, context.msid, context.fsid, H5P_DEFAULT, &(data[0])) < 0)
    {
        return FAIL;
    }

    //close memory space
    if (H5Sclose(context.msid) < 0)
    {
        return FAIL;
    }

   // TODO This may not be a safe way to do this
   context.dims_curr[0] = context.dims[0];

   return SUCCEED;
}

int H5nx::H5NXslab_close( H5NXwrite_context &context )
{
    //close file space
    if (H5Sclose(context.fsid) < 0)
    {
        return FAIL;
    }

    //close type
    if (H5Tclose(context.tid) < 0)
    {
        return FAIL;
    }

    //close dataset
    if ( H5Dclose( context.did ) < 0 )
    {
        return FAIL;
    }
    return SUCCEED;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////
//private functions
/////////////////////////////////////////////////////////////////////////////////////////////////////


/////////////////////////////////////////////////////////////////////////////////////////////////////
//write_root_metadata
/////////////////////////////////////////////////////////////////////////////////////////////////////
int H5nx::write_root_metadata( const char *file_name )
{
    char      *time_buffer = NULL;
    char      version_nr[10];
    unsigned  int vers_major, vers_minor, vers_release;
    hid_t     gid;
    hid_t     aid;
    hid_t     tid;
    hid_t     sid;


    if (( gid = H5Gopen2( this->m_fid, "/", H5P_DEFAULT)) < 0)
    {
        return FAIL;
    }

    // NeXus_version

    if (( sid = H5Screate(H5S_SCALAR)) < 0)
    {
        return FAIL;
    }

    if (( tid = H5Tcopy(H5T_C_S1)) < 0)
    {
        return FAIL;
    }

    if ( H5Tset_size( tid, strlen(NEXUS_VERSION)) < 0)
    {
        return FAIL;
    }

    if (( aid = H5Acreate2( gid, "NeXus_version", tid, sid, H5P_DEFAULT, H5P_DEFAULT)) < 0)
    {
        return FAIL;
    }

    if ( H5Awrite( aid, tid ,NEXUS_VERSION ) < 0 )
    {
        return FAIL;
    }

    if ( H5Tclose( tid ) < 0 )
    {
        return FAIL;
    }
    if ( H5Sclose( sid ) < 0 )
    {
        return FAIL;
    }
    if ( H5Aclose( aid ) < 0 )
    {
        return FAIL;
    }


    //file_name

    if (( sid = H5Screate(H5S_SCALAR)) < 0)
    {
        return FAIL;
    }
    if (( tid = H5Tcopy(H5T_C_S1)) < 0)
    {
        return FAIL;
    }
    if ( H5Tset_size( tid, strlen(file_name)) < 0)
    {
        return FAIL;
    }
    if (( aid = H5Acreate2( gid, "file_name", tid, sid, H5P_DEFAULT, H5P_DEFAULT)) < 0)
    {
        return FAIL;
    }
    if ( H5Awrite( aid, tid , (char*)file_name ) < 0 )
    {
        return FAIL;
    }
    if ( H5Tclose( tid ) < 0 )
    {
        return FAIL;
    }
    if ( H5Sclose( sid ) < 0 )
    {
        return FAIL;
    }
    if ( H5Aclose( aid ) < 0 )
    {
        return FAIL;
    }

    // HDF5_Version

    H5get_libversion(&vers_major, &vers_minor, &vers_release);
    sprintf (version_nr, "%d.%d.%d", vers_major,vers_minor,vers_release);

    if (( sid = H5Screate(H5S_SCALAR)) < 0)
    {
        return FAIL;
    }
    if (( tid = H5Tcopy(H5T_C_S1)) < 0)
    {
        return FAIL;
    }
    if ( H5Tset_size( tid, strlen(version_nr)) < 0)
    {
        return FAIL;
    }
    if (( aid = H5Acreate2( gid, "HDF5_Version", tid, sid, H5P_DEFAULT, H5P_DEFAULT)) < 0)
    {
        return FAIL;
    }
    if ( H5Awrite( aid, tid , (char*)version_nr ) < 0 )
    {
        return FAIL;
    }
    if ( H5Tclose( tid ) < 0 )
    {
        return FAIL;
    }
    if ( H5Sclose( sid ) < 0 )
    {
        return FAIL;
    }
    if ( H5Aclose( aid ) < 0 )
    {
        return FAIL;
    }

    time_buffer = this->format_nexus_time();


    if(time_buffer != NULL)
    {
        if (( sid = H5Screate(H5S_SCALAR)) < 0)
        {
            return FAIL;
        }
        if (( tid = H5Tcopy(H5T_C_S1)) < 0)
        {
            return FAIL;
        }
        if ( H5Tset_size( tid, strlen(time_buffer)) < 0)
        {
            return FAIL;
        }
        if (( aid = H5Acreate2( gid, "file_time", tid, sid, H5P_DEFAULT, H5P_DEFAULT)) < 0)
        {
            return FAIL;
        }
        if ( H5Awrite( aid, tid , time_buffer ) < 0 )
        {
            return FAIL;
        }
        if ( H5Tclose( tid ) < 0 )
        {
            return FAIL;
        }
        if ( H5Sclose( sid ) < 0 )
        {
            return FAIL;
        }
        if ( H5Aclose( aid ) < 0 )
        {
            return FAIL;
        }

        delete [] time_buffer;
    }

    if ( H5Gclose( gid ) < 0 )
    {
        return FAIL;
    }

    return SUCCEED;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////
//format_nexus_time
/////////////////////////////////////////////////////////////////////////////////////////////////////
char * H5nx::format_nexus_time()
{
    time_t timer;
    char* time_buffer = NULL;
    struct tm *time_info;
    const char* time_format;
    long gmt_offset = 0;


//    time_buffer = (char *)malloc(64*sizeof(char));
    time_buffer =  new char[64*sizeof(char)];
    time(&timer);

    time_info = gmtime(&timer);
    if (time_info != NULL)
    {
        gmt_offset = (long)difftime(timer, mktime(time_info));
    }

    time_info = localtime(&timer);
    if (time_info != NULL)
    {
        if (gmt_offset < 0)
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


/////////////////////////////////////////////////////////////////////////////////////////////////////
//H5NXmake_link
/////////////////////////////////////////////////////////////////////////////////////////////////////
int H5nx::H5NXmake_link(const std::string &current_name, const std::string &destination_name )
{


    if ( H5Lcreate_hard( this->m_fid, current_name.c_str(), H5L_SAME_LOC, destination_name.c_str(), H5P_DEFAULT, H5P_DEFAULT) < 0 )
    {
        return FAIL;
    }

    /////////////////////////////////////////////////////////////////////////////////////////////////////
    // target attribute
    /////////////////////////////////////////////////////////////////////////////////////////////////////

    char name[] = "target";
    hid_t did;
    hid_t sid;
    hid_t tid;
    hid_t aid;


    //open dataset
    if (( did = H5Dopen2( this->m_fid, current_name.c_str(), H5P_DEFAULT)) < 0)
    {
        return FAIL;
    }

    // disable error reporting
    H5E_BEGIN_TRY
    {
        hid_t already_exists = H5Aopen_by_name( did, ".", name, H5P_DEFAULT, H5P_DEFAULT);
        if( already_exists > 0 )
        {
            if ( H5Aclose( already_exists ) < 0 )
            {
                return FAIL;
            }
            if ( H5Dclose( did ) < 0 )
            {
                return FAIL;
            }

            //return if already_exists

            return SUCCEED;
        }
        // enable error reporting
    } H5E_END_TRY;

    //create a scalar dataspace
    if (( sid = H5Screate( H5S_SCALAR )) < 0)
    {
        return FAIL;
    }

    //make a copy of H5T_C_S1
    if (( tid = H5Tcopy( H5T_C_S1 )) < 0)
    {
        return FAIL;
    }

    //set size of string
    if ( H5Tset_size( tid, strlen( current_name.c_str() )) < 0)
    {
        return FAIL;
    }

    //create
    if (( aid  = H5Acreate( did, name, tid, sid, H5P_DEFAULT, H5P_DEFAULT )) < 0)
    {
        return FAIL;
    }

    //write
    if ( H5Awrite( aid, tid, current_name.c_str() ) < 0)
    {
        return FAIL;
    }

    //close all
    if ( H5Tclose( tid ) < 0 )
    {
        return FAIL;
    }

    if ( H5Sclose( sid ) < 0 )
    {
        return FAIL;
    }

    if ( H5Aclose( aid ) < 0 )
    {
        return FAIL;
    }

    if ( H5Dclose( did ) < 0 )
    {
        return FAIL;
    }

    return SUCCEED;
}


 //
/////////////////////////////////////////////////////////////////////////////////////////////////////
//H5NXflush
//call H5Fflush: causes all buffers associated with a file to be immediately flushed to disk
/////////////////////////////////////////////////////////////////////////////////////////////////////
int H5nx::H5NXflush()
{

    if ( H5Fflush( m_fid, H5F_SCOPE_GLOBAL) < 0 )
    {
        return FAIL;
    }

    return SUCCEED;
}


