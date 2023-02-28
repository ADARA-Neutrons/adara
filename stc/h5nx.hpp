/**
 * @file h5nx.hpp
 *
 * @nNeXus writer interface for HD5 and NeXus
 *
 * @author pedro.vicente@space-research.org
 */

#ifndef _H5NXS_HPP
#define _H5NXS_HPP 1

#include <map>
#include <string>
#include <vector>
#include <stdint.h>
#include "napi.h"
#include "hdf5.h"
#include <nexus/NeXusFile.hpp>

#include "stcdefs.h"

//HDF5 return codes
#define SUCCEED     0
#define FAIL        (-1)


struct H5NXwrite_context
{
    hid_t   did;
    hid_t   tid;
    hid_t   fsid;
    hid_t   msid;
    hsize_t dims[H5S_MAX_RANK];
    hsize_t dims_curr[H5S_MAX_RANK];
    hsize_t count[H5S_MAX_RANK];
    hsize_t start[H5S_MAX_RANK];
};

/////////////////////////////////////////////////////////////////////////////////////////////////////
//H5nx
/////////////////////////////////////////////////////////////////////////////////////////////////////
class H5nx
{

public:

    //constructor
    H5nx( unsigned short a_compression_level = 0 );

    //create the file
    int H5NXcreate_file( const std::string &file_name );

    //open an existing file
    int H5NXopen_file( const std::string &file_name );

    //close the file 
    int H5NXclose_file();

    //create a group
    int H5NXmake_group( const std::string &group_name,
        const std::string &class_name );

    //create/write a STRING attribute
    int H5NXmake_attribute_string( const std::string &dataset_path,
        const std::string &attr_name, const std::string &attr_value  );

    //create/write a STRING attribute IFF doesn't already exist
    int H5NXcheck_attribute_string( const std::string &dataset_path,
        const std::string &attr_name, const std::string &attr_value,
        std::string &existing_attr_value, bool &wasSet );

    //create/write a STRING dataset
    int H5NXmake_dataset_string( const std::string &group_path,
        const std::string &dataset_name, const std::string &data );

    //check for existence of dataset
    int H5NXcheck_dataset_path( const std::string &group_path,
        const std::string &dataset_name, bool &exists );

    //create/write a SCALAR NUMERICAL attribute
    template <typename NumT>
    int H5NXmake_attribute_scalar( const std::string &dataset_path,
        const std::string &attr_name, const NumT &value );
    template <typename NumT>
    int H5NXwrite_attribute_scalar( const std::string &dataset_path,
        const std::string &attr_name, const NumT &value );
    template <typename NumT>
    int H5NXread_attribute_scalar( const std::string &dataset_path,
        const std::string &attr_name, NumT &value );

    //create/write a SCALAR NUMERICAL dataset
    template <typename NumT>
    int H5NXmake_dataset_scalar( const std::string &group_path,
        const std::string &dataset_name, const NumT &value );
    template <typename NumT>
    int H5NXwrite_dataset_scalar( const std::string &dataset_path,
        NumT &value );
    template <typename NumT>
    int H5NXread_dataset_scalar( const std::string &dataset_path,
        NumT &value );

    //create/write a VECTOR NUMERICAL dataset
    template <typename NumT>
    int H5NXmake_dataset_vector( const std::string &group_path,
        const std::string &dataset_name,
        const std::vector<NumT> &vec, 
        int rank, 
        const std::vector< hsize_t> &dim_vec);
    int H5NXmake_dataset_vector( const std::string &group_path,
        const std::string &dataset_name,
        const std::vector<std::string> &vec,
        int rank,
        const std::vector<hsize_t> &dim_vec );

    //create an extendable dataset

    /////////////////////////////////////////////
    // WARNING
    // this is not a template function
    // the NeXus datatype must be supplied and match the write_slab function
    ////////////////////////////////////////////

    int H5NXcreate_dataset_extend( const std::string &group_path,
                                   const std::string &dataset_name,
                                   int nxdatatype,
                                   hsize_t chunk );

    int H5NXget_dataset_dims( const std::string &dataset_path,
        int &rank, std::vector<hsize_t> &dim_vec );
    hsize_t H5NXget_vector_size( int rank, std::vector<hsize_t> &dim_vec );

    /////////////////////////////////////////////
    // WARNING
    // THIS IS FOR A 1D CASE
    ////////////////////////////////////////////
    template <typename NumT>
    int H5NXwrite_slab( const std::string &dataset_path,
        const std::vector<NumT> &slab, uint64_t slab_size,
        uint64_t cur_size );
    template <typename NumT>
    int H5NXread_slab( const std::string &dataset_path,
        std::vector<NumT> &slab, uint64_t slab_size,
        uint64_t slab_offset );

    ////////////////////////////////////////////
    int H5NXmake_link( const std::string &current_name,
        const std::string &destination_name );
    int H5NXmake_group_link( const std::string &current_name,
        const std::string &destination_name );
    int H5NXmove_link( const std::string &current_name,
        const std::string &destination_name );
        // (a.k.a. "Rename A Link/Dataset"...)

    //call H5Fflush: causes all buffers associated with a file
    //to be immediately flushed to disk
    int H5NXflush();

    //set cache size
    void H5NXset_cache_size( size_t size );

private:

    //HDF5 file handle
    hid_t m_fid;

    //NeXus to HDF5 datatype
    hid_t nx_to_hdf5_type( int nx_datatype );

    //write NeXus root group metadata
    int write_root_metadata( const char *file_name );

    //format time same as NeXus: based on "NXIformatNeXusTime"
    char *format_nexus_time();

    //modify the default chunk cache
    bool modify_chunk_cache;

    //chunk cache size
    size_t m_cache_size;

    //file access property list to modify default chunk cache (and possibly other things)
    hid_t m_fapl;

    unsigned short m_compression_level;

    //Dump HDF5 Error Stack...
    void H5NXdumperr(std::string msg);
};

#endif
