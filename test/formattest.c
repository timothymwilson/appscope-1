#include <errno.h>
#include <float.h>
#include <limits.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "dbg.h"
#include "format.h"

#include "test.h"


static void
fmtCreateReturnsValidPtrForGoodFormat(void** state)
{
    format_t* fmt = fmtCreate(CFG_METRIC_STATSD);
    assert_non_null(fmt);
    fmtDestroy(&fmt);
    assert_null(fmt);

    fmt = fmtCreate(CFG_METRIC_JSON);
    assert_non_null(fmt);
    fmtDestroy(&fmt);
    assert_null(fmt);
}

static void
fmtCreateReturnsNullPtrForBadFormat(void** state)
{
    format_t* fmt = fmtCreate(CFG_FORMAT_MAX);
    assert_null(fmt);
}

void
verifyDefaults(format_t* fmt)
{
    assert_string_equal(fmtStatsDPrefix(fmt), DEFAULT_STATSD_PREFIX);
    assert_int_equal(fmtStatsDMaxLen(fmt), DEFAULT_STATSD_MAX_LEN);
    assert_int_equal(fmtOutVerbosity(fmt), DEFAULT_OUT_VERBOSITY);
    assert_int_equal(fmtCustomTags(fmt), DEFAULT_CUSTOM_TAGS);
}

static void
fmtCreateHasExpectedDefaults(void** state)
{
    format_t* fmt = fmtCreate(CFG_METRIC_STATSD);
    verifyDefaults(fmt);
    fmtDestroy(&fmt);

    // Test that accessors work with null fmt too
    verifyDefaults(NULL);
}

static void
fmtDestroyNullDoesntCrash(void** state)
{
    fmtDestroy(NULL);
    format_t* fmt = NULL;
    fmtDestroy(&fmt);
    // Implicitly shows that calling fmtDestroy with NULL is harmless
}

static void
fmtStatsDPrefixSetAndGet(void** state)
{
    format_t* fmt = fmtCreate(CFG_METRIC_STATSD);
    fmtStatsDPrefixSet(fmt, "cribl.io");
    assert_string_equal(fmtStatsDPrefix(fmt), "cribl.io");
    fmtStatsDPrefixSet(fmt, "");
    assert_string_equal(fmtStatsDPrefix(fmt), "");
    fmtStatsDPrefixSet(fmt, "huh");
    assert_string_equal(fmtStatsDPrefix(fmt), "huh");
    fmtStatsDPrefixSet(fmt, NULL);
    assert_string_equal(fmtStatsDPrefix(fmt), DEFAULT_STATSD_PREFIX);
    fmtDestroy(&fmt);
}

static void
fmtStatsDMaxLenSetAndGet(void** state)
{
    format_t* fmt = fmtCreate(CFG_METRIC_STATSD);
    fmtStatsDMaxLenSet(fmt, 0);
    assert_int_equal(fmtStatsDMaxLen(fmt), 0);
    fmtStatsDMaxLenSet(fmt, UINT_MAX);
    assert_int_equal(fmtStatsDMaxLen(fmt), UINT_MAX);
    fmtDestroy(&fmt);
}

static void
fmtOutVerbositySetAndGet(void** state)
{
    format_t* fmt = fmtCreate(CFG_METRIC_JSON);
    fmtOutVerbositySet(fmt, 0);
    assert_int_equal(fmtOutVerbosity(fmt), 0);
    fmtOutVerbositySet(fmt, UINT_MAX);
    assert_int_equal(fmtOutVerbosity(fmt), CFG_MAX_VERBOSITY);
    fmtDestroy(&fmt);
}

