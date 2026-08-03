/* nfcfake: aliro/errors.h — the error type the transport seam returns.
 * Richer than ecpfake's, because the transports return four distinct codes and
 * a suite has to tell them apart. */
#ifndef NFCFAKE_ALIRO_ERRORS_H
#define NFCFAKE_ALIRO_ERRORS_H

enum AliroErrorCode {
	ALIRO_NO_ERROR = 0,
	ALIRO_NO_MEMORY,
	ALIRO_ERROR_INTERNAL,
	ALIRO_INVALID_STATE,
	ALIRO_INVALID_ARGUMENT,
};

/** Zephyr's Aliro error wrapper, trimmed to comparison and an int accessor. */
class AliroError
{
      public:
	AliroError() : mCode(ALIRO_NO_ERROR) {}
	/* Implicit on purpose: the transports `return ALIRO_INVALID_STATE;`. */
	AliroError(AliroErrorCode code) : mCode(code) {}

	int ToInt() const { return static_cast<int>(mCode); }

	bool operator==(const AliroError &other) const { return mCode == other.mCode; }
	bool operator!=(const AliroError &other) const { return mCode != other.mCode; }

      private:
	AliroErrorCode mCode;
};

#endif /* NFCFAKE_ALIRO_ERRORS_H */
