#!/usr/bin/perl
use strict;
use warnings;

open my $ofp, ">", "filelist.mk";

sub find_srcfiles {
    my $dir = shift;
    my $c_src = qx(find $dir -iname "*.c");
    my $h_src = qx(find $dir -iname "*.h");
    my $cxx_src = qx(find $dir -iname "*.cc");
    my @ret;
    push @ret, split(' ', $c_src), split(' ', $h_src), split(' ', $cxx_src);
    return @ret;
}

sub fmt_filelist {
    return join(" \\\n    ", @_);
}

sub add_target {
    my $name = shift;
    print $ofp "noinst_LTLIBRARIES += $name.la\n";
    foreach my $cat (qw(CFLAGS CPPFLAGS)) {
        print $ofp "${name}_la_${cat} = \$(libcouchbase_la_${cat})\n";
    }
}

sub add_target_with_sources {
    my ($name,$path,$extras,$exclude) = @_;
    my @srclist = find_srcfiles($path);
    if ($extras) {
        foreach my $extra (@$extras) {
            push @srclist, $extra;
        }
    }
    if ($exclude) {
        # Omit these
        @srclist = grep { $_ !~ $exclude } @srclist;
    }
    print $ofp "${name}_la_SOURCES = ".fmt_filelist(@srclist) . "\n";
    add_target($name);
}

my $TEST_PREAMBLE = "if HAVE_CXX\nif HAVE_GOOGLETEST_SRC\n";
my $TEST_POSTAMBLE = "endif\nendif\n";

sub add_test {
    my ($name,$path,$options) = @_;
    my $ltname = $name;
    $options ||= {};

    # Dependencies need to appear in the proper order
    my @deps = qw(
        libnetbuf.la librdb.la liblcbio.la libmcreq.la libcouchbase.la liblcbutils.la);
    $ltname =~ s,[-/],_,g;

    if ($options->{deps}) {
        push @deps, @{$options->{deps}};
    }
    push @deps, "libgtest.la";
    print $ofp $TEST_PREAMBLE;
    print $ofp "check_PROGRAMS += $name\n";
    if ($options->{nomain}) {
        print $ofp "${ltname}_SOURCES =\n";
    } else {
        print $ofp "${ltname}_SOURCES = tests/nonio_tests.cc\n";
    }
    if ($options->{srcextra}) {
        print $ofp "${ltname}_SOURCES += " . join(' ', @{$options->{srcextra}}) . "\n";
    }
    print $ofp "${ltname}_SOURCES += ". fmt_filelist(find_srcfiles($path)) . "\n";
    print $ofp "${ltname}_DEPENDENCIES = " . join(' ', @deps) . " libgtest.la \n";
    print $ofp "${ltname}_LDADD = " . join(' ', @deps) . "\n";
    print $ofp <<"EOF";
${ltname}_LDADD += -lpthread
${ltname}_LDFLAGS =
${ltname}_CFLAGS = \$(AM_NOWARN_CFLAGS)
${ltname}_CXXFLAGS = \$(AM_NOWARN_CXXFLAGS)
${ltname}_CPPFLAGS = \$(AM_NOWARN_CPPFLAGS) -I\$(GTEST_ROOT)
${ltname}_CPPFLAGS += -I\$(GTEST_ROOT)/include -I\$(top_srcdir)/tests \$(NO_WERROR)
EOF
    print $ofp $TEST_POSTAMBLE;
}

printf $ofp "# This file generated by %s on %s\n", $0, scalar localtime();

my @PKGINCLUDE_HEADERS;

# Find the main source files
push @PKGINCLUDE_HEADERS,
    find_srcfiles("include/libcouchbase"),
    "include/libcouchbase/configuration.h",
    "plugins/io/libuv/libuv_io_opts.h",
    "plugins/io/libev/libev_io_opts.h",
    "plugins/io/libevent/libevent_io_opts.h",
    "plugins/io/select/select_io_opts.h",
    "plugins/io/iocp/iocp_iops.h";

my %tmp = map { $_ => 1 } @PKGINCLUDE_HEADERS;
@PKGINCLUDE_HEADERS = keys %tmp;

# @PKGINCLUDE_HEADERS = grep { $_ !~ /configuration\.h/ } @PKGINCLUDE_HEADERS;

print $ofp "pkginclude_HEADERS = ".fmt_filelist(@PKGINCLUDE_HEADERS)."\n";
my @LCB_SOURCES = (find_srcfiles("src"), find_srcfiles("plugins/io/select"));

# Filter out libraries we're gonna build later on
@LCB_SOURCES = grep { $_ !~ m,src/ssl, && $_ !~ m,src/lcbht, && $_ !~ m,src/mt, } @LCB_SOURCES;

# Filter out generated files
@LCB_SOURCES = grep { $_ !~ m,src/config.h, && $_ !~ m,src/probes.h, } @LCB_SOURCES;