static void
fmtCustomTagsSetAndGet(void ** state)
{
    format_t* fmt = fmtCreate(CFG_METRIC_STATSD);
    {
        custom_tag_t t1 = {"name1", "value1"};
        custom_tag_t t2 = {"name2", "value2"};
        custom_tag_t* tags[] = { &t1, &t2, NULL };
        fmtCustomTagsSet(fmt, tags);
        assert_non_null(fmtCustomTags(fmt));
        assert_string_equal(fmtCustomTags(fmt)[0]->name, "name1");
        assert_string_equal(fmtCustomTags(fmt)[0]->value, "value1");
        assert_string_equal(fmtCustomTags(fmt)[1]->name, "name2");
        assert_string_equal(fmtCustomTags(fmt)[1]->value, "value2");
        assert_null(fmtCustomTags(fmt)[2]);
    }

    custom_tag_t* tags[] = { NULL };
    fmtCustomTagsSet(fmt, tags);
    assert_null(fmtCustomTags(fmt));

    fmtCustomTagsSet(fmt, NULL);
    assert_null(fmtCustomTags(fmt));

    fmtDestroy(&fmt);
}

static void
fmtStringStatsDNullEventDoesntCrash(void** state)
{
    format_t* fmt = fmtCreate(CFG_METRIC_STATSD);
    assert_null(fmtString(fmt, NULL, NULL));
    fmtDestroy(&fmt);
}

static void
fmtStringStatsDNullEventFieldsDoesntCrash(void** state)
{
    event_t e = INT_EVENT("useful.apps", 1, CURRENT, NULL);

    format_t* fmt = fmtCreate(CFG_METRIC_STATSD);
    char* msg = fmtString(fmt, &e, NULL);
    assert_string_equal(msg, "useful.apps:1|g\n");
    if (msg) free(msg);
    fmtDestroy(&fmt);
}

static void
fmtStringNullFmtDoesntCrash(void** state)
{
    event_field_t fields[] = {
        STRFIELD("proc",             "redis",            2),
        FIELDEND
    };
    event_t e = INT_EVENT("useful.apps", 1, CURRENT, fields);

    assert_null(fmtString(NULL, &e, NULL));
}

static void
fmtStringStatsDHappyPath(void** state)
{
    char* g_hostname = "myhost";
    char* g_procname = "testapp";
    int g_openPorts = 2;
    pid_t pid = 666;
    int fd = 3;
    char* proto = "TCP";
    in_port_t localPort = 8125;

    event_field_t fields[] = {
        STRFIELD("proc",             g_procname,            2),
        NUMFIELD("pid",              pid,                   7),
        NUMFIELD("fd",               fd,                    7),
        STRFIELD("host",             g_hostname,            2),
        STRFIELD("proto",            proto,                 1),
        NUMFIELD("port",             localPort,             4),
        FIELDEND
    };
    event_t e = INT_EVENT("net.port", g_openPorts, CURRENT, fields);

    format_t* fmt = fmtCreate(CFG_METRIC_STATSD);
    assert_non_null(fmt);
    fmtOutVerbositySet(fmt, CFG_MAX_VERBOSITY);

    char* msg = fmtString(fmt, &e, NULL);
    assert_non_null(msg);

    char expected[1024];
    int rv = snprintf(expected, sizeof(expected),
        "net.port:%d|g|#proc:%s,pid:%d,fd:%d,host:%s,proto:%s,port:%d\n",
         g_openPorts, g_procname, pid, fd, g_hostname, proto, localPort);
    assert_true(rv > 0 && rv < 1024);
    assert_string_equal(expected, msg);
    free(msg);

    fmtDestroy(&fmt);
    assert_null(fmt);
}

