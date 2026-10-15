#include <zephyr/ztest.h>

/* These are the same assertion bodies used by the plain native/sanitizer runner. */
void softusb_test_codec(void);
void softusb_test_transactions(void);

ZTEST(softusb_portable, test_codec)
{
    softusb_test_codec();
}
ZTEST(softusb_portable, test_transactions)
{
    softusb_test_transactions();
}
ZTEST_SUITE(softusb_portable, NULL, NULL, NULL, NULL, NULL);
