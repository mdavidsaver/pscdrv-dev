Building from Source
====================

Begin be fetching all needed source. ::

    git clone https://github.com/mdavidsaver/pscdrv.git
    git clone --branch 7.0 --recursive https://github.com/epics-base/epics-base.git

Prepare the PSCDRV source tree: ::

    cat <<EOF > pscdrv/configure/RELEASE.local
    EPICS_BASE=\$(TOP)/../epics-base
    EOF

Optional pscSig (FFT) support.  Requires the FFTW3 library (eg. 'apt-get install libfftw3-dev') ::

    cat <<EOF > pscdrv/configure/CONFIG_SITE.local
    USE_FFTW=YES
    EOF

Build: ::

    make -C epics-base
    make -C pscdrv

.. _includinginioc:

Including pscdrv in your IOC
----------------------------

Including PSCDRV in an application/IOC using the EPICS Makefiles is straightforward.
For example, starting with the makeBaseApp template from EPICS Base. ::

    makeBaseApp.pl -t ioc myioc
    makeBaseApp.pl -t ioc -i -p myioc demo

Add PSCDRV to the application configure/RELEASE or RELEASE.local file. ::

    cat <<EOF >> configure/RELEASE.local
    PSCDRV=/path/to/your/build/of/pscdrv
    EPICS_BASE=/path/to/your/build/of/epics-base
    EOF

Then add the pscdrv (and optionally pscSig) libraries as a dependencies to your IOC or support module. eg. ::

    PROD_IOC += myioc
    DBD += myioc.dbd
    
    myioc_DBD += pscCore.dbd
    
    ifneq (YES,$(USE_FFTW))
    myioc_DBD += pscSig.dbd
    myioc_LIBS += pscSig
    endif
    
    ifneq (YES,$(USE_UDPFAST))
    myioc_DBD += pscSig.dbd
    myioc_LIBS += pscSig
    endif
    
    myioc_LIBS += pscCore
    myioc_LIBS += $(EPICS_BASE_IOC_LIBS)

    myioc_SYS_LIBS += event_core event_extra
    myioc_SYS_LIBS_Linux += event_pthreads

Connecting to a device
----------------------

Add a "createPSC()" call to the IOC init script. ::

    createPSC("dev1", "localhost", 8765, 1)

Where "dev1" is an IOC internal instance name which will also be used in INP/OUT links.
This device will connect to localhost at port 8765.
The final '1' enables RX inactivity timeout, which is almost always desirable.

eg. a most complete example. ::

    #!../../bin/linux-x86_64-debug/myioc
    dbLoadDatabase("../../dbd/myioc.dbd",0,0)
    myioc_registerRecordDeviceDriver(pdbbase)

    createPSC("dev1", "localhost", 8765, 1)
    
    # Load record for device status/control
    dbLoadRecords("../../db/psc-ctrl.db","NAME=dev1,P=DEV1:")
    
    iocInit()

Note, when sending a register block from IOC to device, it is necessary to set
the block size ahead of time with the "setPSCSendBlockSize()" call. ::

    createPSC("dev1", "localhost", 8765, 1)
    # message 42 will have a 100 byte payload.
    # use DTYP="PSC Reg" to fill it in.
    setPSCSendBlockSize("dev1", 42, 100)
    iocInit()