static void
fmtStringStatsDHappyPathFilteredFields(void** state)
{
    char* g_hostname = "myhost";
    char* g_procname = "testapp";
    int g_openPorts = 2;
    pid_t pid = 666;
    int fd = 3;
    char* proto = "TCP";
    in_port_t localPort = 8125;

    event_field_t fields[] = {
        STRFIELD("proc",             g_procname,            2),
        NUMFIELD("pid",              pid,                   7),
        NUMFIELD("fd",               fd,                    7),
        STRFIELD("host",             g_hostname,            2),
        STRFIELD("proto",            proto,                 1),
        NUMFIELD("port",             localPort,             4),
        FIELDEND
    };
    event_t e = INT_EVENT("net.port", g_openPorts, CURRENT, fields);

    format_t* fmt = fmtCreate(CFG_METRIC_STATSD);
    assert_non_null(fmt);
    fmtOutVerbositySet(fmt, CFG_MAX_VERBOSITY);

    regex_t re;
    assert_int_equal(regcomp(&re, "^[p]", REG_EXTENDED), 0);

    char* msg = fmtString(fmt, &e, &re);
    assert_non_null(msg);


    char expected[1024];
    int rv = snprintf(expected, sizeof(expected),
        "net.port:%d|g|#proc:%s,pid:%d,proto:%s,port:%d\n",
         g_openPorts, g_procname, pid, proto, localPort);
    assert_true(rv > 0 && rv < 1024);
    assert_string_equal(expected, msg);
    free(msg);

    regfree(&re);
    fmtDestroy(&fmt);
    assert_null(fmt);
}

static void
fmtStatsDWithCustomFields(void** state)
{
    format_t* fmt = fmtCreate(CFG_METRIC_STATSD);
    assert_non_null(fmt);

    custom_tag_t t1 = {"name1", "value1"};
    custom_tag_t t2 = {"name2", "value2"};
    custom_tag_t* tags[] = { &t1, &t2, NULL };
    fmtCustomTagsSet(fmt, tags);

    event_t e = INT_EVENT("statsd.metric", 3, CURRENT, NULL);

    char* msg = fmtString(fmt, &e, NULL);
    assert_non_null(msg);

    assert_string_equal("statsd.metric:3|g|#name1:value1,name2:value2\n", msg);
    free(msg);
    fmtDestroy(&fmt);
}

static void
fmtStatsDWithCustomAndStatsdFields(void** state)
{
    format_t* fmt = fmtCreate(CFG_METRIC_STATSD);
    assert_non_null(fmt);

    custom_tag_t t1 = {"tag", "value"};
    custom_tag_t* tags[] = { &t1, NULL };
    fmtCustomTagsSet(fmt, tags);

    event_field_t fields[] = {
        STRFIELD("proc",             "test",                2),
        FIELDEND
    };
    event_t e = INT_EVENT("fs.read", 3, CURRENT, fields);

    char* msg = fmtString(fmt, &e, NULL);
    assert_non_null(msg);

    assert_string_equal("fs.read:3|g|#tag:value,proc:test\n", msg);
    free(msg);
    fmtDestroy(&fmt);
}

static void
fmtStringStatsDReturnsNullIfSpaceIsInsufficient(void** state)
{
    format_t* fmt = fmtCreate(CFG_METRIC_STATSD);
    fmtStatsDMaxLenSet(fmt, 28);
    fmtStatsDPrefixSet(fmt, "98");

    // Test one that just fits
    event_t e1 = INT_EVENT("A", -1234567890123456789, DELTA_MS, NULL);
    char* msg = fmtString(fmt, &e1, NULL);
    assert_non_null(msg);
    assert_string_equal(msg, "98A:-1234567890123456789|ms\n");
    assert_true(strlen(msg) == 28);
    free(msg);

    // One character too much (longer name)
    event_t e2 = INT_EVENT("AB", e1.value.integer, e1.type, e1.fields);
    msg = fmtString(fmt, &e2, NULL);
    assert_null(msg);
    fmtDestroy(&fmt);
}

