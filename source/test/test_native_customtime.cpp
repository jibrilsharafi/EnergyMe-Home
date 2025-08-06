#include <unity.h>
#include <ctime>
#include <sys/time.h>
#include <cstdint>
#include <cstdio>

// Mock time for testing - we'll inject our own time values
static struct timeval mock_time;
static bool use_mock_time = false;

// Override gettimeofday for testing
int gettimeofday_mock(struct timeval *tv, struct timezone *tz) {
    if (use_mock_time && tv) {
        *tv = mock_time;
        return 0;
    }
    return gettimeofday(tv, tz);
}

// Mock the logger for native testing
struct MockLogger {
    template<typename... Args>
    void debug(const char* format, const char* tag, Args... args) {
        printf("[DEBUG] ");
        printf(format, args...);
        printf("\n");
    }
} logger;

static const char* TAG = "test";

// Copy the core logic from your function for native testing
bool isNowCloseToHour_native(uint64_t toleranceMillis) {
    struct timeval tv;
    if (use_mock_time) {
        tv = mock_time;
    } else {
        gettimeofday(&tv, NULL);
    }
    
    struct tm timeinfo;
    localtime_r(&tv.tv_sec, &timeinfo);
    
    // Calculate milliseconds since the current hour started
    uint64_t millisSinceCurrentHour = static_cast<uint64_t>(timeinfo.tm_min * 60 + timeinfo.tm_sec) * 1000;
    
    // Calculate milliseconds until the next hour
    uint64_t millisUntilNextHour = 3600000 - millisSinceCurrentHour;

    // Check if we're close to either the current hour (just passed) or the next hour (approaching)
    if (millisSinceCurrentHour <= toleranceMillis) {
        logger.debug("Current time is close to the current hour (within %llu ms since hour start)", TAG, toleranceMillis);
        return true;
    } else if (millisUntilNextHour <= toleranceMillis) {
        logger.debug("Current time is close to the next hour (within %llu ms)", TAG, toleranceMillis);
        return true;
    } else {
        logger.debug("Current time is not close to any hour (since hour: %llu ms, until next: %llu ms)", TAG, millisSinceCurrentHour, millisUntilNextHour);
        return false;
    }
}    

// Helper function to set mock time for testing
void setMockTime(int hour, int minute, int second) {
    struct tm timeinfo = {0};
    
    timeinfo.tm_year = 2025 - 1900;  // Years since 1900
    timeinfo.tm_mon = 7;             // August (0-based)
    timeinfo.tm_mday = 5;            // 5th day
    timeinfo.tm_hour = hour;
    timeinfo.tm_min = minute;
    timeinfo.tm_sec = second;
    timeinfo.tm_isdst = 0;
    
    time_t test_time = mktime(&timeinfo);
    mock_time.tv_sec = test_time;
    mock_time.tv_usec = 0;
    
    use_mock_time = true;
    
    printf("Mock time set to %02d:%02d:%02d\n", hour, minute, second);
    
    // Verify the mock time
    struct tm verify_tm;
    localtime_r(&mock_time.tv_sec, &verify_tm);
    printf("Verified mock time: %02d:%02d:%02d\n", verify_tm.tm_hour, verify_tm.tm_min, verify_tm.tm_sec);
}

void test_native_just_after_hour() {
    // Test at 23:00:01 (1 second after hour)
    setMockTime(23, 0, 1);
    
    bool result = isNowCloseToHour_native(60000); // 60 second tolerance
    TEST_ASSERT_TRUE(result);
}

void test_native_just_before_hour() {
    // Test at 22:59:59 (1 second before hour)
    setMockTime(22, 59, 59);
    
    bool result = isNowCloseToHour_native(60000); // 60 second tolerance
    TEST_ASSERT_TRUE(result);
}

void test_native_middle_of_hour() {
    // Test at 23:30:00 (middle of hour)
    setMockTime(23, 30, 0);
    
    bool result = isNowCloseToHour_native(60000); // 60 second tolerance
    TEST_ASSERT_FALSE(result);
}

void test_native_edge_cases() {
    // Test exactly at tolerance boundary - just after hour
    setMockTime(23, 1, 0); // 60 seconds after hour
    bool result1 = isNowCloseToHour_native(60000);
    TEST_ASSERT_TRUE(result1);
    
    setMockTime(23, 1, 1); // 61 seconds after hour
    bool result2 = isNowCloseToHour_native(60000);
    TEST_ASSERT_FALSE(result2);
    
    // Test exactly at tolerance boundary - before hour
    setMockTime(22, 59, 0); // 60 seconds before hour
    bool result3 = isNowCloseToHour_native(60000);
    TEST_ASSERT_TRUE(result3);
    
    setMockTime(22, 58, 59); // 61 seconds before hour
    bool result4 = isNowCloseToHour_native(60000);
    TEST_ASSERT_FALSE(result4);
}

void test_native_various_tolerances() {
    // Test with different tolerance values
    setMockTime(23, 0, 30); // 30 seconds after hour
    
    TEST_ASSERT_TRUE(isNowCloseToHour_native(60000));  // 60s tolerance - should pass
    TEST_ASSERT_TRUE(isNowCloseToHour_native(30000));  // 30s tolerance - should pass  
    TEST_ASSERT_FALSE(isNowCloseToHour_native(29000)); // 29s tolerance - should fail
}

int main() {
    UNITY_BEGIN();
    
    RUN_TEST(test_native_just_after_hour);
    RUN_TEST(test_native_just_before_hour);
    RUN_TEST(test_native_middle_of_hour);
    RUN_TEST(test_native_edge_cases);
    RUN_TEST(test_native_various_tolerances);
    
    return UNITY_END();
}
