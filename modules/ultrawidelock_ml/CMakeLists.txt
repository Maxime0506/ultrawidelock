# Contributes nothing unless CONFIG_ULTRAWIDELOCK_ML_LOS=y, so every image that does not
# ask for it stays byte-identical. No image asks for it today: the classifier is
# built and certified by the host suite, and wiring it to a decision in the
# ranging path is a separate change.

if(CONFIG_ULTRAWIDELOCK_ML_LOS)

  zephyr_library_named(ultrawidelock_ml)
  zephyr_library_sources(${CMAKE_CURRENT_SOURCE_DIR}/src/ultrawidelock_ml_los.c
                         ${CMAKE_CURRENT_SOURCE_DIR}/src/ultrawidelock_ml_lin.c
                         ${CMAKE_CURRENT_SOURCE_DIR}/src/ultrawidelock_ml_feat.c
                         ${CMAKE_CURRENT_SOURCE_DIR}/src/ultrawidelock_ml_range.c
                         ${CMAKE_CURRENT_SOURCE_DIR}/src/ultrawidelock_ml_log2.c)

  # The tree and the scaler are `static const` and belong to one translation
  # unit; putting them on the global include path would copy the scaler into
  # every file that reached for it. Only the feature count and order are public,
  # and those live in include/ where ultrawidelock_ml.h can re-export them.
  zephyr_library_include_directories(${CMAKE_CURRENT_SOURCE_DIR}/src)

  zephyr_include_directories(${CMAKE_CURRENT_SOURCE_DIR}/include)

endif()
