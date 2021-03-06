
EXTRA_DIST      = recovery.pl valgrind.supp data/* plugin.cc

noinst_PROGRAMS = test recovery issue32 issue43
noinst_BIN      = test recovery issue32 issue43

BOOST_LIBS		= $(BOOST_SYSTEM_LIBS) $(BOOST_THREAD_LIBS) $(BOOST_FILESYSTEM_LIBS)
BOOST_FLAGS     = $(BOOST_SYSTEM_LDFLAGS) $(BOOST_THREAD_LDFLAGS) $(BOOST_FILESYSTEM_LDFLAGS)

AM_CPPFLAGS     = -I../include -I$(top_srcdir)/include -I../src

test_SOURCES    = api110.cpp \
			      approx.cpp \
				  blob_manager.cpp \
				  btree.cpp \
				  btree_cursor.cpp \
				  btree_default.cpp \
				  btree_erase.cpp \
				  btree_insert.cpp \
				  btree_key.cpp \
				  changeset.cpp \
				  check.cpp \
				  compression.cpp \
				  cppapi.cpp \
				  crc32.cpp \
				  cursor1.cpp \
				  cursor2.cpp \
				  db.cpp \
				  device.cpp \
				  duplicates.cpp \
				  env.cpp \
				  error.cpp \
				  upscaledb.cpp \
				  uqi.cpp \
				  journal.cpp \
				  misc.cpp \
				  os.cpp \
				  os.hpp \
				  page.cpp \
				  page_manager.cpp \
				  main.cpp \
				  recno.cpp \
				  simd.cpp \
				  txn.cpp \
				  txn_cursor.cpp \
				  utils.h \
				  zint32.cpp

recovery_SOURCES = recovery.cpp

test_LDADD      = $(top_builddir)/src/.libs/libupscaledb.a \
				  $(BOOST_LIBS) -lpthread -ldl
recovery_LDADD  = $(top_builddir)/src/.libs/libupscaledb.a \
				  $(BOOST_LIBS) -lpthread -ldl
test_LDFLAGS    = $(BOOST_FLAGS)
recovery_LDFLAGS= $(BOOST_FLAGS)

if ENABLE_REMOTE
test_SOURCES   += remote.cpp
AM_CPPFLAGS    += -DUPS_ENABLE_REMOTE
test_LDADD     += $(top_builddir)/src/5server/.libs/libupsserver.a \
				  -lprotobuf -luv -ldl
recovery_LDADD += $(top_builddir)/src/5server/.libs/libupsserver.a \
				  -lprotobuf -luv -ldl
endif

if ENABLE_ENCRYPTION
test_SOURCES   += aes.cpp
test_LDADD     += -lcrypto
recovery_LDADD += -lcrypto
endif

if WITH_ZLIB
test_LDADD     += -lz
recovery_LDADD += -lz
endif
if WITH_SNAPPY
test_LDADD     += -lsnappy
recovery_LDADD += -lsnappy
endif

AM_CFLAGS	    =
AM_CXXFLAGS	    =
if ENABLE_SSE2
AM_CFLAGS	   += -msse2 -flax-vector-conversions
AM_CXXFLAGS	   += -msse2 -flax-vector-conversions
endif
if ENABLE_SSE4
AM_CFLAGS	   += -msse4 -flax-vector-conversions
AM_CXXFLAGS    += -msse4 -flax-vector-conversions
endif

valgrind:
	valgrind --suppressions=valgrind.supp --leak-check=full \
			--num-callers=20 --show-reachable=yes \
			--log-file=valgrind-out.txt ./test

test_recovery:
	perl recovery.pl

issue32_SOURCES = issue32.c
issue32_LDADD   = $(top_builddir)/src/libupscaledb.la

issue43_SOURCES = issue43.cc
issue43_LDADD   = $(top_builddir)/src/libupscaledb.la

plugin: plugin.cc
	$(CXX) -fPIC -shared -o plugin.so plugin.cc $(AM_CPPFLAGS)
