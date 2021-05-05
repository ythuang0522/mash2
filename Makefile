CXXFLAGS += -O3 -std=c++14 -Isrc -I/usr/local//include -I/bip7_disk/yuqing109/Mash/boost_1_75_0/bin.v2//include
CPPFLAGS += -DUSE_BOOST

UNAME_S=$(shell uname -s)

ifeq ($(UNAME_S),Darwin)
	CXXFLAGS += -mmacosx-version-min=10.7 -stdlib=libc++
else
	CXXFLAGS += -include src/mash/memcpyLink.h -Wl,--wrap=memcpy
	CFLAGS += -include src/mash/memcpyLink.h
endif

SOURCES=\
	src/mash/Command.cpp \
	src/mash/CommandBounds.cpp \
	src/mash/CommandContain.cpp \
	src/mash/CommandDistance.cpp \
	src/mash/CommandTaxScreen.cpp \
	src/mash/CommandScreen.cpp \
	src/mash/CommandTriangle.cpp \
	src/mash/CommandFind.cpp \
	src/mash/CommandInfo.cpp \
	src/mash/CommandPaste.cpp \
	src/mash/CommandSketch.cpp \
	src/mash/CommandList.cpp \
	src/mash/hash.cpp \
	src/mash/HashList.cpp \
	src/mash/HashPriorityQueue.cpp \
	src/mash/HashSet.cpp \
	src/mash/MinHashHeap.cpp \
	src/mash/MurmurHash3.cpp \
	src/mash/mash.cpp \
	src/mash/Sketch.cpp \
	src/mash/sketchParameterSetup.cpp \

OBJECTS=$(SOURCES:.cpp=.o) src/mash/capnp/MinHash.capnp.o

all : mash libmash.a

mash : libmash.a src/mash/memcpyWrap.o
	$(CXX) $(CXXFLAGS) $(CPPFLAGS) -o mash src/mash/memcpyWrap.o libmash.a /usr/local//lib/libcapnp.a /usr/local//lib/libkj.a /bip7_disk/yuqing109/Mash/boost_1_75_0/bin.v2//lib/libboost_math_c99.a -lstdc++ -lz -lm -lpthread

libmash.a : $(OBJECTS)
	ar -cr libmash.a $(OBJECTS)
	ranlib libmash.a

.SUFFIXES :

%.o : %.cpp src/mash/capnp/MinHash.capnp.h
	$(CXX) -c $(CXXFLAGS) $(CPPFLAGS) -o $@ $<

%.o : %.c++
	$(CXX) -c $(CXXFLAGS) $(CPPFLAGS) -o $@ $<

src/mash/memcpyWrap.o : src/mash/memcpyWrap.c
	$(CC) $(CFLAGS) -c -o $@ $<

src/mash/capnp/MinHash.capnp.c++ src/mash/capnp/MinHash.capnp.h : src/mash/capnp/MinHash.capnp
	cd src/mash/capnp;export PATH=/usr/local//bin/:${PATH};capnp compile -I /usr/local//include -oc++ MinHash.capnp

.PHONY: install-man install
install-man:
	mkdir -p /usr/local/share/man/man1
	cp `pwd`/doc/man/*.1 /usr/local/share/man/man1

install : mash install-man
	mkdir -p /usr/local/bin/
	mkdir -p /usr/local/lib/
	mkdir -p /usr/local/include/
	mkdir -p /usr/local/include/mash
	mkdir -p /usr/local/include/mash/capnp
	cp `pwd`/mash /usr/local/bin/
	cp `pwd`/libmash.a /usr/local/lib/
	cp `pwd`/src/mash/*.h /usr/local/include/mash/
	cp `pwd`/src/mash/capnp/MinHash.capnp.h /usr/local/include/mash/capnp/

.PHONY: uninstall uninstall-man
uninstall: uninstall-man
	rm -f /usr/local/bin/mash
	rm -f /usr/local/lib/libmash.a
	rm -rf /usr/local/include/mash

uninstall-man:
	rm -f /usr/local/share/man/man1/mash*.1

clean :
	-rm mash
	-rm libmash.a
	-rm src/mash/*.o
	-rm src/mash/capnp/*.o
	-rm src/mash/capnp/*.c++
	-rm src/mash/capnp/*.h

.PHONY: test
test : testSketch testDist testScreen

testSketch : mash test/genomes.msh test/reads.msh
	./mash info -d test/genomes.msh > test/genomes.json
	./mash info -d test/reads.msh > test/reads.json
	diff test/genomes.json test/ref/genomes.json
	diff test/reads.json test/ref/reads.json

test/genomes.msh : mash
	cd test ; ../mash sketch -o genomes.msh genome1.fna genome2.fna genome3.fna

test/reads.msh : mash
	cd test ; ../mash sketch -r -I reads reads1.fastq reads2.fastq -o reads.msh

testDist : mash test/genomes.msh test/reads.msh
	./mash dist test/genomes.msh test/reads.msh > test/genomes.dist
	diff test/genomes.dist test/ref/genomes.dist

testScreen : mash test/genomes.msh
	cd test ; ../mash screen genomes.msh reads1.fastq reads2.fastq > screen
	diff test/screen test/ref/screen
