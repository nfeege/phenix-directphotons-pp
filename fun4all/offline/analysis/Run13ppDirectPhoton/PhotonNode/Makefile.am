AUTOMAKE_OPTIONS = foreign

AM_CXXFLAGS = -Wall -Werror
INCLUDES = -I$(includedir) -I$(OFFLINE_MAIN)/include -I$(ROOTSYS)/include

lib_LTLIBRARIES = \
  libPhotonNode.la

#mydatadir = $(datadir)/$(PACKAGE_INCLUDE_DIR)
#dist_mydata_DATA =

include_HEADERS =

noinst_HEADERS = \
  AnaToolsTowerID.h \
  AnaToolsPhoton.h \
  EmcLocalRecalibrator.h \
  EmcLocalRecalibratorSasha.h \
  PhotonContainerClone.h \
  FillHisto.h \
  PhotonNodeLinkDef.h

libPhotonNode_la_SOURCES = \
  EmcLocalRecalibrator.C \
  EmcLocalRecalibratorSasha.C \
  PhotonContainerClone.C \
  FillHisto.C \
  PhotonNode_Dict.C

AM_LDFLAGS = \
  -L$(libdir) \
  -L$(OFFLINE_MAIN)/lib

libPhotonNode_la_LIBADD = \
  -lTOAD \
  -lDirectPhotonPP \
  -lfun4all \
  -lfun4allfuncs \
  -lphool

BUILT_SOURCES = testexternals.C
noinst_PROGRAMS = testexternals
testexternals_LDADD = libPhotonNode.la
testexternals.C:
	echo "//*** this is a generated file. Do not commit, do not edit" > $@
	echo "int main() {return 0;}" >> $@

PhotonNode_Dict.C: \
  FillHisto.h \
  PhotonNodeLinkDef.h
	rootcint -f $@ -c $(DEFAULT_INCLUDES) $(INCLUDES) $^

CLEANFILES = *Dict* testexternals*