static void
fmtStringStatsDReturnsNullIfSpaceIsInsufficientMax(void** state)
{
    format_t* fmt = fmtCreate(CFG_METRIC_STATSD);
    fmtStatsDMaxLenSet(fmt, 2+1+315+3);
    fmtStatsDPrefixSet(fmt, "98");

    // Test one that just fits
    event_t e1 = FLT_EVENT("A", -DBL_MAX, DELTA_MS, NULL);
    char* msg = fmtString(fmt, &e1, NULL);
    assert_non_null(msg);
    assert_string_equal(msg, "98A:-179769313486231570814527423731704356798070567525844996598917476803157260780028538760589558632766878171540458953514382464234321326889464182768467546703537516986049910576551282076245490090389328944075868508455133942304583236903222948165808559332123348274797826204144723168738177180919299881250404026184124858368.00|ms\n");
    assert_true(strlen(msg) == 2+1+315+3);
    free(msg);

    // One character too much (longer name)
    event_t e2 = FLT_EVENT("AB", e1.value.floating, e1.type, e1.fields);
    msg = fmtString(fmt, &e2, NULL);
    assert_null(msg);
    fmtDestroy(&fmt);
}

static void
fmtStringStatsDVerifyEachStatsDType(void** state)
{
    format_t* fmt = fmtCreate(CFG_METRIC_STATSD);

    data_type_t t;
    for (t=DELTA; t<=SET; t++) {
        event_t e = INT_EVENT("A", 1, t, NULL);
        char* msg = fmtString(fmt, &e, NULL);
        switch (t) {
            case DELTA:
                assert_string_equal(msg, "A:1|c\n");
                break;
            case CURRENT:
                assert_string_equal(msg, "A:1|g\n");
                break;
            case DELTA_MS:
                assert_string_equal(msg, "A:1|ms\n");
                break;
            case HISTOGRAM:
                assert_string_equal(msg, "A:1|h\n");
                break;
            case SET:
                assert_string_equal(msg, "A:1|s\n");
                break;
        }
        free(msg);
    }

    assert_int_equal(dbgCountMatchingLines("src/format.c"), 0);

    // In undefined case, just don't crash...
    event_t e = INT_EVENT("A", 1, SET+1, NULL);
    char* msg = fmtString(fmt, &e, NULL);
    if (msg) free(msg);

    assert_int_equal(dbgCountMatchingLines("src/format.c"), 1);
    dbgInit(); // reset dbg for the rest of the tests
    fmtDestroy(&fmt);
}

static void
fmtStringStatsDOmitsFieldsIfSpaceIsInsufficient(void** state)
{
    event_field_t fields[] = {
        NUMFIELD("J",               222,                    9),
        STRFIELD("I",               "V",                    8),
        NUMFIELD("H",               111,                    7),
        STRFIELD("G",               "W",                    6),
        NUMFIELD("F",               321,                    5),
        STRFIELD("E",               "X",                    4),
        NUMFIELD("D",               654,                    3),
        STRFIELD("C",               "Y",                    2),
        NUMFIELD("B",               987,                    1),
        STRFIELD("A",               "Z",                    0),
        FIELDEND
    };
    event_t e = INT_EVENT("metric", 1, DELTA, fields);
    format_t* fmt = fmtCreate(CFG_METRIC_STATSD);
    fmtOutVerbositySet(fmt, CFG_MAX_VERBOSITY);

    // Note that this test documents that we don't prioritize
    // the lowest cardinality fields when space is scarce.  We
    // just "fit what we can", while ensuring that we obey
    // fmtStatsDMaxLen.

    fmtStatsDMaxLenSet(fmt, 31);
    char* msg = fmtString(fmt, &e, NULL);
    assert_non_null(msg);
    //                                 1111111111222222222233
    //                        1234567890123456789012345678901
    //                       "metric:1|c|#J:222,I:V,H:111,G:W,F:321,E:X,D:654"
    assert_string_equal(msg, "metric:1|c|#J:222,I:V,H:111\n");
    free(msg);

    fmtStatsDMaxLenSet(fmt, 32);
    msg = fmtString(fmt, &e, NULL);
    assert_non_null(msg);
    assert_string_equal(msg, "metric:1|c|#J:222,I:V,H:111,G:W\n");
    free(msg);

    fmtDestroy(&fmt);
}

