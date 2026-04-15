# ADARA



This is the software system for next-generation Data Acquisition System (DAS) at the ​Spallation Neutron Source (SNS), the High Flux Isotope Reactor (HFIR) and the Second Target Station (STS) at ​Oak Ridge National Laboratory (ORNL) -- "ADARA".

ADARA stands for "Accelerating Data Acquisition, Reduction, and Analysis" and is a collaborative project between the ​SNS and ​ORNL's ​Computing and Computational Sciences Directorate (CCSD) to develop "next-generation" software and hardware components for ​SNS data acquisition. The goal of ADARA is to provide real time (streaming) data acquisition and data analysis system(s) for ​SNS that will ultimately provide the researchers using the ​SNS beam lines with effectively instant access to their reduced experimental data both during, and after, data collection.

The main ADARA software infrastructure contains the following key software components:

* ​''Stream Management Service (SMS)'' for collecting and collating neutron pulse and event data from detector system preprocessors, as well as Process Variables (PVs) from the beamline Slow Controls and Sample Environment subsystems, and producing a hybrid streaming network protocol that combines these data along with run-time experiment/run meta-data;

* ​''Streaming Translation Client (STC)'' (formerly ​''Streaming Translation Service (STS)'' prior to April 2019) receives the live data stream from the SMS and applies it to produce live ​NeXus Event files, available immediately upon completion of a given experimental run, for reduction and analysis;

* ​''Process Variable Streaming Daemon (PVSD) (formerly "PVStreamer")'' service collects the values and status of Slow Controls and Sample Environment Process Variables (PVs) and delivers them to the SMS for inclusion in the hybrid network protocol stream;

* ​''Post-Processing and Reduction'' service is notified when the STC completes the generation of the ​NeXus Event file, and proceeds to apply pre-defined data post-processing and reduction operations, customizable on a per-beamline basis, and then archives and catalogs the resulting data files. 

There is also an ​ADARA Test Harness for building and testing various ADARA software components, using historical SNS experiment data. This Test Harness can be used for Manual Testing as well as regular Integration Testing via the Jenkins Test and Build system.

## Building ADARA

ADARA:

    Requires autoconf, automake, and boost-devel, among other packages. :-)

    SMS requires log4cxx-devel (or log4cpp-devel) and EPICS
    STC requires hdf5-devel, libxml2-devel, activemq-cpp-devel, apr-devel,
        and nexus-devel (third-party) [NeXus now subsumed into STC headers]
    DASMON server requires libxml2-devel, activemq-cpp-devel, apr-devel

    To build:
        ./bootstrap
        ./configure --with-hdf5=/usr/bin/h5cc \
            --with-epics=/path/to/epics [EPICS3]
        or
            --with-epics7=/path/to/epics \
            --with-pcas=/path/to/pcas [EPICS7]

    For "Verbose" ADARA Build Output, either do:
        ./configure . . . --disable-silent-rules
        or
        make V=1

    You can disable components using --disable-NAME; for more information,
    see ./configure --help.

EPICS:

    https://epics.anl.gov/download/base/baseR3.14.12.6.tar.gz [EPICS3]

    or

    https://epics.anl.gov/download/base/base-7.0.7.tar.gz [EPICS7]
    plus
    PCAS: https://github.com/epics-modules/pcas

    Requires readline-devel and perl-ExtUtils-ParseXS, and others

    Building is basically running make in the top-level directory.
    * No need to do make install (it doesn't do what you'd expect anyway!)
