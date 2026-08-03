/* stackfake: aliro/errors.h.
 *
 * The enum ORDER is load-bearing: aliro_stack.cpp indexes a string table with
 * the code and static_asserts that the table has exactly ALIRO_ERROR_MAX
 * entries, so a code inserted in the wrong place here would silently shift
 * every message. Transcribed from the upstream header for that reason.
 *
 * ToString() and FromInt() are DECLARED and not defined: they are the code
 * under test. */
#ifndef STACKFAKE_ALIRO_ERRORS_H
#define STACKFAKE_ALIRO_ERRORS_H

enum AliroErrorCode {
	ALIRO_NO_ERROR,
	ALIRO_NO_MEMORY,
	ALIRO_ERROR_INTERNAL,
	ALIRO_INVALID_STATE,
	ALIRO_INVALID_ARGUMENT,
	ALIRO_INVALID_SIGNATURE,
	ALIRO_INVALID_AUTHENTICATION_TAG,
	ALIRO_PUBLIC_KEY_NOT_FOUND,
	ALIRO_PUBLIC_KEY_EXPIRED,
	ALIRO_PUBLIC_KEY_NOT_TRUSTED,
	ALIRO_KEY_ALREADY_EXISTS,
	ALIRO_TIMEOUT,
	ALIRO_ERROR_NOT_IMPLEMENTED,
	ALIRO_TLV_INVALID_TAG,
	ALIRO_TLV_INVALID_LEN,
	ALIRO_TLV_BUFFER_TOO_SMALL,
	ALIRO_TLV_WRONG_DATA_TYPE,
	ALIRO_TLV_END_OF_TLV,
	ALIRO_ERROR_UNKNOWN,
	ALIRO_SESSION_NOT_FOUND,
	ALIRO_SESSION_TERMINATE,
	ALIRO_VERSION_NOT_SUPPORTED,
	ALIRO_APDU_STATUS_INVALID,
	ALIRO_ENCRYPTION_COUNTER_OVERFLOW,
	ALIRO_DECRYPTION_COUNTER_OVERFLOW,
	ALIRO_INVALID_DATA_FORMAT,
	ALIRO_INVALID_DATA_CONTENT,
	ALIRO_NO_PUBLIC_KEY_IN_RESPONSE,
	ALIRO_ERROR_MAX,
};

/** The stack's error wrapper. Implicitly constructible, as upstream's is. */
class AliroError
{
      public:
	AliroError() = default;
	AliroError(AliroErrorCode code) : mCode(code) {}

	AliroErrorCode ToErrorCode() const { return mCode; }
	int ToInt() const { return static_cast<int>(mCode); }

	friend bool operator==(AliroError lhs, AliroError rhs)
	{
		return lhs.ToErrorCode() == rhs.ToErrorCode();
	}
	friend bool operator!=(AliroError lhs, AliroError rhs)
	{
		return lhs.ToErrorCode() != rhs.ToErrorCode();
	}
	friend bool operator!=(AliroError lhs, AliroErrorCode rhs) { return lhs.ToErrorCode() != rhs; }
	friend bool operator!=(AliroErrorCode lhs, AliroError rhs) { return lhs != rhs.ToErrorCode(); }
	friend bool operator==(AliroError lhs, AliroErrorCode rhs) { return lhs.ToErrorCode() == rhs; }
	friend bool operator==(AliroErrorCode lhs, AliroError rhs) { return lhs == rhs.ToErrorCode(); }

	/* Defined by aliro_stack.cpp -- the code under test. */
	const char *ToString() const;
	static AliroError FromInt(int ec);

      private:
	AliroErrorCode mCode{ALIRO_NO_ERROR};
};

#endif /* STACKFAKE_ALIRO_ERRORS_H */