static void
fmtStringStatsDHonorsCardinality(void** state)
{
    event_field_t fields[] = {
        STRFIELD("A",               "Z",                    0),
        NUMFIELD("B",               987,                    1),
        STRFIELD("C",               "Y",                    2),
        NUMFIELD("D",               654,                    3),
        STRFIELD("E",               "X",                    4),
        NUMFIELD("F",               321,                    5),
        STRFIELD("G",               "W",                    6),
        NUMFIELD("H",               111,                    7),
        STRFIELD("I",               "V",                    8),
        NUMFIELD("J",               222,                    9),
        FIELDEND
    };
    event_t e = INT_EVENT("metric", 1, DELTA, fields);
    format_t* fmt = fmtCreate(CFG_METRIC_STATSD);

    fmtOutVerbositySet(fmt, 0);
    char* msg = fmtString(fmt, &e, NULL);
    assert_non_null(msg);
    assert_string_equal(msg, "metric:1|c|#A:Z\n");
    free(msg);

    fmtOutVerbositySet(fmt, 5);
    msg = fmtString(fmt, &e, NULL);
    assert_non_null(msg);
    assert_string_equal(msg, "metric:1|c|#A:Z,B:987,C:Y,D:654,E:X,F:321\n");
    free(msg);

    fmtOutVerbositySet(fmt, 9);
    msg = fmtString(fmt, &e, NULL);
    assert_non_null(msg);
    // Verify every name is there, 10 in total
    int count =0;
    event_field_t* f;
    for (f = fields; f->value_type != FMT_END; f++) {
        assert_non_null(strstr(msg, f->name));
        count++;
    }
    assert_true(count == 10);
    free(msg);

    fmtDestroy(&fmt);
}

static void
fmtEventMessageStringValue(void** state)
{
    format_t* fmt = fmtCreate(CFG_EVENT_ND_JSON);
    assert_non_null(fmt);

    event_format_t event_format;
    event_format.timestamp = 1573058085.991;
    event_format.src = "stdin";
    event_format.hostname = "earl";
    event_format.data = "поспехаў";
    event_format.datasize = strlen(event_format.data);
    event_format.uid = 0xCAFEBABEDEADBEEF;
    event_format.cmd = "cmd";
    event_format.procname = "formattest";

    assert_null(fmtEventMessageString(NULL, NULL));

    assert_null(fmtEventMessageString(NULL, &event_format));

    assert_null(fmtEventMessageString(fmt, NULL));

    char* str = fmtEventMessageString(fmt, &event_format);
    assert_non_null(str);

    //printf("%s:%d %s\n", __FUNCTION__, __LINE__, str);
    assert_string_equal(str, "{\"ty\":\"ev\","
                              "\"id\":\"earl-formattest-cmd\","
                              "\"_time\":1573058085.991,"
                              "\"source\":\"stdin\","
                              "\"_raw\":\"поспехаў\","
                              "\"host\":\"earl\","
                              "\"_channel\":\"14627333968688430831\"}");
    free(str);

    fmtDestroy(&fmt);
}

