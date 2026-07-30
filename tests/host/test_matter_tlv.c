/**
 * @file test_matter_tlv.c — Matter TLV encoder against CHIP's own golden bytes.
 *
 * The two multi-container vectors are copied from the vendored CHIP SDK,
 * workspace/modules/lib/matter/src/lib/core/tests/TestTLV.cpp: Encoding3 at
 * :2042-2048 and Encoding2 at :1898-1910, with TestProfile_1 = 0xAABBCCDD and
 * TestProfile_2 = 0x11223344 from :54-55. Pinning a foreign implementation's
 * output is the point -- a round-trip against our own reader would pass just as
 * happily if both halves shared a misreading of the spec.
 *
 * The single-element cases are derived from the constant tables (TLVTypes.h:60-86
 * element types, TLVTags.h:105-112 tag controls) rather than quoted, and are
 * marked as such below.
 */
#include <string.h>

#include "matter_tlv.h"

#include "test.h"

#define TEST_PROFILE_1 0xAABBCCDDu
#define TEST_PROFILE_2 0x11223344u

void test_matter_tlv(void)
{
	uint8_t buf[64];
	struct matter_tlv_writer w;
	size_t len;

	t_group("single elements (derived from the constant tables)");

	matter_tlv_writer_init(&w, buf, sizeof(buf));
	T_EQ("bool true rc", matter_tlv_put_bool(&w, MATTER_TLV_ANON, true), MATTER_TLV_OK);
	T_EQ("bool true finish", matter_tlv_writer_finish(&w, &len), MATTER_TLV_OK);
	t_vec("anon bool true", buf, len, "09");

	matter_tlv_writer_init(&w, buf, sizeof(buf));
	(void)matter_tlv_put_null(&w, MATTER_TLV_ANON);
	T_EQ("null finish", matter_tlv_writer_finish(&w, &len), MATTER_TLV_OK);
	t_vec("anon null", buf, len, "14");

	/* Context tag 1 + UInt8 = 0x20 | 0x04, one tag octet, then the value. */
	matter_tlv_writer_init(&w, buf, sizeof(buf));
	(void)matter_tlv_put_u64(&w, MATTER_TLV_CTX(1), 42u);
	T_EQ("ctx u8 finish", matter_tlv_writer_finish(&w, &len), MATTER_TLV_OK);
	t_vec("ctx1 uint 42", buf, len, "24012a");

	/* Signed values pick the narrowest width: -17 fits Int8, so 0x20 | 0x00. */
	matter_tlv_writer_init(&w, buf, sizeof(buf));
	(void)matter_tlv_put_i64(&w, MATTER_TLV_CTX(0), -17);
	T_EQ("ctx i8 finish", matter_tlv_writer_finish(&w, &len), MATTER_TLV_OK);
	t_vec("ctx0 int -17", buf, len, "2000ef");

	matter_tlv_writer_init(&w, buf, sizeof(buf));
	(void)matter_tlv_put_u64(&w, MATTER_TLV_ANON, 0x1234u);
	T_EQ("u16 finish", matter_tlv_writer_finish(&w, &len), MATTER_TLV_OK);
	t_vec("anon uint 0x1234 is 2-octet", buf, len, "053412");

	matter_tlv_writer_init(&w, buf, sizeof(buf));
	(void)matter_tlv_put_utf8(&w, MATTER_TLV_ANON, "Hello!", 6u);
	T_EQ("utf8 finish", matter_tlv_writer_finish(&w, &len), MATTER_TLV_OK);
	t_vec("anon utf8 Hello!", buf, len, "0c0648656c6c6f21");

	matter_tlv_writer_init(&w, buf, sizeof(buf));
	(void)matter_tlv_put_bytes(&w, MATTER_TLV_ANON, (const uint8_t *)"\x00\x01\x02\x03\x04", 5u);
	T_EQ("bytes finish", matter_tlv_writer_finish(&w, &len), MATTER_TLV_OK);
	t_vec("anon octets 0..4", buf, len, "10050001020304");

	matter_tlv_writer_init(&w, buf, sizeof(buf));
	(void)matter_tlv_start_container(&w, MATTER_TLV_ANON, MATTER_TLV_STRUCTURE);
	(void)matter_tlv_end_container(&w);
	T_EQ("empty struct finish", matter_tlv_writer_finish(&w, &len), MATTER_TLV_OK);
	t_vec("empty anon structure", buf, len, "1518");

	t_group("CHIP TestTLV.cpp Encoding3 (:2042-2048)");

	matter_tlv_writer_init(&w, buf, sizeof(buf));
	matter_tlv_writer_set_implicit_profile(&w, TEST_PROFILE_2);
	(void)matter_tlv_start_container(&w, MATTER_TLV_PROFILE(TEST_PROFILE_1, 1),
					 MATTER_TLV_STRUCTURE);
	(void)matter_tlv_put_bool(&w, MATTER_TLV_PROFILE(TEST_PROFILE_2, 2), false);
	(void)matter_tlv_end_container(&w);
	T_EQ("Encoding3 finish", matter_tlv_writer_finish(&w, &len), MATTER_TLV_OK);
	T_EQ("Encoding3 length", (long)len, 11L);
	t_vec("Encoding3", buf, len, "d5bbaaddcc010088020018");

	t_group("CHIP TestTLV.cpp Encoding2 (:1898-1910)");

	matter_tlv_writer_init(&w, buf, sizeof(buf));
	matter_tlv_writer_set_implicit_profile(&w, TEST_PROFILE_2);
	(void)matter_tlv_start_container(&w, MATTER_TLV_PROFILE(TEST_PROFILE_1, 1),
					 MATTER_TLV_STRUCTURE);
	(void)matter_tlv_put_bool(&w, MATTER_TLV_PROFILE(TEST_PROFILE_1, 2), true);
	(void)matter_tlv_put_bool(&w, MATTER_TLV_PROFILE(TEST_PROFILE_2, 2), false);
	(void)matter_tlv_end_container(&w);
	(void)matter_tlv_start_container(&w, MATTER_TLV_PROFILE(TEST_PROFILE_2, 1),
					 MATTER_TLV_STRUCTURE);
	(void)matter_tlv_put_bool(&w, MATTER_TLV_PROFILE(TEST_PROFILE_2, 2), false);
	(void)matter_tlv_put_bool(&w, MATTER_TLV_PROFILE(TEST_PROFILE_1, 2), true);
	(void)matter_tlv_end_container(&w);
	T_EQ("Encoding2 finish", matter_tlv_writer_finish(&w, &len), MATTER_TLV_OK);
	T_EQ("Encoding2 length", (long)len, 32L);
	t_vec("Encoding2", buf, len,
	      "d5bbaaddcc0100c9bbaaddcc020088020018950100880200c9bbaaddcc020018");

	t_group("implicit profile is opt-in");

	/* Same tag, no implicit profile nominated, so it must go fully qualified. */
	matter_tlv_writer_init(&w, buf, sizeof(buf));
	(void)matter_tlv_put_bool(&w, MATTER_TLV_PROFILE(TEST_PROFILE_2, 2), false);
	T_EQ("no-implicit finish", matter_tlv_writer_finish(&w, &len), MATTER_TLV_OK);
	/* 0x11223344 splits vendor 0x1122 then profile 0x3344, each little-endian,
	 * the same way Encoding3 splits 0xAABBCCDD into bb aa dd cc. */
	t_vec("profile tag without implicit", buf, len, "c8221144330200");

	t_group("errors are latched, not silent");

	/* One byte short of the two an anonymous 1-octet uint needs. */
	matter_tlv_writer_init(&w, buf, 1u);
	(void)matter_tlv_put_u64(&w, MATTER_TLV_ANON, 42u);
	T_EQ("overflow latches NOSPACE", matter_tlv_writer_finish(&w, &len), MATTER_TLV_E_NOSPACE);

	/* Sticky: a later put on a failed writer must not resurrect it. */
	T_EQ("later put keeps the error", matter_tlv_put_bool(&w, MATTER_TLV_ANON, true),
	     MATTER_TLV_E_NOSPACE);

	matter_tlv_writer_init(&w, buf, sizeof(buf));
	(void)matter_tlv_start_container(&w, MATTER_TLV_ANON, MATTER_TLV_STRUCTURE);
	T_EQ("unclosed container fails finish", matter_tlv_writer_finish(&w, &len),
	     MATTER_TLV_E_STATE);

	matter_tlv_writer_init(&w, buf, sizeof(buf));
	T_EQ("end without start", matter_tlv_end_container(&w), MATTER_TLV_E_STATE);

	matter_tlv_writer_init(&w, buf, sizeof(buf));
	T_EQ("bad container type", matter_tlv_start_container(&w, MATTER_TLV_ANON, 0x04u),
	     MATTER_TLV_E_INVAL);

	matter_tlv_writer_init(&w, buf, sizeof(buf));
	for (int i = 0; i < MATTER_TLV_MAX_DEPTH; i++) {
		(void)matter_tlv_start_container(&w, MATTER_TLV_ANON, MATTER_TLV_ARRAY);
	}
	T_EQ("depth cap holds", w.rc, MATTER_TLV_OK);
	T_EQ("one past the cap fails",
	     matter_tlv_start_container(&w, MATTER_TLV_ANON, MATTER_TLV_ARRAY),
	     MATTER_TLV_E_DEPTH);
}