print $ofp "libcouchbase_la_SOURCES += ".fmt_filelist(@LCB_SOURCES)."\n";
print $ofp "libcouchbase_la_SOURCES += ".fmt_filelist(find_srcfiles("contrib/cJSON"))."\n";
print $ofp "libcouchbase_la_SOURCES += ".fmt_filelist(find_srcfiles("include/memcached"))."\n";
print $ofp "libcouchbase_la_SOURCES += ".fmt_filelist(find_srcfiles("include/ep-engine"))."\n";
print $ofp "if HAVE_WINSOCK2\n";
print $ofp "libcouchbase_la_SOURCES +=".
            fmt_filelist(find_srcfiles("plugins/io/iocp"))."\n";
print $ofp "endif\n";


print $ofp "if ENABLE_SSL\n";
add_target_with_sources("liblcbssl", "src/ssl");
print $ofp "endif\n";

print $ofp "if BUILD_STATIC_SNAPPY\n";
add_target_with_sources("liblcbsnappy", "contrib/snappy");
print $ofp "else\n";
add_target_with_sources("liblcbsnappy", "config/dummy-c.c");
print $ofp "endif\n";
print $ofp "libcouchbase_la_DEPENDENCIES += liblcbsnappy.la\n";
print $ofp "libcouchbase_la_LIBADD += liblcbsnappy.la\n";

# Find the basic test stuff

add_target_with_sources("liblcbio", "src/lcbio");
add_target_with_sources("librdb", "src/rdb");
add_target_with_sources("libmcreq", "src/mc");
add_target_with_sources("libnetbuf", "src/netbuf");
add_target_with_sources("libcliopts", "contrib/cliopts");
add_target_with_sources("liblcbht", "src/lcbht", [find_srcfiles("contrib/http_parser")]);

print $ofp "if BUILD_LCBMT\n";
add_target_with_sources("liblcbmt", "src/mt", [], qr,src/plugin/,);
print $ofp "liblcbmt_la_CPPFLAGS += -pthread\n";
print $ofp "endif\n";

print $ofp "if HAVE_CXX\nif BUILD_TOOLS\n";
add_target_with_sources("liblcbtools", "tools/common", [find_srcfiles("contrib/cliopts")]);
print $ofp "endif\nendif\n";

my @CBUTIL_SOURCES = qw(contrib/cJSON/cJSON.c src/strcodecs/base64.c
    src/strcodecs/url_encoding.c src/gethrtime.c src/genhash.c src/hashtable.c
    src/hashset.c src/hostlist.c src/list.c src/logging.c src/packetutils.c
    src/ringbuffer.c src/simplestring.c);

add_target("liblcbutils");
print $ofp "liblcbutils_la_SOURCES = ".fmt_filelist(@CBUTIL_SOURCES) . "\n";

print $ofp "if HAVE_CXX\nif HAVE_GOOGLETEST_SRC\n";
add_target_with_sources("libioserver", "tests/ioserver");
add_test("tests/nonio-tests", "tests/basic");
add_test("tests/rdb-tests", "tests/rdb");
add_test("tests/mc-tests", "tests/mc", {deps=>["liblcbsnappy.la", "libcouchbase.la"]});
add_test("tests/sock-tests", "tests/socktests", {deps=>["libioserver.la"]});
add_test("tests/htparse-tests", "tests/htparse", {deps=>["liblcbht.la", "liblcbutils.la"]});

print $ofp "if HAVE_COUCHBASEMOCK\n";
add_test("tests/unit-tests", "tests/iotests",
    {deps => ["libmocksupport.la", "liblcbutils.la"], srcextra=>["tests/unit_tests.cc"], nomain=>1});
print $ofp "endif\n";

print $ofp "endif\nendif\n";

my $MOCKSUPP_LIST = fmt_filelist(find_srcfiles("tests/mocksupport"));
print $ofp <<"EOF";
if HAVE_COUCHBASEMOCK
if HAVE_GOOGLETEST_SRC
if HAVE_CXX
noinst_LTLIBRARIES += libmocksupport.la
libmocksupport_la_SOURCES = $MOCKSUPP_LIST
tests_unit_tests_DEPENDENCIES += tests/CouchbaseMock.jar
endif
endif
endif
EOF


# Write the EXTRA_DIST
print $ofp "EXTRA_DIST =\n";
my @cmakes = map { chomp($_); $_; } qx(git ls-files | grep CMakeLists.txt);
push @cmakes, 'cmake';

print $ofp "EXTRA_DIST +=".fmt_filelist(@cmakes)."\n";
my @doxystuff = (qw(Doxyfile DoxygenLayout.xml));
push @doxystuff, map { chomp($_); $_; } qx(git ls-files doc);
print $ofp "EXTRA_DIST +=".fmt_filelist(@doxystuff)."\n";