static void
fmtEventMessageStringWithEmbeddedNulls(void** state)
{
    format_t* fmt = fmtCreate(CFG_EVENT_ND_JSON);
    assert_non_null(fmt);

    event_format_t event_format;
    event_format.timestamp = 1573058085.001;
    event_format.src = "stdout";
    event_format.hostname = "earl";
    char buf[] = "Unë mund të ha qelq dhe nuk më gjen gjë";
    event_format.data = buf;
    event_format.datasize = strlen(event_format.data);
    event_format.data[9] = '\0';                  //  <-- Null in middle of buf
    event_format.data[29] = '\0';                 //  <-- Null in middle of buf
    event_format.uid = 0xCAFEBABEDEADBEEF;
    event_format.procname = "";
    event_format.cmd = "";
    
    // test that _raw has the nulls properly escaped
    char* str = fmtEventMessageString(fmt, &event_format);
    assert_non_null(str);
    assert_string_equal(str, "{\"ty\":\"ev\","
                              "\"id\":\"earl--\","
                              "\"_time\":1573058085.001,"
                              "\"source\":\"stdout\","
                              "\"_raw\":\"Unë mund\\u0000të ha qelq dhe nuk\\u0000më gjen gjë\","
                              "\"host\":\"earl\","
                              "\"_channel\":\"14627333968688430831\"}");
    free(str);

    // test that datasize of zero acts like null delimited input.
    event_format.datasize=0;
    str = fmtEventMessageString(fmt, &event_format);
    assert_non_null(str);
    assert_string_equal(str, "{\"ty\":\"ev\","
                              "\"id\":\"earl--\","
                              "\"_time\":1573058085.001,"
                              "\"source\":\"stdout\","
                              "\"_raw\":\"Unë mund\","
                              "\"host\":\"earl\","
                              "\"_channel\":\"14627333968688430831\"}");
    free(str);

    // test that null data returns empty string for _raw.
    event_format.datasize=29;
    event_format.data=NULL;
    str = fmtEventMessageString(fmt, &event_format);
    assert_non_null(str);
    assert_string_equal(str, "{\"ty\":\"ev\","
                              "\"id\":\"earl--\","
                              "\"_time\":1573058085.001,"
                              "\"source\":\"stdout\","
                              "\"_raw\":\"\","
                              "\"host\":\"earl\","
                              "\"_channel\":\"14627333968688430831\"}");
    free(str);

    fmtDestroy(&fmt);
}

static void
fmtStringMetricJsonNoFields(void** state)
{
    format_t* fmt = fmtCreate(CFG_METRIC_JSON);

    const char* map[] =
        //DELTA     CURRENT  DELTA_MS  HISTOGRAM    SET
        {"counter", "gauge", "timer", "histogram", "set", "unknown"};

    // test each value of _metric_type
    data_type_t type;
    for (type=DELTA; type<=SET+1; type++) {
        char expected[256];
        assert_return_code(snprintf(expected, sizeof(expected),
                 "{\"_metric\":\"A\","
                 "\"_metric_type\":\"%s\","
                 "\"_value\":1}", map[type]), errno);

        event_t e = INT_EVENT("A", 1, type, NULL);
        char* actual = fmtString(fmt, &e, NULL);
        assert_string_equal(actual, expected);
        if (actual) free(actual);
    }
    fmtDestroy(&fmt);
}

static void
fmtStringMetricJsonWFields(void** state)
{
    format_t* fmt = fmtCreate(CFG_METRIC_JSON);
    event_field_t fields[] = {
        STRFIELD("A",               "Z",                    0),
        NUMFIELD("B",               987,                    1),
        STRFIELD("C",               "Y",                    2),
        NUMFIELD("D",               654,                    3),
        FIELDEND
    };
    event_t e = INT_EVENT("hey", 2, HISTOGRAM, fields);
    char* str = fmtString(fmt, &e, NULL);
    assert_string_equal(str,
                 "{\"_metric\":\"hey\","
                 "\"_metric_type\":\"histogram\","
                 "\"_value\":2,"
                 "\"A\":\"Z\",\"B\":987,\"C\":\"Y\",\"D\":654}");
    if (str) free(str);
    fmtDestroy(&fmt);
}

static void
fmtStringMetricJsonWFilteredFields(void** state)
{
    format_t* fmt = fmtCreate(CFG_METRIC_JSON);
    event_field_t fields[] = {
        STRFIELD("A",               "Z",                    0),
        NUMFIELD("B",               987,                    1),
        STRFIELD("C",               "Y",                    2),
        NUMFIELD("D",               654,                    3),
        FIELDEND
    };
    event_t e = INT_EVENT("hey", 2, HISTOGRAM, fields);
    regex_t re;
    assert_int_equal(regcomp(&re, "[AD]", REG_EXTENDED), 0);
    char* str = fmtString(fmt, &e, &re);
    assert_string_equal(str,
                 "{\"_metric\":\"hey\","
                 "\"_metric_type\":\"histogram\","
                 "\"_value\":2,"
                 "\"A\":\"Z\",\"D\":654}");
    if (str) free(str);
    regfree(&re);
    fmtDestroy(&fmt);
}

