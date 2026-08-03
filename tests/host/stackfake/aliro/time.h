/* stackfake: aliro/time.h.
 *
 * The comparison operators are upstream's, inline, because
 * Interface::AccessDocument::VerifyValidityPeriod is implemented against them
 * in the fake and a different ordering would change which documents it accepts.
 *
 * FromTimestamp(const uint8_t *, size_t) is DECLARED only: that parser is
 * aliro_stack.cpp's, and it is one of the things under test. */
#ifndef STACKFAKE_ALIRO_TIME_H
#define STACKFAKE_ALIRO_TIME_H

#include <cstddef>
#include <cstdint>
#include <optional>

#include <aliro/types.h>

namespace Aliro
{

/** A wall-clock instant, to the second. */
class Time
{
      public:
	explicit constexpr Time(int year, int month, int day, int hour, int minute, int second)
		: mYear(year), mMonth(month), mDay(day), mHour(hour), mMinute(minute),
		  mSecond(second)
	{
	}
	constexpr Time() = default;

	friend bool operator<(const Time &lhs, const Time &rhs)
	{
		if (lhs.mYear != rhs.mYear) {
			return lhs.mYear < rhs.mYear;
		}
		if (lhs.mMonth != rhs.mMonth) {
			return lhs.mMonth < rhs.mMonth;
		}
		if (lhs.mDay != rhs.mDay) {
			return lhs.mDay < rhs.mDay;
		}
		if (lhs.mHour != rhs.mHour) {
			return lhs.mHour < rhs.mHour;
		}
		if (lhs.mMinute != rhs.mMinute) {
			return lhs.mMinute < rhs.mMinute;
		}
		return lhs.mSecond < rhs.mSecond;
	}
	friend bool operator>(const Time &lhs, const Time &rhs) { return rhs < lhs; }
	friend bool operator<=(const Time &lhs, const Time &rhs) { return !(rhs < lhs); }
	friend bool operator>=(const Time &lhs, const Time &rhs) { return !(lhs < rhs); }
	friend bool operator==(const Time &lhs, const Time &rhs)
	{
		return !(lhs < rhs) && !(rhs < lhs);
	}

	int Year() const { return mYear; }
	int Month() const { return mMonth; }
	int Day() const { return mDay; }
	int Hour() const { return mHour; }
	int Minute() const { return mMinute; }
	int Second() const { return mSecond; }

	/** Defined by aliro_stack.cpp -- the code under test. */
	static std::optional<Time> FromTimestamp(const uint8_t *timestamp, size_t length);

	static std::optional<Time> FromTimestamp(const Timestamp &timestamp)
	{
		return FromTimestamp(timestamp.data(), timestamp.size());
	}

      private:
	int mYear{};
	int mMonth{};
	int mDay{};
	int mHour{};
	int mMinute{};
	int mSecond{};
};

} // namespace Aliro

#endif /* STACKFAKE_ALIRO_TIME_H */
