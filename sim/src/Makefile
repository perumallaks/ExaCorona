#------------------------------------------------------------------------------
# Author: Kalyan S. Perumalla
#------------------------------------------------------------------------------

#------------------------------------------------------------------------------
BINDIR = ../bin
EXACORONA = $(BINDIR)/exacorona
MUSIKDIR = ./musik
#CXX = mpiCC
CXX = mpic++
CC = mpicc

#------------------------------------------------------------------------------
# If compiler not already accepting C++11:
#     * MacOS: Add -std=c++11
#     * Summit: Add -std=gnu++11
#------------------------------------------------------------------------------
CFLAGS  = -DMPI_AVAILABLE=1 -I$(MUSIKDIR) -I$(MUSIKDIR)/libsynk
LDLIBS  = -L$(MUSIKDIR) -L$(MUSIKDIR)/libsynk -lmusik -lsynk
LDFLAGS = $(LDLIBS)

#------------------------------------------------------------------------------
all: $(EXACORONA)

clean:
	rm -f $(EXACORONA) *.o
	cd musik; $(MAKE) clean

#------------------------------------------------------------------------------
$(EXACORONA): $(BINDIR) exacorona.cpp $(MUSIKDIR)/libmusik.a
	$(CXX) -o $(EXACORONA) $(CFLAGS) exacorona.cpp $(LDFLAGS)

$(BINDIR):
	mkdir $(BINDIR)

$(MUSIKDIR)/libmusik.a:
	cd $(MUSIKDIR); $(MAKE)

#------------------------------------------------------------------------------