static void
fmtStringMetricJsonEscapedValues(void** state)
{
    format_t* fmt = fmtCreate(CFG_EVENT_ND_JSON);
    {
        event_t e = INT_EVENT("Paç \"fat!", 3, SET, NULL);    // embedded double quote
        char* str = fmtString(fmt, &e, NULL);
        assert_string_equal(str,
                 "{\"_metric\":\"Paç \\\"fat!\","
                 "\"_metric_type\":\"set\","
                 "\"_value\":3}");
        free(str);
    }

    {
        event_field_t fields[] = {
            STRFIELD("A",         "행운을	빕니다",    0),   // embedded tab
            NUMFIELD("Viel\\ Glück",     123,      1),   // embedded backslash
            FIELDEND
        };
        event_t e = INT_EVENT("you", 4, DELTA, fields);
        char* str = fmtString(fmt, &e, NULL);
        assert_string_equal(str,
                 "{\"_metric\":\"you\","
                 "\"_metric_type\":\"counter\","
                 "\"_value\":4,"
                 "\"A\":\"행운을\\t빕니다\","
                 "\"Viel\\\\ Glück\":123}");
        free(str);
    }
    fmtDestroy(&fmt);
}

int
main(int argc, char* argv[])
{
    printf("running %s\n", argv[0]);

    const struct CMUnitTest tests[] = {
        cmocka_unit_test(fmtCreateReturnsValidPtrForGoodFormat),
        cmocka_unit_test(fmtCreateReturnsNullPtrForBadFormat),
        cmocka_unit_test(fmtCreateHasExpectedDefaults),
        cmocka_unit_test(fmtDestroyNullDoesntCrash),
        cmocka_unit_test(fmtStatsDPrefixSetAndGet),
        cmocka_unit_test(fmtStatsDMaxLenSetAndGet),
        cmocka_unit_test(fmtOutVerbositySetAndGet),
        cmocka_unit_test(fmtCustomTagsSetAndGet),
        cmocka_unit_test(fmtStringStatsDNullEventDoesntCrash),
        cmocka_unit_test(fmtStringStatsDNullEventFieldsDoesntCrash),
        cmocka_unit_test(fmtStringNullFmtDoesntCrash),
        cmocka_unit_test(fmtStringStatsDHappyPath),
        cmocka_unit_test(fmtStringStatsDHappyPathFilteredFields),
        cmocka_unit_test(fmtStatsDWithCustomFields),
        cmocka_unit_test(fmtStatsDWithCustomAndStatsdFields),
        cmocka_unit_test(fmtStringStatsDReturnsNullIfSpaceIsInsufficient),
        cmocka_unit_test(fmtStringStatsDReturnsNullIfSpaceIsInsufficientMax),
        cmocka_unit_test(fmtStringStatsDVerifyEachStatsDType),
        cmocka_unit_test(fmtStringStatsDOmitsFieldsIfSpaceIsInsufficient),
        cmocka_unit_test(fmtStringStatsDHonorsCardinality),
        cmocka_unit_test(fmtEventMessageStringValue),
        cmocka_unit_test(fmtEventMessageStringWithEmbeddedNulls),
        cmocka_unit_test(fmtStringMetricJsonNoFields),
        cmocka_unit_test(fmtStringMetricJsonWFields),
        cmocka_unit_test(fmtStringMetricJsonWFilteredFields),
        cmocka_unit_test(fmtStringMetricJsonEscapedValues),
        cmocka_unit_test(dbgHasNoUnexpectedFailures),
    };
    return cmocka_run_group_tests(tests, groupSetup, groupTeardown);
}
