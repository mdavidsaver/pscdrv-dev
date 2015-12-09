#include <psc/devcommon.h>
#include <longoutRecord.h>
#include <epicsExport.h>

MAKEDSET(longout, devLOFFTnfft, NULL, NULL, NULL);
MAKEDSET(longout, devAOFFTFSamp, NULL, NULL, NULL);
MAKEDSET(longout, devAOFFTScale, NULL, NULL, NULL);
MAKEDSET(longout, devAIFFTTotPwrTime, NULL, NULL, NULL);
MAKEDSET(longout, devAIFFTTotPwrFreq, NULL, NULL, NULL);
MAKEDSET(longout, devWFFFTInput, NULL, NULL, NULL);
MAKEDSET(longout, devWFFFTOutput, NULL, NULL, NULL);
MAKEDSET(longout, devWFFFTFScale, NULL, NULL, NULL);


epicsExportAddress(dset, devLOFFTnfft);
epicsExportAddress(dset, devAOFFTFSamp);
epicsExportAddress(dset, devAOFFTScale);
epicsExportAddress(dset, devAIFFTTotPwrTime);
epicsExportAddress(dset, devAIFFTTotPwrFreq);
epicsExportAddress(dset, devWFFFTInput);
epicsExportAddress(dset, devWFFFTOutput);
epicsExportAddress(dset, devWFFFTFScale);

